import datetime
import math

import fbprophet

import numpy

import pandas

import rows.forecast.visit


def winsorize(frame, tail):
    series = frame['y'].sort_values()
    len_series = len(series)
    if len_series < 10:
        return frame.copy()

    index = len_series - 1
    bottom = series.iloc[int(math.floor(index * tail))]
    top = series.iloc[int(math.ceil(index * (1.0 - tail)))]

    frame_to_use = frame.copy()
    frame_to_use['y'] = frame_to_use['y'].apply(lambda v: v if v >= bottom else bottom)
    frame_to_use['y'] = frame_to_use['y'].apply(lambda v: v if v <= top else top)
    return frame_to_use


def make_median_from_day_forecast(baseline, cutoff_point, periods):
    history_frame = baseline[baseline['ds'] < cutoff_point].copy()

    buckets = [[], [], [], [], [], [], []]
    values = []
    for row in history_frame.itertuples():
        value = row.y.to_timedelta64().astype(float)
        buckets[row.ds.dayofweek].append(value)
        values.append(value)

    yhat_bucket = []
    for bucket in buckets:
        if not bucket:
            yhat_bucket.append(numpy.median(values))
        else:
            yhat_bucket.append(numpy.median(bucket))

    data = []
    for day in range(periods):
        data.append([cutoff_point + datetime.timedelta(days=day), yhat_bucket[(cutoff_point.weekday() + day) % 7]])
    return pandas.DataFrame(columns=['ds', 'yhat'], data=data)


def make_prophet_forecast(baseline, cutoff_point, periods):
    end_point = cutoff_point + datetime.timedelta(days=periods)

    cut_frame = baseline[baseline['ds'] < cutoff_point]
    model_frame = cut_frame[['ds', 'y']].copy()

    m = fbprophet.Prophet(growth='linear', interval_width=0.95)
    m.fit(model_frame)
    prophet_periods = (end_point - m.history['ds'].max()).days - 1
    assert prophet_periods > 0

    future_frame = m.make_future_dataframe(periods=prophet_periods)
    forecast_frame = m.predict(future_frame)
    return forecast_frame


def make_judgemental_forecast(baseline, cutoff_point, periods):
    end_point = cutoff_point + datetime.timedelta(days=periods)
    results = baseline[(baseline['ds'] >= cutoff_point) & (baseline['ds'] < end_point)].copy()
    results['yhat'] = results['planned_duration'].apply(lambda x: x.to_timedelta64().astype(float))
    return results[['ds', 'yhat']].copy()


def make_combined_forecast(history_frame, cutoff_point, periods):
    days_look_behind = 60

    two_months_before_history_end = cutoff_point - datetime.timedelta(days=days_look_behind)
    num_two_months_samples = len(history_frame[(history_frame['ds'] >= two_months_before_history_end)
                                               & (history_frame['ds'] < cutoff_point)]['ds'].unique())
    if num_two_months_samples >= 0.75 * days_look_behind:
        return 'prophet', make_prophet_forecast(history_frame, cutoff_point, periods)
    else:
        return 'day_median', make_median_from_day_forecast(history_frame, cutoff_point, periods)


class ForecastModel:

    def __init__(self):
        self.__forecast_frame = None

    def train(self, visits, start_time, end_time):
        assert visits
        assert start_time <= end_time
        data = [visit.to_list() for visit in visits]
        frame = pandas.DataFrame(columns=rows.forecast.visit.Visit.columns(), data=data)
        frame['ds'] \
            = frame['planned_start_time'].apply(lambda x: datetime.datetime.combine(x.date(), datetime.time()))
        frame['y'] = frame['real_duration']

        visits_after_cutoff = frame[frame['ds'] >= start_time]
        assert visits_after_cutoff.empty

        client_ids = frame['client_id'].unique()
        assert len(client_ids) == 1

        frame = winsorize(frame, 0.05)
        history_frame = frame[frame['y'] >= pandas.Timedelta('00:05:00')].copy()
        periods = (end_time - start_time).days + 1

        _forecast_method, forecast_frame = make_combined_forecast(history_frame, start_time, periods)
        forecast_frame.set_index('ds', inplace=True)
        self.__forecast_frame = forecast_frame

    def forecast(self, date):
        date_time = datetime.datetime.combine(date, datetime.time())
        assert date_time in self.__forecast_frame.index

        forecast_value = self.__forecast_frame.loc[date_time]['yhat']
        return pandas.Timedelta(forecast_value, 'ns')
