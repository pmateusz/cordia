#!/usr/bin/env python3
import concurrent.futures
import collections
import datetime
import functools
import itertools
import logging
import os

import numpy

import pandas

import sklearn.cluster

import tqdm

import matplotlib.pyplot
import matplotlib.pylab

import statsmodels.api

from analysis import VisitCSVSourceFile, time_to_seconds

# select a user
# develop a model with prediction
# plot the model and errors
# compare with errors that real humans do
from mpl_toolkits.mplot3d import Axes3D


class Cluster:

    def __init__(self, label, items=None):
        self.__label = label
        self.__items = items if items else []

    def add(self, item):
        self.__items.append(item)

    @property
    def label(self):
        return self.__label

    @property
    def items(self):
        return self.__items

    @property
    def user(self):
        if self.__items:
            return self.__items[0].user
        return None

    def data_frame(self):
        data_frame = pandas.DataFrame(
            index=pandas.DatetimeIndex(data=[clear_time(visit.original_start) for visit in self.items]),
            data=[(time_to_seconds(visit.real_start.time()), visit.real_duration.total_seconds()) for visit in
                  self.items],
            columns=['Start', 'Duration'])
        data_frame.sort_index(inplace=True)
        # remove outliers and invalid values, resample, fill missing values
        data_frame.where(
            (numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()) & (
                    data_frame.Duration > 0), inplace=True)
        data_frame.dropna(inplace=True)
        data_frame = data_frame.resample('D').mean()
        return data_frame

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

    penalty = 0.0
    if left.original_start.date() == right.original_start.date():
        penalty = 100.0

    if left.tasks == right.tasks \
            or left.tasks.tasks.issubset(right.tasks.tasks) \
            or right.tasks.tasks.issubset(left.tasks.tasks):
        return abs(left_start_min - right_start_min) + abs(left_duration_min - right_duration_min) + penalty
    else:
        left_task_price = left_duration_min / len(left.tasks.tasks)
        right_task_price = right_duration_min / len(right.tasks.tasks)
        return abs(left_start_min - right_start_min) \
               + left_task_price * len(left.tasks.tasks.difference(right.tasks.tasks)) \
               + right_task_price * len(right.tasks.tasks.difference(left.tasks.tasks)) + penalty


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


def get_series_name(group, cluster):
    if cluster.label == -1:
        return '{0} - outliers'.format(group[0].tasks)
    centroid = cluster.centroid()
    return '{0} - {1}'.format(group[0].tasks, centroid.original_start.time())


def plot_clusters(clusters, user_id, output_dir):
    fig, ax = matplotlib.pyplot.subplots()
    __COLOR_MAP = 'tab10'
    matplotlib.pyplot.set_cmap(__COLOR_MAP)
    color_map = matplotlib.cm.get_cmap(__COLOR_MAP)
    color_it = iter(color_map.colors)
    shapes = ['s', 'o', 'D', 'X']
    shape_it = iter(shapes)
    task_shapes = {}
    label_colors = {}

    group_handles = []
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
                                           marker='s',
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

    legend = matplotlib.pylab.legend(list(map(lambda item: item[0], group_handles)),
                                     list(map(lambda item: item[1], group_handles)),
                                     loc=9,
                                     bbox_to_anchor=(0.5, -0.30),
                                     ncol=2,
                                     shadow=False,
                                     frameon=True)
    matplotlib.pyplot.savefig(os.path.join(output_dir, str(user_id) + '.png'),
                              additional_artists=(legend,),
                              bbox_inches='tight')
    matplotlib.pyplot.close(fig)


def cluster():
    visits = load_visits('/home/pmateusz/dev/cordia/output.csv')
    output_dir = '/home/pmateusz/dev/cordia/data/clustering'
    user_counter = collections.Counter()
    for visit in visits:
        user_counter[visit.user] += 1
    print('Loaded {0} users'.format(user_counter))

    # if more than 2 visits in the same day lower distance
    for user_id in user_counter:  # tqdm.tqdm(user_counter, unit='users', desc='Clustering', leave=False):
        visits_to_use = [v for v in visits if v.user == user_id]
        raw_visits_to_use = numpy.array(visits_to_use)

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
        # plot_clusters(clusters, output_dir)

        for index in range(1, len(clusters), 1):
            cluster = clusters[index]
            data_frame = pandas.DataFrame(
                [(clear_time(visit.original_start), visit.real_duration.total_seconds() / 60.0)
                 for visit in cluster.items],
                columns=['DateTime', 'Duration'])

            data_frame.to_pickle(os.path.join(output_dir, 'u{0}_c{1}.pickle'.format(user_id, cluster.label)))
    return

    # data_frame = data_frame \
    #     .where((numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()) & (
    #         data_frame.Duration > 0))
    # data_frame = data_frame.dropna()
    # data_frame.index = data_frame.DateTime
    # data_frame = data_frame.resample('D').mean()
    # data_frame = data_frame.interpolate(method='linear')
    # # decomposition_result = statsmodels.api.tsa.seasonal_decompose(data_frame.Duration)
    # # decomposition_result.plot()
    # # matplotlib.pyplot.show()
    # #
    # # result = statsmodels.api.tsa.stattools.adfuller(data_frame.Duration)
    #
    # training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame, test_size=0.2, shuffle=False)
    #
    # # compare with average
    # ets = statsmodels.api.tsa.ExponentialSmoothing(numpy.asarray(training_frame.Duration),
    #                                                trend=None,
    #                                                damped=False,
    #                                                seasonal='add',
    #                                                seasonal_periods=7)
    #
    # holt_winters = ets.fit(smoothing_level=0.15, use_boxcox='log', optimized=True, use_basinhopping=True)
    #
    # arima = statsmodels.api.tsa.statespace.SARIMAX(training_frame.Duration,
    #                                                trend=None,
    #                                                order=(1, 1, 4),
    #                                                enforce_stationarity=True,
    #                                                enforce_invertibility=True,
    #                                                seasonal_order=(1, 1, 1, 7)).fit()
    #
    # test_frame_to_use = test_frame.copy()
    # test_frame_to_use['HoltWinters'] = holt_winters.forecast(len(test_frame))
    # test_frame_to_use['ARIMA'] = arima.predict(start="2017-10-24", end="2017-12-31", dynamic=True)
    # test_frame_to_use['Average'] = training_frame.Duration.mean()
    # test_frame_to_use['MovingAverage'] = training_frame.Duration.rolling(10).mean().iloc[-1]
    #
    # matplotlib.pyplot.figure(figsize=(16, 8))
    # matplotlib.pyplot.plot(training_frame.Duration, label='Train')
    # matplotlib.pyplot.plot(test_frame_to_use.Duration, label='Test')
    # matplotlib.pyplot.plot(test_frame_to_use['HoltWinters'], label='HoltWinters: {0:.3f}'.format(
    #     numpy.sqrt(sklearn.metrics.mean_squared_error(
    #         test_frame_to_use.Duration, test_frame_to_use['HoltWinters']))))
    # matplotlib.pyplot.plot(test_frame_to_use['ARIMA'], label='ARIMA: {0:.3f}'.format(
    #     numpy.sqrt(sklearn.metrics.mean_squared_error(
    #         test_frame_to_use.Duration, test_frame_to_use['ARIMA']))))
    # matplotlib.pyplot.plot(test_frame_to_use['Average'], label='Average: {0:.3f}'.format(
    #     numpy.sqrt(sklearn.metrics.mean_squared_error(
    #         test_frame_to_use.Duration, test_frame_to_use['Average']))))
    # matplotlib.pyplot.plot(test_frame_to_use['MovingAverage'], label='MovingAverage: {0:.3f}'.format(
    #     numpy.sqrt(sklearn.metrics.mean_squared_error(
    #         test_frame_to_use.Duration, test_frame_to_use['MovingAverage']))))
    # matplotlib.pyplot.legend(loc='best')
    # matplotlib.pyplot.show()


def normalize(data_frame, last_date):
    first_valid_index = data_frame.first_valid_index()
    last_valid_index = data_frame.last_valid_index()
    duration_series_to_use = data_frame.truncate(first_valid_index, last_valid_index)
    week_days_served = duration_series_to_use.Duration.groupby(by=lambda dt: dt.week).count()
    non_zero_week_days_served = week_days_served.where(week_days_served > 0).dropna()

    weeks_to_use = non_zero_week_days_served.where(numpy.abs(
        non_zero_week_days_served - non_zero_week_days_served.mean()) <= 1.96 * non_zero_week_days_served.std()) \
        .dropna()

    data_set = []
    for week_of_year, freq in weeks_to_use.iteritems():
        begin_offset = datetime.datetime(2017, 1, 1)
        end_offset = begin_offset
        if int(week_of_year) > 0 and int(week_of_year) != 52:
            # the first of January 2017 is recognized as the 52 week
            begin_offset += datetime.timedelta(days=7 - begin_offset.weekday())
            if int(week_of_year) > 1:
                begin_offset += datetime.timedelta(weeks=int(week_of_year) - 1, days=0)
            end_offset = begin_offset + datetime.timedelta(days=6)
        data_set.extend(data_frame.loc[begin_offset:end_offset].values)
    return pandas.DataFrame(index=pandas.date_range(end=last_date, periods=len(data_set)),
                            data=data_set,
                            columns=data_frame.columns)


def plot_time_series(training_frame, test_frame):
    ets = statsmodels.api.tsa.ExponentialSmoothing(numpy.asarray(training_frame.Duration),
                                                   damped=False,
                                                   trend=None,
                                                   seasonal='add',
                                                   seasonal_periods=14)

    holt_winters = ets.fit(optimized=True, use_boxcox=True, remove_bias=True)

    test_frame_to_use = test_frame.copy()
    test_frame_to_use['HoltWinters'] = holt_winters.forecast(len(test_frame))
    test_frame_to_use['Average'] = training_frame.Duration.mean()

    matplotlib.pyplot.figure(figsize=(16, 8))
    matplotlib.pyplot.plot(training_frame.Duration, label='Train')
    matplotlib.pyplot.plot(test_frame_to_use.Duration, label='Test')
    matplotlib.pyplot.plot(test_frame_to_use['HoltWinters'], label='HoltWinters: {0:.3f}'.format(
        numpy.sqrt(sklearn.metrics.mean_squared_error(
            test_frame_to_use.Duration, test_frame_to_use['HoltWinters']))))
    matplotlib.pyplot.plot(test_frame_to_use['Average'], label='Average: {0:.3f}'.format(
        numpy.sqrt(sklearn.metrics.mean_squared_error(
            test_frame_to_use.Duration, test_frame_to_use['Average']))))
    matplotlib.pyplot.legend(loc='best')
    matplotlib.pyplot.show()


def compute_kmeans_clusters(visits):
    visits.sort(key=lambda visit: visit.user)

    def compute_user_clusters(visit_group):
        MIN_CLUSTER_SIZE = 8

        day_visit_counter = collections.Counter()
        for visit in visit_group:
            day_visit_counter[visit.original_start.date()] += 1
        _, n_clusters = day_visit_counter.most_common()[0]

        visit_frequency_counter = collections.Counter()
        for date, count in day_visit_counter.items():
            visit_frequency_counter[count] += 1

        n_clusters = 1
        for count, freq in visit_frequency_counter.items():
            if freq >= MIN_CLUSTER_SIZE:
                n_clusters = max(n_clusters, count)

        possible_dates = [date for date, count in day_visit_counter.items() if count == n_clusters]
        centroid_candidates = []
        for date in possible_dates:
            centroids = [visit for visit in visit_group if visit.original_start.date() == date]
            centroids.sort(key=lambda item: item.original_start)
            centroid_candidates.append(centroids)

        centroid_distances = [(centroid, numpy.mean(
            [numpy.mean([distance(left, right) for left, right in zip(centroid, other)])
             for other in centroid_candidates if centroid != other])) for centroid in centroid_candidates]
        centroid_to_use = min(centroid_distances, key=lambda centroid_distance: centroid_distance[1])[0]
        centroid_distances = [[distance(centroid, visit) for visit in visit_group] for centroid in centroid_to_use]
        distances = calculate_distance(numpy.array(visit_group), distance)
        db = sklearn.cluster.KMeans(n_init=1,
                                    n_clusters=n_clusters,
                                    precompute_distances=False,
                                    init=numpy.array(centroid_distances))
        db.fit(distances)
        labels = db.labels_
        cluster_count = len(set(labels)) - (1 if -1 in labels else 0)
        clusters = [Cluster(label) for label in range(-1, cluster_count, 1)]
        for visit, label in zip(visit_group, labels):
            clusters[label + 1].add(visit)
        return clusters

    results = []
    groups = [(user_id, list(group)) for user_id, group in itertools.groupby(visits, lambda v: v.user)]
    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(compute_user_clusters, visit_group) for user_id, visit_group in groups]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list),
                           total=len(futures_list),
                           unit='users',
                           desc='Clustering', leave=False):
            results.append(f.result())
    return results


def compute_clusters(visits):
    visits.sort(key=lambda visit: visit.user)

    def compute_user_clusters(visit_group):
        MAX_EPS, MIN_EPS = 6 * 15 + 1, 15 + 1

        distances = calculate_distance(numpy.array(visit_group), distance)
        eps_threshold = MAX_EPS
        while True:
            db = sklearn.cluster.DBSCAN(eps=eps_threshold, min_samples=8, metric='precomputed')
            db.fit(distances)
            labels = db.labels_
            cluster_count = len(set(labels)) - (1 if -1 in labels else 0)
            clusters = [Cluster(label) for label in range(-1, cluster_count, 1)]
            for visit, label in zip(visit_group, labels):
                clusters[label + 1].add(visit)

            if eps_threshold == MIN_EPS:
                return clusters

            separable_clusters = True
            for cluster in clusters[1:]:
                duplicate_counter = collections.Counter()
                for item in cluster.items:
                    duplicate_counter[item.original_start.date()] += 1
                duplicate_percent = \
                    sum(1 for _, freq in duplicate_counter.items() if freq > 1) / len(duplicate_counter.keys())
                if duplicate_percent >= 0.1:
                    item_index = {visit: visit_group.index(visit) for visit in visit_group}
                    distance_counter = collections.Counter()
                    for left, right in itertools.combinations(item_index.values(), 2):
                        left_right_distance = int(numpy.ceil(distances[left][right]))
                        if MIN_EPS < left_right_distance < eps_threshold:
                            distance_counter[left_right_distance] += 1
                    eps_candidates = [distances for distances, count in distance_counter.items()]
                    if eps_candidates:
                        eps_threshold = max(eps_candidates) - 1
                        separable_clusters = False
                        break

            if separable_clusters:
                return clusters

    results = []
    groups = [(user_id, list(group)) for user_id, group in itertools.groupby(visits, lambda v: v.user)]
    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(compute_user_clusters, visit_group) for user_id, visit_group in groups]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list),
                           total=len(futures_list), unit='users',
                           desc='Clustering', leave=False):
            results.append(f.result())
    return results


def save_clusters(clusters, output_directory):
    for user_clusters in tqdm.tqdm(clusters, total=len(clusters), leave=False):
        if user_clusters:
            user = next((cluster.user for cluster in user_clusters if cluster.user != None), None)
            if user:
                plot_clusters(user_clusters, user, output_directory)

    def cluster_path(user, label):
        return os.path.join(output_directory, 'user', user, 'cluster', label)

    def save_user_clusters(clusters):
        for cluster in clusters:
            if not cluster or cluster.label == -1 or not cluster.user:
                continue
            cluster_dir = cluster_path(str(cluster.user), str(cluster.label))
            if not os.path.exists(cluster_dir):
                os.makedirs(cluster_dir)
            cluster_file = VisitCSVSourceFile(os.path.join(cluster_dir, 'visits.csv'))
            cluster_file.write(cluster.items)

    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(save_user_clusters, user_clusters) for user_clusters in clusters]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list), total=len(futures_list), leave=False):
            f.result()


def load_clusters(input_directory):
    users_root_dir = os.path.join(input_directory, 'user')
    user_dirs = list(filter(os.path.isdir, (os.path.join(users_root_dir, user_part)
                                            for user_part in os.listdir(users_root_dir)
                                            if user_part.isdigit())))
    user_clusters = []
    for user_dir in user_dirs:
        clusters = []
        clusters_root_dir = os.path.join(user_dir, 'cluster')
        labels = [cluster_part for cluster_part in os.listdir(clusters_root_dir) if cluster_part.isdigit()]
        for label in labels:
            cluster_dir = os.path.join(clusters_root_dir, label)
            if not os.path.isdir(cluster_dir):
                continue
            visit_file = os.path.join(cluster_dir, 'visits.csv')
            if not os.path.isfile(visit_file):
                continue
            visit_source = VisitCSVSourceFile(visit_file)
            visits = visit_source.read()
            clusters.append(Cluster(int(label), visits))
        user_clusters.append(clusters)
    return user_clusters


def test_clusters():
    root_dir = '/home/pmateusz/dev/cordia/data/clustering/'
    for file_name in os.listdir(root_dir):
        if not file_name.endswith('pickle'):
            continue
        data_frame = pandas.read_pickle(os.path.join(root_dir, file_name))
        data_frame = data_frame \
            .where((numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std())
                   & (data_frame.Duration > 0))
        data_frame.index = data_frame.DateTime
        data_frame = data_frame.resample('D').mean()
        data_frame = data_frame.dropna()
        data_frame_to_use = normalize(data_frame, datetime.datetime(2017, 10, 1))

        if data_frame_to_use.Duration.count() / data_frame_to_use.size > 0.75 \
                and data_frame_to_use.Duration.count() > 64:
            data_frame_to_use = data_frame_to_use.interpolate(method='linear')

            training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame_to_use,
                                                                                  test_size=14,
                                                                                  shuffle=False)
            plot_time_series(training_frame, test_frame)


if __name__ == '__main__':
    tqdm.tqdm.monitor_interval = 0

    # TODO: put limit on maximum allowed date to use for clustering
    # TODO: compute models
    # TODO: save models
    # TODO: load models
    # TODO: forecast
    # action = 'load_clusters'
    action = 'save_clusters'
    # action = 'compare_forecasts'
    if action == 'save_clusters':
        source_file = VisitCSVSourceFile('/home/pmateusz/dev/cordia/output.csv')
        visits = source_file.read()
        user_clusters = compute_kmeans_clusters(visits)
        save_clusters(user_clusters, '/home/pmateusz/dev/cordia/data/clustering')
    elif action == 'load_clusters':
        forecast_errors = []
        cluster_groups = load_clusters('/home/pmateusz/dev/cordia/data/clustering')
        clusters = [cluster for cluster_group in cluster_groups for cluster in cluster_group]


        def forecast_cluster(cluster):
            data_frame = cluster.data_frame()
            if data_frame.empty:
                return None

            fill_factor = float(data_frame.Duration.count()) / len(data_frame.index)
            if fill_factor < 0.50:
                return None

            time_delta = data_frame.last_valid_index() - data_frame.first_valid_index()
            if time_delta.days < 32:
                return None

            data_frame.where(
                (numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()),
                inplace=True)

            try:
                data_frame.interpolate(method='linear', inplace=True)
                training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame,
                                                                                      test_size=14,
                                                                                      shuffle=False)
                ets = statsmodels.api.tsa.ExponentialSmoothing(numpy.asarray(training_frame.Duration),
                                                               damped=False,
                                                               trend=None,
                                                               seasonal='add',
                                                               seasonal_periods=7)

                holt_winters = ets.fit(optimized=True, remove_bias=True)

                test_frame_to_use = test_frame.copy()
                test_frame_to_use['HoltWinters'] = holt_winters.forecast(len(test_frame))
                test_frame_to_use['Average'] = training_frame.Duration.mean()

                holt_winters_error = sklearn.metrics.mean_squared_error(test_frame_to_use.Duration,
                                                                        test_frame_to_use['HoltWinters'])
                average_error = sklearn.metrics.mean_squared_error(test_frame_to_use.Duration,
                                                                   test_frame_to_use['Average'])
                return fill_factor, time_delta.days, holt_winters_error, average_error
            except:
                logging.exception('Uncaught exception')
                return None


        results = []
        with concurrent.futures.ThreadPoolExecutor() as executor:
            futures_list = [executor.submit(forecast_cluster, cluster) for cluster in clusters]
            for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list),
                               total=len(futures_list), unit='users',
                               desc='Forecasting', leave=False):
                result = f.result()
                if result:
                    results.append(result)
        results_data_frame = pandas.DataFrame(data=results,
                                              columns=['FillFactor', 'Days', 'HoltWintersError', 'AverageError'])
        results_data_frame.to_pickle('forecast_errors.pickle')
    elif action == 'compare_forecasts':
        data_frame = pandas.read_pickle('/home/pmateusz/dev/cordia/forecast_errors.pickle')

        fill_factors = list(set(data_frame.FillFactor))
        fill_factors.sort()

        days = list(set(data_frame.Days))
        days.sort()

        data_set = []
        combinations = [(fill_factor, day) for fill_factor in fill_factors for day in days]
        with tqdm.tqdm(combinations, total=len(combinations)) as t:
            for fill_factor, day in t:
                data_frame_query = data_frame.where((data_frame.FillFactor >= fill_factor))
                average_error = data_frame_query.AverageError.mean()
                holt_winters_error = data_frame_query.HoltWintersError.mean()
                data_set.append((fill_factor, day, average_error, holt_winters_error))
        error_data_frame = pandas.DataFrame(data=data_set,
                                            columns=['FillFactor', 'Days', 'HoltWintersMeanError', 'AverageMeanError'])

        fig = matplotlib.pyplot.figure()
        ax = Axes3D(fig)
        surf = ax.scatter(error_data_frame.FillFactor,
                          error_data_frame.Days,
                          error_data_frame.HoltWintersMeanError)
        matplotlib.pyplot.show()

        # data_frame = data_frame \
        #     .where((numpy.abs(data_frame.HoltWintersError - data_frame.HoltWintersError.mean()) <= 3 * data_frame.HoltWintersError.std()))
        # data_frame = data_frame \
        #     .where((numpy.abs(data_frame.AverageError - data_frame.AverageError.mean()) <= 3 * data_frame.AverageError.std()))
