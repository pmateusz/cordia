#!/usr/bin/env python3

import argparse
import datetime
import concurrent
import concurrent.futures
import itertools
import logging
import os
import warnings

import rows.forecast.visit
import rows.forecast.cluster
import rows.forecast.forecast

import numpy
import pandas

import fbprophet
import fbprophet.plot

import tqdm
import matplotlib.pyplot
import matplotlib.dates
import matplotlib.ticker


def parse_args():
    parser = argparse.ArgumentParser()

    subparsers = parser.add_subparsers(dest='command')

    prepare_cluster = subparsers.add_parser(name='prepare')
    prepare_cluster.add_argument('data_set_file')
    prepare_cluster.add_argument('--output')

    cluster_parser = subparsers.add_parser(name='cluster')
    cluster_parser.add_argument('data_set_file')
    cluster_parser.add_argument('--output')
    cluster_parser.add_argument('--table', required=True)

    forecast_parser = subparsers.add_parser(name='forecast')
    forecast_parser.add_argument('data_set_file')

    residuals_parser = subparsers.add_parser(name='compute-residuals')
    residuals_parser.add_argument('data_set_file')

    investigate_parser = subparsers.add_parser(name='investigate')
    investigate_parser.add_argument('data_set_file')
    investigate_parser.add_argument('--client', required=True)
    investigate_parser.add_argument('--cluster', required=True)

    plot_residuals_parser = subparsers.add_parser(name='plot-residuals')
    plot_residuals_parser.add_argument('data_set_file')

    subparsers.add_parser(name='test')

    return parser.parse_args()


def prepare_dataset_command(args):
    data_set_file_path = getattr(args, 'data_set_file')
    output_path = getattr(args, 'output')

    frame = pandas.read_csv(data_set_file_path, header=None, names=['visit_id',
                                                                    'client_id',
                                                                    'tasks',
                                                                    'area',
                                                                    'carer',
                                                                    'planned_start_time',
                                                                    'planned_end_time',
                                                                    'check_in',
                                                                    'check_out',
                                                                    'check_in_processed'],
                            index_col=False)

    def parse_datetime(value):
        return datetime.datetime.strptime(value, '%Y-%m-%d %H:%M:%S.%f')

    frame['planned_end_time'] = frame['planned_end_time'].apply(parse_datetime)
    frame['planned_start_time'] = frame['planned_start_time'].apply(parse_datetime)
    frame['check_in'] = frame['check_in'].apply(parse_datetime)
    frame['check_out'] = frame['check_out'].apply(parse_datetime)
    frame['planned_duration'] = frame['planned_end_time'] - frame['planned_start_time']
    frame['real_duration'] = frame['check_out'] - frame['check_in']
    frame['check_in_processed'] = frame['check_in_processed'].astype('bool')
    frame['tasks'] = frame['tasks'].apply(rows.forecast.visit.Tasks)

    frame.to_hdf(output_path, key='a')


class PandasTimeDeltaConverter:

    def __init__(self, unit):
        self.__unit = unit

    def __call__(self, x, pos=None):
        try:
            timedelta = pandas.Timedelta(value=x, unit=self.__unit)
            py_timedelta = timedelta.to_pytimedelta()
            total_seconds = py_timedelta.total_seconds()
            hours, minutes, seconds = PandasTimeDeltaConverter.split_total_seconds(total_seconds)
            return '{0}{1:02d}:{2:02d}:{3:02d}'.format('' if total_seconds > 0.0 else '-', hours, minutes, seconds)
        except:
            logging.exception('Failure to convert {0} to time delta'.format(x))
            return x

    @staticmethod
    def split_total_seconds(value):
        abs_value = abs(value)
        hours = int(abs_value // matplotlib.dates.SEC_PER_HOUR)
        minutes = int((abs_value - hours * matplotlib.dates.SEC_PER_HOUR) // matplotlib.dates.SEC_PER_MIN)
        seconds = int(abs_value - 3600 * hours - 60 * minutes)
        return hours, minutes, seconds


def save_figure(figure, file_name):
    matplotlib.pyplot.savefig(file_name + '.png', transparent=True)
    matplotlib.pyplot.close(figure)


def visualize_cluster(data_frame, client_id):
    selected_visits = data_frame[data_frame['client_id'] == client_id].copy()

    selected_visits['planned_start_date'] \
        = selected_visits.apply(lambda row: row['planned_start_time'].date(), axis=1)
    selected_visits['real_start_time'] \
        = selected_visits.apply(lambda row: row['planned_start_time'].time(), axis=1)

    color_map = matplotlib.pyplot.get_cmap('tab20')
    clusters = selected_visits['cluster'].unique()
    fig, ax = matplotlib.pyplot.subplots(figsize=(5, 8))
    for cluster in clusters:
        cluster_frame = selected_visits[selected_visits['cluster'] == cluster]
        ax.plot_date(cluster_frame['planned_start_date'],
                     cluster_frame['real_start_time'],
                     c=color_map.colors[cluster], fmt='s', markersize=3)

    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(PandasTimeDeltaConverter('ns')))
    ax.set_yticks(numpy.arange(0, 24 * matplotlib.dates.SEC_PER_HOUR + 1, 2 * matplotlib.dates.SEC_PER_HOUR))
    ax.set_ylim(bottom=0, top=24 * matplotlib.dates.SEC_PER_HOUR)
    matplotlib.pyplot.xticks(rotation=70)

    ax.set_xlabel('Date')
    ax.set_ylabel('Check In')
    matplotlib.pyplot.tight_layout()

    return fig, ax


def cluster_command(args):
    data_set_file = getattr(args, 'data_set_file')
    output_file = getattr(args, 'output')
    table = getattr(args, 'table', None)

    frame = pandas.read_hdf(data_set_file)

    if not output_file:
        file_name = os.path.basename(data_set_file)
        data_set_file_no_ext, __ = os.path.splitext(file_name)
        output_file_name = 'clusters_' + data_set_file_no_ext + '.hdf'
        output_file = os.path.join(os.path.dirname(data_set_file), output_file_name)

    with warnings.catch_warnings():
        warnings.filterwarnings('ignore', '', tqdm.TqdmSynchronisationWarning)
        with tqdm.tqdm(frame.itertuples(), desc='Loading the data set', leave=False) as reader:
            visits = rows.forecast.visit.Visit.load_from_tuples(reader)
            visits_to_use = rows.forecast.visit.filter_incorrect_visits(visits)
            visits_to_use.sort(key=lambda v: v.client_id)
    visit_groups = {client_id: list(visit_group)
                    for client_id, visit_group in itertools.groupby(visits_to_use, lambda v: v.client_id)}

    cluster_frame = frame.copy()
    cluster_frame['cluster'] = 0

    def cluster(visit_group):
        model = rows.forecast.cluster.AgglomerativeModel(rows.forecast.cluster.NoSameDayPlannedStarDurationDistanceMatrix())
        return model.cluster(visit_group)

    records = []
    with warnings.catch_warnings():
        warnings.filterwarnings('ignore', '', tqdm.TqdmSynchronisationWarning)
        with tqdm.tqdm(desc='Computing clusters', total=len(visit_groups), leave=False) as cluster_progress_bar:
            with concurrent.futures.ThreadPoolExecutor() as executor:
                futures_list = [executor.submit(cluster, visit_groups[visit_group]) for visit_group in visit_groups]
                for f in concurrent.futures.as_completed(futures_list):
                    try:
                        customer_clusters = f.result()
                        if customer_clusters:
                            for label, clustered_visits in customer_clusters.items():
                                for visit in clustered_visits:
                                    record = visit.to_list()
                                    record.append(label)
                                    records.append(record)
                        cluster_progress_bar.update(1)
                    except:
                        logging.exception('Exception in processing results')

    record_columns = list(rows.forecast.Visit.columns())
    record_columns.append('cluster')
    data_frame = pandas.DataFrame.from_records(records, columns=record_columns)

    output_params = {'mode': 'w'}
    if table:
        output_params['key'] = table

    data_frame.to_hdf(output_file, **output_params)

    class IdGenerator:
        def __init__(self):
            self.__counter = 0
            self.__mapping = dict()

        def __call__(self, id):
            if id in self.__mapping:
                return self.__mapping[id]
            result = self.__counter
            self.__counter += 1
            self.__mapping[id] = result
            return result

    visit_id_gen = IdGenerator()
    client_id_gen = IdGenerator()

    rows = []
    for row in data_frame.itertuples():
        # client_id = client_id_gen(row.client_id)
        client_id = row.client_id
        visit_id = visit_id_gen(row.visit_id)
        row = [visit_id,
               client_id,
               row.tasks,
               row.planned_start_time,
               row.planned_start_time.time(),
               row.planned_end_time,
               row.planned_duration,
               row.check_in,
               row.check_in.time(),
               row.check_out,
               row.real_duration,
               row.cluster]
        rows.append(row)

    excel_frame = pandas.DataFrame(columns=['visit_id',
                                            'client_id',
                                            'tasks',
                                            'planned_start',
                                            'planned_start_time',
                                            'planned_end',
                                            'planned_duration',
                                            'real_start',
                                            'real_start_time',
                                            'real_end',
                                            'real_duration',
                                            'cluster'], data=rows)
    excel_frame['planned_duration_excel'] = excel_frame['planned_duration'].apply(lambda v: str(v))
    excel_frame['real_duration_excel'] = excel_frame['real_duration'].apply(lambda v: str(v))
    excel_frame.to_excel('start_time_clusters.xlsx')


def calculate_mape(forecast, baseline, cutoff_point, periods):
    end_point = cutoff_point + datetime.timedelta(days=periods)

    bare_forecast = forecast[forecast['ds'] >= cutoff_point]
    bare_baseline = baseline[(baseline['ds'] >= cutoff_point) & (baseline['ds'] < end_point)].copy()
    bare_baseline['yhat'] = bare_baseline['y'].apply(lambda x: x.to_timedelta64().astype(float))
    bare_baseline.set_index('ds', inplace=True)

    components = 0.0
    cumulative_error = 0.0
    for row in bare_forecast.itertuples():
        if bare_baseline.index.contains(row.ds):
            components += 1.0
            baseline_yhat = bare_baseline.loc[row.ds]['yhat'].mean()
            cumulative_error += abs((baseline_yhat - row.yhat) / baseline_yhat)
    assert components > 0.0
    return (cumulative_error / components) * 100.0


def forecast_command(args):
    cluster_data_set = getattr(args, 'cluster_data_set_file')
    data_frame = pandas.read_hdf(cluster_data_set)

    cutoff_point = datetime.datetime(2017, 10, 1)
    periods = 14
    end_point = cutoff_point + datetime.timedelta(days=periods)

    data_frame['ds'] \
        = data_frame['planned_start_time'].apply(lambda x: datetime.datetime.combine(x.date(), datetime.time()))
    data_frame['y'] = data_frame['real_duration']

    def benchmark_forecast(client_id, cluster_label, data_frame):
        try:
            cut_frame = data_frame[data_frame['planned_start_time'] < cutoff_point]
            validation_frame = data_frame[(data_frame['planned_start_time'] >= cutoff_point)
                                          & (data_frame['planned_start_time'] < end_point)]

            prophet_periods = (end_point - cut_frame['ds'].max()).days - 1

            prophet_frame = rows.forecast.forecast.make_prophet_forecast(data_frame, cutoff_point, periods)
            prophet_mape = calculate_mape(prophet_frame, data_frame, cutoff_point, periods)

            planners_frame = rows.forecast.forecast.make_judgemental_forecast(data_frame, cutoff_point, periods)
            planners_mape = calculate_mape(planners_frame, data_frame, cutoff_point, periods)

            day_median_frame = rows.forecast.forecast.make_median_from_day_forecast(data_frame, cutoff_point, periods)
            day_median_mape = calculate_mape(day_median_frame, data_frame, cutoff_point, periods)

            return [client_id,
                    cluster_label,
                    prophet_periods,
                    len(validation_frame),
                    len(cut_frame),
                    prophet_mape,
                    day_median_mape,
                    planners_mape]
        except:
            logging.exception('Failed to validate benchmark for client %d cluster %d', client_id, cluster_label)
            return [client_id,
                    cluster_label,
                    0,
                    0,
                    0,
                    0.0,
                    0.0,
                    0.0]

    # frame = pandas.read_hdf('benchmark.hdf', key='a')
    # frame.to_excel('benchmark.xlsx')
    # sys.exit(0)

    with warnings.catch_warnings():
        warnings.filterwarnings('ignore', '', tqdm.TqdmSynchronisationWarning)

        with concurrent.futures.ThreadPoolExecutor() as executor:
            futures_list = []
            client_ids = data_frame['client_id'].unique()
            for client_id in client_ids:
                cluster_labels = data_frame[(data_frame['client_id'] == client_id)]['cluster'].unique()
                for cluster_label in cluster_labels:
                    data_frame_to_use = data_frame[(data_frame['client_id'] == client_id)
                                                   & (data_frame['cluster'] == cluster_label)].copy()

                    validation_frame = data_frame_to_use[(data_frame_to_use['ds'] >= cutoff_point)
                                                         & (data_frame_to_use['ds'] < end_point)].copy()

                    training_frame = data_frame_to_use[(data_frame_to_use['ds'] < cutoff_point)]
                    if len(training_frame) > 2 and not validation_frame.empty:
                        handle = executor.submit(benchmark_forecast, client_id, cluster_label, data_frame_to_use)
                        futures_list.append(handle)

            results = []
            with tqdm.tqdm(desc='Computing benchmark', total=len(futures_list), leave=False) as progress_bar:
                for f in concurrent.futures.as_completed(futures_list):
                    try:
                        result = f.result()
                        results.append(result)
                        progress_bar.update(1)
                    except:
                        logging.exception('Exception in processing results')

    results = pandas.DataFrame(data=results,
                               columns=['client_id', 'cluster', 'periods', 'validation_size', 'history_size',
                                        'prophet_mape', 'day_median_mape', 'planner_mape'])
    results.to_hdf('benchmark.hdf', key='a')


def investigate_command(args):
    # interesting issues:
    # 9092362	3 - remove zeros
    # 9096903	1 - remove zeros
    # 9028645	0 - remove zeros
    # 9097709	2 - trend adjustment - changepoint_prior_scale=10.0, n_changepoints=120
    # 9082163	2 - outlier in the validation set
    # 9096892	4 - outlier in the validation set, changepoint_prior_scale=5.0, n_changepoints=120

    data_set_file = getattr(args, 'data_set_file')
    data_frame = pandas.read_hdf(data_set_file)

    client_id = int(getattr(args, 'client'))
    cluster_label = int(getattr(args, 'cluster'))

    cutoff_point = datetime.datetime(2017, 10, 1)
    periods = 14
    end_point = cutoff_point + datetime.timedelta(days=periods)

    # reject one percent longest visits
    data_frame['ds'] \
        = data_frame['planned_start_time'].apply(lambda x: datetime.datetime.combine(x.date(), datetime.time()))
    data_frame['y'] = data_frame['real_duration']
    selected_frame = data_frame[(data_frame['client_id'] == client_id)
                                & (data_frame['cluster'] == cluster_label)
        # & (data_frame['real_duration'] > pandas.Timedelta('00:05:00'))
                                ].copy()

    cut_frame = selected_frame[selected_frame['planned_start_time'] < cutoff_point]
    model_frame = cut_frame[['ds', 'y']].copy()
    model_frame = rows.forecast.forecast.winsorize(model_frame, 0.05)
    m = fbprophet.Prophet(growth='linear', interval_width=0.95, changepoint_prior_scale=5.0, n_changepoints=120)
    m.fit(model_frame)

    prophet_periods = (end_point - m.history['ds'].max()).days - 1
    assert prophet_periods > 0

    future_frame = m.make_future_dataframe(periods=prophet_periods)
    forecast_frame = m.predict(future_frame)
    prophet_mape = calculate_mape(forecast_frame, selected_frame, cutoff_point, periods)

    day_median_frame = rows.forecast.forecast.make_median_from_day_forecast(selected_frame, cutoff_point, periods)
    day_median_mape = calculate_mape(day_median_frame, selected_frame, cutoff_point, periods)

    planners_frame = rows.forecast.forecast.make_judgemental_forecast(selected_frame, cutoff_point, periods)
    planners_mape = calculate_mape(planners_frame, selected_frame, cutoff_point, periods)

    fig = m.plot(forecast_frame)
    ax = matplotlib.pyplot.gca()

    real_points = selected_frame[(selected_frame['planned_start_time'] < end_point)
                                 & (selected_frame['planned_start_time'] >= cutoff_point)]

    ax.plot(real_points['ds'].dt.to_pydatetime(), real_points['y'], '.', color='red')
    ax.plot(day_median_frame['ds'].dt.to_pydatetime(), day_median_frame['yhat'], '-', color='grey')
    ax.plot(planners_frame['ds'].dt.to_pydatetime(), planners_frame['yhat'], '-', color='black')
    ax.set_xlabel('Time [Y-m]')
    ax.set_ylabel('Duration [H:M:S]')
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(PandasTimeDeltaConverter('ns')))

    y_max = selected_frame['y'].max().total_seconds() + 15 * 60 * 10 ** 9
    ax.set_yticks(numpy.arange(0, y_max, 30 * 60 * 10 ** 9))

    handles, labels = ax.get_legend_handles_labels()
    ax.legend([handles[1], handles[3], handles[4], handles[2], handles[0]],
              ['Prophet Forecast (MAPE={0:3.2f}%)'.format(prophet_mape),
               'Day Median Forecast (MAPE={0:3.2f}%)'.format(day_median_mape),
               'Planners Forecast (MAPE={0:3.2f}%)'.format(planners_mape),
               'Real Value',
               'Training Value'])
    fig.tight_layout()

    # fbprophet.plot.add_changepoints_to_plot(ax, m, forecast_frame)
    fig.savefig('investigate_{0}_{1}.png'.format(client_id, cluster_label))

    fig = m.plot_components(forecast_frame)
    axes = fig.get_axes()
    for ax in axes:
        ax.set_ylabel('Duration [H:M:S]')
        ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(PandasTimeDeltaConverter('ns')))
    axes[0].set_xlabel('Time [Y-m]')
    axes[0].set_yticks(numpy.arange(0, 45 * 60 * 10 ** 9, 10 * 60 * 10 ** 9))
    fig.tight_layout()
    fig.savefig('investigate_components_{0}_{1}.png'.format(client_id, cluster_label))


def compute_residuals_command(args):
    data_set_file = getattr(args, 'data_set_file')
    data_frame = pandas.read_hdf(data_set_file)

    cutoff_point = datetime.datetime(2017, 10, 1)
    periods = 14
    end_point = cutoff_point + datetime.timedelta(days=periods)

    client_cluster_frame = data_frame[(data_frame['planned_start_time'] >= cutoff_point)
                                      & (data_frame['planned_start_time'] < end_point)
                                      & (data_frame['real_duration'] >= pandas.Timedelta('00:05:00'))][
        ['client_id', 'cluster']].drop_duplicates()

    def compute_residuals(frame):
        frame['ds'] = frame['planned_start_time'].apply(lambda x: datetime.datetime.combine(x.date(), datetime.time()))
        frame['y'] = frame['real_duration']

        columns = ['visit_id', 'client_id', 'cluster',
                   'planned_duration', 'real_duration',
                   'method', 'yhat', 'yhat_lower', 'yhat_upper']

        history_frame = frame[(frame['planned_start_time'] < cutoff_point)][['ds', 'y']].copy()
        if len(history_frame) > 0:
            forecast_periods = (end_point - history_frame['ds'].max()).days - 1
            assert forecast_periods > 0
            method_name, forecast_frame = rows.forecast.forecast.make_combined_forecast(history_frame, cutoff_point, periods)
            forecast_frame.set_index('ds', inplace=True)

            rows = []
            validation_frame = frame[
                (frame['planned_start_time'] >= cutoff_point) & (frame['planned_start_time'] < end_point)]
            for visit_row in validation_frame.itertuples():
                rows.append([
                    visit_row.visit_id, visit_row.client_id,
                    visit_row.cluster, visit_row.planned_duration, visit_row.real_duration, method_name,
                    forecast_frame.loc[visit_row.ds]['yhat'],
                    forecast_frame.loc[visit_row.ds]['yhat_lower'] if 'yhat_lower' in forecast_frame else None,
                    forecast_frame.loc[visit_row.ds]['yhat_upper'] if 'yhat_upper' in forecast_frame else None])
            return pandas.DataFrame(data=rows, columns=columns)
        return pandas.DataFrame(columns=columns, data=[])

    with warnings.catch_warnings():
        warnings.filterwarnings('ignore', '', tqdm.TqdmSynchronisationWarning)

        with concurrent.futures.ThreadPoolExecutor() as executor:
            futures_list = []

            for row in client_cluster_frame.itertuples():
                selected_frame = data_frame[(data_frame['client_id'] == row.client_id)
                                            & (data_frame['cluster'] == row.cluster)
                                            & (data_frame['real_duration'] >= pandas.Timedelta('00:05:00'))].copy()
                future_handle = executor.submit(compute_residuals, selected_frame)
                futures_list.append(future_handle)

            results = []
            with tqdm.tqdm(desc='Computing residuals', total=len(futures_list), leave=False) as progress_bar:
                for f in concurrent.futures.as_completed(futures_list):
                    try:
                        frame = f.result()
                        results.append(frame)
                        progress_bar.update(1)
                    except:
                        logging.exception('Exception in processing results')

            results_frame = pandas.concat(results)
            results_frame.to_hdf('residuals.hdf', key='a')


def numpy_ns_to_py_duration(x):
    if not x:
        return None
    return pandas.Timedelta(x, 'ns')


def plot_residuals_command(args):
    data_set_path = getattr(args, 'data_set_file')

    frame = pandas.read_hdf(data_set_path)
    frame['yhat_duration'] = frame['yhat'].apply(numpy_ns_to_py_duration)
    frame['yhat_lower_duration'] = frame['yhat_lower'].apply(numpy_ns_to_py_duration)
    frame['yhat_upper_duration'] = frame['yhat_upper'].apply(numpy_ns_to_py_duration)

    def compute_residuals(frame, baseline, forecast):
        series = frame[forecast] - frame[baseline]
        series = series.sort_values()
        return [value.astype('float64') for value in series.values]

    def plot_residuals(residuals, bins=64, left_limit=None, right_limit=None):
        residuals_to_use = residuals
        if left_limit:
            residuals_to_use = list(filter(lambda v: v >= left_limit, residuals_to_use))
        if right_limit:
            residuals_to_use = list(filter(lambda v: v <= right_limit, residuals_to_use))

        fig, ax = matplotlib.pyplot.subplots(1, 1)
        ax.hist(residuals_to_use, bins=bins)
        ax.set_xlabel('Forecast Error [H:M:S]')
        ax.set_ylabel('Samples')
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(PandasTimeDeltaConverter('ns')))
        fig.tight_layout()
        return fig

    bins = 64
    left_limit = -1.5 * 3600 * 10 ** 9
    right_limit = 1.5 * 3600 * 10 ** 9
    prophet_frame = frame[frame['method'] == 'prophet']
    prophet_residuals = compute_residuals(prophet_frame, 'real_duration', 'yhat_duration')
    fig = plot_residuals(prophet_residuals, bins, left_limit, right_limit)
    save_figure(fig, 'prophet_residuals')

    prophet_confidence_residuals = compute_residuals(prophet_frame, 'real_duration', 'yhat_upper_duration')
    fig = plot_residuals(prophet_confidence_residuals, bins, left_limit, right_limit)
    save_figure(fig, 'prophet_upper_residuals')

    day_mean_frame = frame[frame['method'] == 'day_median']
    day_median_residuals = compute_residuals(day_mean_frame, 'real_duration', 'yhat_duration')
    fig = plot_residuals(day_median_residuals, bins, left_limit, right_limit)
    save_figure(fig, 'day_median_residuals')

    method_residuals = compute_residuals(frame, 'real_duration', 'yhat_duration')
    fig = plot_residuals(method_residuals, bins, left_limit, right_limit)
    save_figure(fig, 'method_residuals')

    planners_residuals = compute_residuals(frame, 'real_duration', 'planned_duration')
    fig = plot_residuals(planners_residuals, bins, left_limit, right_limit)
    save_figure(fig, 'planners_residuals')


if __name__ == '__main__':
    logging.basicConfig(level=logging.ERROR)

    args = parse_args()
    command = getattr(args, 'command')

    if command == 'cluster':
        cluster_command(args)
    elif command == 'forecast':
        forecast_command(args)
    elif command == 'prepare':
        prepare_dataset_command(args)
    elif command == 'investigate':
        investigate_command(args)
    elif command == 'compute-residuals':
        compute_residuals_command(args)
    elif command == 'plot-residuals':
        plot_residuals_command(args)
