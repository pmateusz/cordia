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

import scipy.spatial.distance
import scipy.optimize
import scipy.stats

import tqdm

import matplotlib.pyplot
import matplotlib.pylab

import statsmodels.api
import statsmodels.tsa.holtwinters

from analysis import VisitCSVSourceFile, time_to_seconds

# select a user
# develop a model with prediction
# plot the model and errors
# compare with errors that real humans do
from mpl_toolkits.mplot3d import Axes3D


class Cluster:

    def __init__(self, label, items=None, dir_path=None):
        self.__label = label
        self.__items = items if items else []
        self.__dir_path = dir_path

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

    def load_model(self):
        if not self.has_model():
            return None
        return statsmodels.tsa.holtwinters.ResultsWrapper.load(self.model_path)

    def has_model(self):
        return os.path.isfile(self.model_path)

    @property
    def model_path(self):
        return os.path.join(self.__dir_path, 'forecast.pickle')

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
        if not visit_group:
            return None

        MIN_CLUSTER_SIZE = 8

        day_visit_counter = collections.Counter()
        for visit in visit_group:
            day_visit_counter[visit.original_start.date()] += 1
        _, n_clusters = day_visit_counter.most_common()[0]

        visit_frequency_counter = collections.Counter()
        for date, count in day_visit_counter.items():
            visit_frequency_counter[count] += 1

        n_cluster_candidates = [count for count, freq in visit_frequency_counter.items() if freq >= MIN_CLUSTER_SIZE]
        if not n_cluster_candidates:
            logging.warning('User %s has not enough visits to distinguish clusters', visit_group[0].user)
            return None

        n_clusters = max(count for count, freq in visit_frequency_counter.items() if freq >= MIN_CLUSTER_SIZE)
        possible_dates = [date for date, count in day_visit_counter.items() if count == n_clusters]
        centroid_candidates = []
        for date in possible_dates:
            centroids = [visit for visit in visit_group if visit.original_start.date() == date]
            centroids.sort(key=lambda item: item.original_start)
            centroid_candidates.append(centroids)

        centroid_distances = [(centroid,
                               numpy.sum([numpy.power(
                                   numpy.sum([distance(left, right) for left, right in zip(centroid, other)]), 2.0)
                                   for other in centroid_candidates if centroid != other])) for centroid in
                              centroid_candidates]
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
            clusters = f.result()
            if clusters:
                results.append(clusters)
    return results


def compute_dbscan_clusters(visits):
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


class Paths:
    def __init__(self, root_dir):
        self.__root_dir = root_dir

    def users_dir(self):
        return os.path.join(self.__root_dir, 'user')

    def user_dir(self, user):
        return os.path.join(self.users_dir(), user)

    def clusters_dir(self, user):
        return os.path.join(self.user_dir(user), 'cluster')

    def cluster_dir(self, user, label):
        return os.path.join(self.clusters_dir(user), label)

    def visit_file(self, user, label):
        return os.path.join(self.cluster_dir(user, label), 'visits.csv')

    def forecast_file(self, user, label):
        return os.path.join(self.cluster_dir(user, label), 'forecast.pickle')

    @property
    def root_dir(self):
        return self.__root_dir


def save_clusters(clusters, output_directory):
    paths = Paths(output_directory)

    for user_clusters in tqdm.tqdm(clusters, total=len(clusters), leave=False):
        if user_clusters:
            user = next((cluster.user for cluster in user_clusters if cluster.user), None)
            if user:
                plot_clusters(user_clusters, user, output_directory)

    def save_user_clusters(clusters):
        for cluster in clusters:
            try:
                if not cluster or cluster.label == -1 or not cluster.user:
                    continue
                cluster_dir = paths.cluster_dir(str(cluster.user), str(cluster.label))
                if not os.path.exists(cluster_dir):
                    os.makedirs(cluster_dir)
                cluster_file = VisitCSVSourceFile(paths.visit_file(str(cluster.user), str(cluster.label)))
                cluster_file.write(cluster.items)
            except:
                logging.exception('Failed to save cluster %d of user %d', cluster.label, cluster.user)

    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(save_user_clusters, user_clusters) for user_clusters in clusters]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list), total=len(futures_list), leave=False):
            f.result()


def save_forecasts(cluster_groups, output_directory):
    paths = Paths(output_directory)

    def forecast_cluster(cluster):
        data_frame = cluster.data_frame()
        if data_frame.empty:
            return

        fill_factor = float(data_frame.Duration.count()) / len(data_frame.index)
        if fill_factor < 0.50:
            return

        time_delta = data_frame.last_valid_index() - data_frame.first_valid_index()
        if time_delta.days < 32:
            return

        data_frame.where(
            (numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()),
            inplace=True)

        try:
            data_frame.dropna(inplace=True)
            data_frame.interpolate(method='linear', inplace=True)
            training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame,
                                                                                  test_size=14,
                                                                                  shuffle=False)
            ets = statsmodels.api.tsa.ExponentialSmoothing(numpy.asarray(training_frame.Duration),
                                                           damped=False,
                                                           trend=None,
                                                           seasonal='add',
                                                           seasonal_periods=7)

            if ets.nobs < 14:
                # we are predicting for 2 weeks, so any smaller value does not make sense
                # especially the number of observations cannot be 12 to avoid division by 0 in the AICC formula
                return

            holt_winters = ets.fit(optimized=True, remove_bias=True)

            if numpy.isnan(holt_winters.sse):
                logging.warning('Model of cluster %d of user %d is invalid', cluster.label, cluster.user)

            holt_winters.save(paths.forecast_file(str(cluster.user), str(cluster.label)))
        except:
            logging.exception('Uncaught exception while processing user %s', cluster.user)

    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(forecast_cluster, cluster) for cluster_group in cluster_groups for cluster in
                        cluster_group]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list),
                           total=len(futures_list), unit='users',
                           desc='Forecasting', leave=False):
            f.result()


def load_clusters(input_directory):
    paths = Paths(input_directory)
    user_ids = list(user_id for user_id in os.listdir(paths.users_dir())
                    if user_id.isdigit() and os.path.isdir(paths.user_dir(user_id)))
    user_clusters = []
    for user_id in user_ids:
        clusters = []
        cluster_ids = [cluster_part for cluster_part in os.listdir(paths.clusters_dir(user_id)) if
                       cluster_part.isdigit()]
        for cluster_id in cluster_ids:
            cluster_dir = paths.cluster_dir(user_id, cluster_id)
            if not os.path.isdir(cluster_dir):
                continue
            visit_file = paths.visit_file(user_id, cluster_id)
            if not os.path.isfile(visit_file):
                continue
            visit_source = VisitCSVSourceFile(visit_file)
            clusters.append(Cluster(int(cluster_id), items=visit_source.read(), dir_path=cluster_dir))
        if clusters:
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


def test_models(cluster_groups, error_file):
    def test_model(cluster):
        if not cluster.has_model():
            return None
        try:
            data_frame = cluster.data_frame()
            data_frame.interpolate(method='linear', inplace=True)
            training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame,
                                                                                  test_size=14,
                                                                                  shuffle=False)
            model = cluster.load_model()
            if numpy.isnan(model.sse):
                logging.warning('Model of cluster %d of user %d is invalid', cluster.label, cluster.user)
                return None
            test_frame_to_use = test_frame.copy()
            test_frame_to_use['HoltWinters'] = model.forecast(len(test_frame))
            test_frame_to_use['Average'] = training_frame.Duration.mean()
            holt_winters_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['HoltWinters'])
            average_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['Average'])

            fill_factor = float(data_frame.Duration.count()) / len(data_frame.index)
            time_delta = data_frame.last_valid_index() - data_frame.first_valid_index()
            return [fill_factor, time_delta.days, holt_winters_error, average_error]
        except ValueError:
            logging.exception('Failed to process cluster %d of user %d', cluster.label, cluster.user)
            return None

    results = []
    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(test_model, cluster)
                        for cluster_group in cluster_groups
                        for cluster in cluster_group]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list),
                           total=len(futures_list), unit='users',
                           desc='Testing models', leave=False):
            result = f.result()
            if result:
                results.append(result)
    results_data_frame = pandas.DataFrame(data=results,
                                          columns=['FillFactor', 'Days', 'HoltWintersError', 'AverageError'])
    results_data_frame.to_pickle(error_file)


if __name__ == '__main__':
    tqdm.tqdm.monitor_interval = 0

    # TODO: put limit on maximum allowed date to use for clustering
    # TODO: forecast
    # action = 'save_forecasts'
    # action = 'save_clusters'
    # action = 'compare_forecasts'
    # action = 'test_models'
    action = 'reproduce_error'
    if action == 'save_clusters':
        source_file = VisitCSVSourceFile('/home/pmateusz/dev/cordia/output.csv')
        visits = source_file.read()
        user_clusters = compute_kmeans_clusters(visits)
        save_clusters(user_clusters, '/home/pmateusz/dev/cordia/data/clustering')
    elif action == 'save_forecasts':
        cluster_groups = load_clusters('/home/pmateusz/dev/cordia/data/clustering')
        save_forecasts(cluster_groups, '/home/pmateusz/dev/cordia/data/clustering')
    elif action == 'test_models':
        cluster_groups = load_clusters('/home/pmateusz/dev/cordia/data/clustering')
        test_models(cluster_groups, 'prediction_errors.pickle')
    elif action == 'reproduce_error':

        def fit(ets, smoothing_level=None, smoothing_slope=None, smoothing_seasonal=None,
                damping_slope=None, optimized=True, use_boxcox=False, remove_bias=False,
                use_basinhopping=False):
            """
            fit Holt Winter's Exponential Smoothing

            Parameters
            ----------
            smoothing_level : float, optional
                The alpha value of the simple exponential smoothing, if the value is
                set then this value will be used as the value.
            smoothing_slope :  float, optional
                The beta value of the holts trend method, if the value is
                set then this value will be used as the value.
            smoothing_seasonal : float, optional
                The gamma value of the holt winters seasonal method, if the value is
                set then this value will be used as the value.
            damping_slope : float, optional
                The phi value of the damped method, if the value is
                set then this value will be used as the value.
            optimized : bool, optional
                Should the values that have not been set above be optimized
                automatically?
            use_boxcox : {True, False, 'log', float}, optional
                Should the boxcox tranform be applied to the data first? If 'log'
                then apply the log. If float then use lambda equal to float.
            remove_bias : bool, optional
                Should the bias be removed from the forecast values and fitted values
                before being returned? Does this by enforcing average residuals equal
                to zero.
            use_basinhopping : bool, optional
                Should the opptimser try harder using basinhopping to find optimal
                values?

            Returns
            -------
            results : HoltWintersResults class
                See statsmodels.tsa.holtwinters.HoltWintersResults

            Notes
            -----
            This is a full implementation of the holt winters exponential smoothing as
            per [1]. This includes all the unstable methods as well as the stable methods.
            The implementation of the library covers the functionality of the R
            library as much as possible whilst still being pythonic.

            References
            ----------
            [1] Hyndman, Rob J., and George Athanasopoulos. Forecasting: principles and practice. OTexts, 2014.
            """

            def _holt_init(x, xi, p, y, l, b):
                """Initialization for the Holt Models"""
                p[xi] = x
                alpha, beta, _, l0, b0, phi = p[:6]
                alphac = 1 - alpha
                betac = 1 - beta
                y_alpha = alpha * y
                l[:] = 0
                b[:] = 0
                l[0] = l0
                b[0] = b0
                return alpha, beta, phi, alphac, betac, y_alpha

            def _holt__(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Simple Exponential Smoothing
                Minimization Function
                (,)
                """
                alpha, beta, phi, alphac, betac, y_alpha = _holt_init(x, xi, p, y, l, b)
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1]) + (alphac * (l[i - 1]))
                return scipy.spatial.distance.sqeuclidean(l, y)

            def _holt_mul_dam(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Multiplicative and Multiplicative Damped
                Minimization Function
                (M,) & (Md,)
                """
                alpha, beta, phi, alphac, betac, y_alpha = _holt_init(x, xi, p, y, l, b)
                if alpha == 0.0:
                    return max_seen
                if beta > alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1]) + (alphac * (l[i - 1] * b[i - 1] ** phi))
                    b[i] = (beta * (l[i] / l[i - 1])) + (betac * b[i - 1] ** phi)
                return scipy.spatial.distance.sqeuclidean(l * b ** phi, y)

            def _holt_add_dam(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Additive and Additive Damped
                Minimization Function
                (A,) & (Ad,)
                """
                alpha, beta, phi, alphac, betac, y_alpha = _holt_init(x, xi, p, y, l, b)
                if alpha == 0.0:
                    return max_seen
                if beta > alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1]) + (alphac * (l[i - 1] + phi * b[i - 1]))
                    b[i] = (beta * (l[i] - l[i - 1])) + (betac * phi * b[i - 1])
                return scipy.spatial.distance.sqeuclidean(l + phi * b, y)

            def _holt_win_init(x, xi, p, y, l, b, s, m):
                """Initialization for the Holt Winters Seasonal Models"""
                p[xi] = x
                alpha, beta, gamma, l0, b0, phi = p[:6]
                s0 = p[6:]
                alphac = 1 - alpha
                betac = 1 - beta
                gammac = 1 - gamma
                y_alpha = alpha * y
                y_gamma = gamma * y
                l[:] = 0
                b[:] = 0
                s[:] = 0
                l[0] = l0
                b[0] = b0
                s[:m] = s0
                return alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma

            def _holt_win__mul(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Multiplicative Seasonal
                Minimization Function
                (,M)
                """
                alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma = _holt_win_init(
                    x, xi, p, y, l, b, s, m)
                if alpha == 0.0:
                    return max_seen
                if gamma > 1 - alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1] / s[i - 1]) + (alphac * (l[i - 1]))
                    s[i + m - 1] = (y_gamma[i - 1] / (l[i - 1])) + (gammac * s[i - 1])
                return scipy.spatial.distance.sqeuclidean(l * s[:-(m - 1)], y)

            def _holt_win__add(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Additive Seasonal
                Minimization Function
                (,A)
                """
                alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma = _holt_win_init(
                    x, xi, p, y, l, b, s, m)
                if alpha == 0.0:
                    return max_seen
                if gamma > 1 - alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1]) - (alpha * s[i - 1]) + (alphac * (l[i - 1]))
                    s[i + m - 1] = y_gamma[i - 1] - \
                                   (gamma * (l[i - 1])) + (gammac * s[i - 1])
                return scipy.spatial.distance.sqeuclidean(l + s[:-(m - 1)], y)

            def _holt_win_add_mul_dam(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Additive and Additive Damped with Multiplicative Seasonal
                Minimization Function
                (A,M) & (Ad,M)
                """
                alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma = _holt_win_init(
                    x, xi, p, y, l, b, s, m)
                if alpha * beta == 0.0:
                    return max_seen
                if beta > alpha or gamma > 1 - alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1] / s[i - 1]) + \
                           (alphac * (l[i - 1] + phi * b[i - 1]))
                    b[i] = (beta * (l[i] - l[i - 1])) + (betac * phi * b[i - 1])
                    s[i + m - 1] = (y_gamma[i - 1] / (l[i - 1] + phi *
                                                      b[i - 1])) + (gammac * s[i - 1])
                return scipy.spatial.distance.sqeuclidean((l + phi * b) * s[:-(m - 1)], y)

            def _holt_win_mul_mul_dam(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Multiplicative and Multiplicative Damped with Multiplicative Seasonal
                Minimization Function
                (M,M) & (Md,M)
                """
                alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma = _holt_win_init(
                    x, xi, p, y, l, b, s, m)
                if alpha * beta == 0.0:
                    return max_seen
                if beta > alpha or gamma > 1 - alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1] / s[i - 1]) + \
                           (alphac * (l[i - 1] * b[i - 1] ** phi))
                    b[i] = (beta * (l[i] / l[i - 1])) + (betac * b[i - 1] ** phi)
                    s[i + m - 1] = (y_gamma[i - 1] / (l[i - 1] *
                                                      b[i - 1] ** phi)) + (gammac * s[i - 1])
                return scipy.spatial.distance.sqeuclidean((l * b ** phi) * s[:-(m - 1)], y)

            def _holt_win_add_add_dam(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Additive and Additive Damped with Additive Seasonal
                Minimization Function
                (A,A) & (Ad,A)
                """
                alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma = _holt_win_init(
                    x, xi, p, y, l, b, s, m)
                if alpha * beta == 0.0:
                    return max_seen
                if beta > alpha or gamma > 1 - alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1]) - (alpha * s[i - 1]) + \
                           (alphac * (l[i - 1] + phi * b[i - 1]))
                    b[i] = (beta * (l[i] - l[i - 1])) + (betac * phi * b[i - 1])
                    s[i + m - 1] = y_gamma[i - 1] - \
                                   (gamma * (l[i - 1] + phi * b[i - 1])) + (gammac * s[i - 1])
                return scipy.spatial.distance.sqeuclidean((l + phi * b) + s[:-(m - 1)], y)

            def _holt_win_mul_add_dam(x, xi, p, y, l, b, s, m, n, max_seen):
                """
                Multiplicative and Multiplicative Damped with Additive Seasonal
                Minimization Function
                (M,A) & (M,Ad)
                """
                alpha, beta, gamma, phi, alphac, betac, gammac, y_alpha, y_gamma = _holt_win_init(
                    x, xi, p, y, l, b, s, m)
                if alpha * beta == 0.0:
                    return max_seen
                if beta > alpha or gamma > 1 - alpha:
                    return max_seen
                for i in range(1, n):
                    l[i] = (y_alpha[i - 1]) - (alpha * s[i - 1]) + \
                           (alphac * (l[i - 1] * b[i - 1] ** phi))
                    b[i] = (beta * (l[i] / l[i - 1])) + (betac * b[i - 1] ** phi)
                    s[i + m - 1] = y_gamma[i - 1] - \
                                   (gamma * (l[i - 1] * b[i - 1] ** phi)) + (gammac * s[i - 1])
                return scipy.spatial.distance.sqeuclidean((l * phi * b) + s[:-(m - 1)], y)

            # Variable renames to alpha,beta, etc as this helps with following the
            # mathematical notation in general
            alpha = smoothing_level
            beta = smoothing_slope
            gamma = smoothing_seasonal
            phi = damping_slope

            data = ets.endog
            damped = ets.damped
            seasoning = ets.seasoning
            trending = ets.trending
            trend = ets.trend
            seasonal = ets.seasonal
            m = ets.seasonal_periods
            opt = None
            phi = phi if damped else 1.0
            if use_boxcox == 'log':
                lamda = 0.0
                y = scipy.stats.boxcox(data, lamda)
            elif isinstance(use_boxcox, float):
                lamda = use_boxcox
                y = scipy.stats.boxcox(data, lamda)
            elif use_boxcox:
                y, lamda = scipy.stats.boxcox(data)
            else:
                lamda = None
                y = data.squeeze()
            if numpy.ndim(y) != 1:
                raise NotImplementedError('Only 1 dimensional data supported')
            l = numpy.zeros((ets.nobs,))
            b = numpy.zeros((ets.nobs,))
            s = numpy.zeros((ets.nobs + m - 1,))
            p = numpy.zeros(6 + m)
            max_seen = numpy.finfo(numpy.double).max
            if seasoning:
                l0 = y[numpy.arange(ets.nobs) % m == 0].mean()
                b0 = ((y[m:m + m] - y[:m]) / m).mean() if trending else None
                s0 = list(y[:m] / l0) if seasonal == 'mul' else list(y[:m] - l0)
            elif trending:
                l0 = y[0]
                b0 = y[1] / y[0] if trend == 'mul' else y[1] - y[0]
                s0 = []
            else:
                l0 = y[0]
                b0 = None
                s0 = []
            if optimized:
                init_alpha = alpha if alpha is not None else 0.5 / max(m, 1)
                init_beta = beta if beta is not None else 0.1 * init_alpha if trending else beta
                init_gamma = None
                init_phi = phi if phi is not None else 0.99
                # Selection of functions to optimize for approporate parameters
                func_dict = {('mul', 'add'): _holt_win_add_mul_dam,
                             ('mul', 'mul'): _holt_win_mul_mul_dam,
                             ('mul', None): _holt_win__mul,
                             ('add', 'add'): _holt_win_add_add_dam,
                             ('add', 'mul'): _holt_win_mul_add_dam,
                             ('add', None): _holt_win__add,
                             (None, 'add'): _holt_add_dam,
                             (None, 'mul'): _holt_mul_dam,
                             (None, None): _holt__}
                if seasoning:
                    init_gamma = gamma if gamma is not None else 0.05 * \
                                                                 (1 - init_alpha)
                    xi = numpy.array([alpha is None, beta is None, gamma is None,
                                      True, trending, phi is None and damped] + [True] * m)
                    func = func_dict[(seasonal, trend)]
                elif trending:
                    xi = numpy.array([alpha is None, beta is None, False,
                                      True, True, phi is None and damped] + [False] * m)
                    func = func_dict[(None, trend)]
                else:
                    xi = numpy.array([alpha is None, False, False,
                                      True, False, False] + [False] * m)
                    func = func_dict[(None, None)]
                p[:] = [init_alpha, init_beta, init_gamma, l0, b0, init_phi] + s0

                # txi [alpha, beta, gamma, l0, b0, phi, s0,..,s_(m-1)]
                # Have a quick look in the region for a good starting place for alpha etc.
                # using guestimates for the levels
                txi = xi & numpy.array(
                    [True, True, True, False, False, True] + [False] * m)
                bounds = numpy.array([(0.0, 1.0), (0.0, 1.0), (0.0, 1.0),
                                      (0.0, None), (0.0, None), (0.0, 1.0)] + [(None, None), ] * m)
                res = scipy.optimize.brute(func, bounds[txi], (txi, p, y, l, b, s, m, ets.nobs, max_seen),
                                           Ns=25, full_output=True, finish=None)
                (p[txi], max_seen, grid, Jout) = res
                [alpha, beta, gamma, l0, b0, phi] = p[:6]
                s0 = p[6:]
                # bounds = np.array([(0.0,1.0),(0.0,1.0),(0.0,1.0),(0.0,None),(0.0,None),(0.8,1.0)] + [(None,None),]*m)
                if use_basinhopping:
                    # Take a deeper look in the local minimum we are in to find the best
                    # solution to parameters, maybe hop around to try escape the local
                    # minimum we may be in.
                    res = scipy.optimize.basinhopping(func, p[xi], minimizer_kwargs={'args': (
                        xi, p, y, l, b, s, m, ets.nobs, max_seen), 'bounds': bounds[xi]}, stepsize=0.01)
                else:
                    # Take a deeper look in the local minimum we are in to find the best
                    # solution to parameters
                    res = scipy.optimize.minimize(func, p[xi], args=(
                        xi, p, y, l, b, s, m, ets.nobs, max_seen), bounds=bounds[xi])
                p[xi] = res.x
                [alpha, beta, gamma, l0, b0, phi] = p[:6]
                s0 = p[6:]
                opt = res
            return ets.fit(smoothing_level=alpha, smoothing_slope=beta,
                           smoothing_seasonal=gamma, damping_slope=phi,
                           use_boxcox=use_boxcox, remove_bias=remove_bias)


        users = ['164', '841', '517', '621', '911']
        clusters = []
        paths = Paths('/home/pmateusz/dev/cordia/data/clustering')
        for user_id in users:
            cluster_ids = [cluster_part for cluster_part in os.listdir(paths.clusters_dir(user_id)) if
                           cluster_part.isdigit()]
            for cluster_id in cluster_ids:
                cluster_dir = paths.cluster_dir(user_id, cluster_id)
                if not os.path.isdir(cluster_dir):
                    continue
                visit_file = paths.visit_file(user_id, cluster_id)
                if not os.path.isfile(visit_file):
                    continue
                visit_source = VisitCSVSourceFile(visit_file)
                cluster = Cluster(int(cluster_id), items=visit_source.read(), dir_path=cluster_dir)
                if cluster.has_model():
                    clusters.append(cluster)

        errors = []
        for cluster in clusters:
            data_frame = cluster.data_frame()
            data_frame.where(
                (numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()),
                inplace=True)
            data_frame.Duration = data_frame.Duration / 60.0
            data_frame.dropna(inplace=True)
            data_frame.interpolate(method='linear', inplace=True)
            training_frame, test_frame = sklearn.model_selection.train_test_split(data_frame,
                                                                                  test_size=14,
                                                                                  shuffle=False)

            decomposition_result = statsmodels.api.tsa.seasonal_decompose(data_frame.Duration, freq=30)
            decomposition_result.plot()
            matplotlib.pyplot.show()

            ets = statsmodels.api.tsa.ExponentialSmoothing(numpy.asarray(training_frame.Duration),
                                                           damped=False,
                                                           trend=None,
                                                           seasonal='add',
                                                           seasonal_periods=7)

            if ets.nobs < 14:
                # we are predicting for 2 weeks, so any smaller value does not make sense
                # especially the number of observations cannot be 12 to avoid division by 0 in the AICC formula
                continue

            model = fit(ets)
            test_frame_to_use = test_frame.copy()
            test_frame_to_use['HoltWinters'] = model.forecast(len(test_frame))
            test_frame_to_use['Average'] = training_frame.Duration.mean()
            holt_winters_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['HoltWinters'])
            average_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['Average'])
            print('{0:10.4f} {1:10.4f} {2}'.format(holt_winters_error,
                                                   average_error,
                                                   '+' if holt_winters_error < average_error else '-'))
            errors.append((holt_winters_error, average_error))
        errors_df = pandas.DataFrame(data=errors, columns=['HoltWinters', 'Average'])
        print('Total score: {0:10.4f} {1:10.4f} {2}'.format(errors_df.HoltWinters.mean(),
                                                            errors_df.Average.mean(),
                                                            '+' if errors_df.HoltWinters.mean() < errors_df.Average.mean() else '-'))

    # data_frame = pandas.read_pickle('/home/pmateusz/dev/cordia/forecast_errors.pickle')
    #
    # fill_factors = list(set(data_frame.FillFactor))
    # fill_factors.sort()
    #
    # days = list(set(data_frame.Days))
    # days.sort()
    #
    # data_set = []
    # combinations = [(fill_factor, day) for fill_factor in fill_factors for day in days]
    # with tqdm.tqdm(combinations, total=len(combinations)) as t:
    #     for fill_factor, day in t:
    #         data_frame_query = data_frame.where((data_frame.FillFactor >= fill_factor))
    #         average_error = data_frame_query.AverageError.mean()
    #         holt_winters_error = data_frame_query.HoltWintersError.mean()
    #         data_set.append((fill_factor, day, average_error, holt_winters_error))
    # error_data_frame = pandas.DataFrame(data=data_set,
    #                                     columns=['FillFactor', 'Days', 'HoltWintersMeanError', 'AverageMeanError'])
    #
    # fig = matplotlib.pyplot.figure()
    # ax = Axes3D(fig)
    # surf = ax.scatter(error_data_frame.FillFactor,
    #                   error_data_frame.Days,
    #                   error_data_frame.HoltWintersMeanError)
    # matplotlib.pyplot.show()

    # data_frame = data_frame \
    #     .where((numpy.abs(data_frame.HoltWintersError - data_frame.HoltWintersError.mean()) <= 3 * data_frame.HoltWintersError.std()))
    # data_frame = data_frame \
    #     .where((numpy.abs(data_frame.AverageError - data_frame.AverageError.mean()) <= 3 * data_frame.AverageError.std()))
