#!/usr/bin/env python3

import csv
import collections
import datetime
import functools
import itertools
import os

import numpy

import pandas

import analysis
import sklearn.cluster

import numpy as np

import matplotlib.pyplot
import matplotlib.pylab

import statsmodels.api


# select a user
# develop a model with prediction
# plot the model and errors
# compare with errors that real humans do

def load_visits(file_path):
    with open(file_path, 'r') as stream_reader:
        results = []
        sniffer = csv.Sniffer()
        dialect = sniffer.sniff(stream_reader.read(4096))
        stream_reader.seek(0)
        reader = csv.reader(stream_reader, dialect=dialect)
        next(reader)
        for row in reader:
            raw_visit_id, \
            raw_user_id, \
            raw_area_id, \
            raw_tasks, \
            _start_date, \
            raw_original_start, \
            _original_start_ord, \
            raw_original_duration, \
            _original_duration_ord, \
            raw_planned_start, \
            _planned_start_ord, \
            raw_planned_duration, \
            _planned_duration_ord, \
            raw_real_start, \
            _real_start_ord, \
            raw_real_duration, \
            _real_duration_ord, \
            raw_carer_count, \
            raw_checkout_method = row

            visit_id = int(raw_visit_id)
            user_id = int(raw_user_id)
            area_id = int(raw_area_id)
            tasks = analysis.str_to_tasks(raw_tasks)
            original_start = analysis.str_to_date_time(raw_original_start)
            original_duration = analysis.str_to_time_delta(raw_original_duration)
            planned_start = analysis.str_to_date_time(raw_planned_start)
            planned_duration = analysis.str_to_time_delta(raw_planned_duration)
            real_start = analysis.str_to_date_time(raw_real_start)
            real_duration = analysis.str_to_time_delta(raw_real_duration)
            carer_count = int(raw_carer_count)
            checkout_method = int(raw_checkout_method)

            results.append(analysis.SimpleVisit(id=visit_id,
                                                user=user_id,
                                                area=area_id,
                                                tasks=tasks,
                                                original_start=original_start,
                                                original_duration=original_duration,
                                                planned_start=planned_start,
                                                planned_duration=planned_duration,
                                                real_start=real_start,
                                                real_duration=real_duration,
                                                carer_count=carer_count,
                                                checkout_method=checkout_method))
        return results


class Cluster:

    def __init__(self, label):
        self.__label = label
        self.__items = []

    def add(self, item):
        self.__items.append(item)

    @property
    def label(self):
        return self.__label

    @property
    def items(self):
        return self.__items

    @staticmethod
    def __ordering(left, right):
        if left.tasks == right.tasks:
            if left.original_start == right.original_start:
                if left.original_duration == right.original_duration:
                    return 0
                else:
                    return -1 if left.original_duration < right.original_duration else 1
            else:
                return -1 if left.original_start < right.original_start else 1
        else:
            return -1 if str(left.tasks) < str(right.tasks) else 1

    def centroid(self):
        sorted_items = list(self.__items)
        sorted_items.sort(key=functools.cmp_to_key(self.__ordering))
        position = float(len(sorted_items)) / 2
        return sorted_items[int(position)]


def distance(left, right):
    left_start_min = left.original_start_ord / 60
    right_start_min = right.original_start_ord / 60
    left_duration_min = left.original_duration_ord / 60
    right_duration_min = right.original_duration_ord / 60

    if left.tasks == right.tasks \
            or left.tasks.tasks.issubset(right.tasks.tasks) \
            or right.tasks.tasks.issubset(left.tasks.tasks):
        return abs(left_start_min - right_start_min) + abs(left_duration_min - right_duration_min)
    else:
        left_task_price = left_duration_min / len(left.tasks.tasks)
        right_task_price = right_duration_min / len(right.tasks.tasks)
        return abs(left_start_min - right_start_min) \
               + left_task_price * len(left.tasks.tasks.difference(right.tasks.tasks)) \
               + right_task_price * len(right.tasks.tasks.difference(left.tasks.tasks))


def calculate_distance(records, metric):
    length = len(records)
    out = numpy.zeros((length, length), dtype='float')
    iterator = itertools.combinations(range(length), 2)
    for i, j in iterator:
        out[i, j] = metric(records[i], records[j])

    # Make symmetric
    # NB: out += out.T will produce incorrect results
    out = out + out.T

    # Calculate diagonal
    # NB: nonzero diagonals are allowed for both metrics and kernels
    for i in range(length):
        x = records[i]
        out[i, i] = metric(x, x)
    return out


__ZERO_DATE = datetime.datetime(2017, 1, 1, 0, 0)


def clear_date(value):
    return datetime.datetime(__ZERO_DATE.year,
                             __ZERO_DATE.month,
                             __ZERO_DATE.day,
                             value.hour,
                             value.minute,
                             value.second)


def clear_time(value):
    return datetime.datetime(value.year,
                             value.month,
                             value.day,
                             0,
                             0,
                             0)


def plot_clusters(clusters, output_dir):
    group_handles = []
    fig, ax = matplotlib.pyplot.subplots()
    __COLOR_MAP = 'tab10'
    matplotlib.pyplot.set_cmap(__COLOR_MAP)
    color_map = matplotlib.cm.get_cmap(__COLOR_MAP)
    color_it = iter(color_map.colors)
    shapes = ['s', 'o', 'D', 'X']
    shape_it = iter(shapes)
    task_shapes = {}
    label_colors = {}

    for cluster in clusters:
        task_groups = collections.defaultdict(list)
        for visit in cluster.items:
            task_groups[visit.tasks].append(visit)

        for tasks, group in task_groups.items():
            fill_color = 'w'
            edge_color = 'black'
            shape = 's'
            edge_width = 0.5
            size = 8

            if cluster.label != -1:
                edge_width = 0
                item = group[0]
                if item.tasks in task_shapes:
                    shape = task_shapes[item.tasks]
                else:
                    shape = next(shape_it, '.')
                    task_shapes[item.tasks] = shape

                if cluster.label in label_colors:
                    fill_color = label_colors[cluster.label]
                else:
                    fill_color = next(color_it, None)
                    label_colors[cluster.label] = fill_color

            start_days = []
            start_times = []
            for visit in group:
                start_days.append(clear_time(visit.original_start))
                start_times.append(clear_date(visit.original_start))
            task_label_handle = ax.scatter(start_days,
                                           start_times,
                                           c=fill_color,
                                           marker=shape,
                                           linewidth=edge_width,
                                           edgecolor=edge_color,
                                           alpha=0.7,
                                           s=size)

            group_handles.append((task_label_handle, get_series_name(group, cluster)))

    ax.yaxis.set_major_formatter(matplotlib.dates.DateFormatter('%H:%M'))
    ax.set_ylim(__ZERO_DATE + datetime.timedelta(hours=6), __ZERO_DATE + datetime.timedelta(hours=23, minutes=50))
    matplotlib.pyplot.xticks(rotation=-60)

    chart_box = ax.get_position()
    ax.set_position([chart_box.x0, chart_box.y0, chart_box.width, chart_box.height * 0.75])
    ax.grid(True)

    group_handles.sort(key=lambda item: item[1])

    legend = matplotlib.pylab.legend(map(lambda item: item[0], group_handles),
                                     map(lambda item: item[1], group_handles),
                                     loc=9,
                                     bbox_to_anchor=(0.5, -0.30),
                                     ncol=2,
                                     shadow=False,
                                     frameon=True)
    matplotlib.pyplot.savefig(os.path.join(output_dir, str(user_id) + '.png'),
                              additional_artists=(legend,),
                              bbox_inches='tight')
    matplotlib.pyplot.close(fig)


if __name__ == '__main__':
    visits = load_visits('/home/pmateusz/dev/cordia/output.csv')
    output_dir = '/home/pmateusz/dev/cordia/data/clustering'
    user_counter = collections.Counter()
    for visit in visits:
        user_counter[visit.user] += 1
    print('Loaded {0} users'.format(user_counter))


    def get_series_name(group, cluster):
        if cluster.label == -1:
            return '{0} - outliers'.format(group[0].tasks)
        centroid = cluster.centroid()
        return '{0} - {1}'.format(group[0].tasks, centroid.original_start.time())


    # if more than 2 visits in the same day lower distance
    for user_id in user_counter:  # tqdm.tqdm(user_counter, unit='users', desc='Clustering', leave=False):
        visits_to_use = [v for v in visits if v.user == user_id]
        raw_visits_to_use = np.array(visits_to_use)

        distances = calculate_distance(raw_visits_to_use, distance)
        eps_threshold = -1
        cluster_count = -1
        clusters = []
        for eps_threshold, min_samples in zip([91, 76, 61, 46, 31, 17], [8, 8, 8, 16, 16, 16]):
            db = sklearn.cluster.DBSCAN(eps=eps_threshold, min_samples=min_samples, metric='precomputed')
            db.fit(distances)

            labels = db.labels_
            cluster_count = len(set(labels)) - (1 if -1 in labels else 0)

            clusters = [Cluster(label) for label in range(-1, cluster_count, 1)]
            for visit, label in zip(visits_to_use, labels):
                clusters[label + 1].add(visit)

            has_duplicates = False
            for cluster in clusters[1:]:
                visit_dates = set((item.original_start.date() for item in cluster.items))
                if len(visit_dates) < len(cluster.items):
                    has_duplicates = True
                    break
            if not has_duplicates:
                break

        print('User=({0}) Clusters=({1}) Threshold=({2})'.format(user_id, cluster_count, eps_threshold))
        plot_clusters(clusters, output_dir)

        cluster = clusters[1]
        data_frame = pandas.DataFrame([(clear_time(visit.original_start), visit.real_duration.total_seconds() / 60.0)
                                       for visit in cluster.items],
                                      columns=['DateTime', 'Duration'])
        data_frame = data_frame \
            .where((numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()) & (
                data_frame.Duration > 0))
        data_frame = data_frame.dropna()
        data_frame.index = data_frame.DateTime
        data_frame = data_frame.resample('D').mean()
        data_frame = data_frame.interpolate(method='linear')
        # decomposition_result = statsmodels.api.tsa.seasonal_decompose(data_frame.Duration)
        # decomposition_result.plot()
        # matplotlib.pyplot.show()
        #
        # result = statsmodels.api.tsa.stattools.adfuller(data_frame.Duration)

        training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame, test_size=0.2, shuffle=False)

        # compare with average
        ets = statsmodels.api.tsa.ExponentialSmoothing(numpy.asarray(training_frame.Duration),
                                                       trend=None,
                                                       damped=False,
                                                       seasonal='add',
                                                       seasonal_periods=7)

        holt_winters = ets.fit(smoothing_level=0.15, use_boxcox='log', optimized=True, use_basinhopping=True)

        arima = statsmodels.api.tsa.statespace.SARIMAX(training_frame.Duration,
                                                       trend=None,
                                                       order=(1, 1, 4),
                                                       enforce_stationarity=True,
                                                       enforce_invertibility=True,
                                                       seasonal_order=(1, 1, 1, 7)).fit()

        test_frame_to_use = test_frame.copy()
        test_frame_to_use['HoltWinters'] = holt_winters.forecast(len(test_frame))
        test_frame_to_use['ARIMA'] = arima.predict(start="2017-10-24", end="2017-12-31", dynamic=True)
        test_frame_to_use['Average'] = training_frame.Duration.mean()
        test_frame_to_use['MovingAverage'] = training_frame.Duration.rolling(10).mean().iloc[-1]

        matplotlib.pyplot.figure(figsize=(16, 8))
        matplotlib.pyplot.plot(training_frame.Duration, label='Train')
        matplotlib.pyplot.plot(test_frame_to_use.Duration, label='Test')
        matplotlib.pyplot.plot(test_frame_to_use['HoltWinters'], label='HoltWinters: {0:.3f}'.format(
            numpy.sqrt(sklearn.metrics.mean_squared_error(
                test_frame_to_use.Duration, test_frame_to_use['HoltWinters']))))
        matplotlib.pyplot.plot(test_frame_to_use['ARIMA'], label='ARIMA: {0:.3f}'.format(
            numpy.sqrt(sklearn.metrics.mean_squared_error(
                test_frame_to_use.Duration, test_frame_to_use['ARIMA']))))
        matplotlib.pyplot.plot(test_frame_to_use['Average'], label='Average: {0:.3f}'.format(
            numpy.sqrt(sklearn.metrics.mean_squared_error(
                test_frame_to_use.Duration, test_frame_to_use['Average']))))
        matplotlib.pyplot.plot(test_frame_to_use['MovingAverage'], label='MovingAverage: {0:.3f}'.format(
            numpy.sqrt(sklearn.metrics.mean_squared_error(
                test_frame_to_use.Duration, test_frame_to_use['MovingAverage']))))
        matplotlib.pyplot.legend(loc='best')
        matplotlib.pyplot.show()
