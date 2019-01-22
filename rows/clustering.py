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

import statsmodels.graphics.tsaplots
import statsmodels.api
import statsmodels.stats.diagnostic
import statsmodels.tsa.holtwinters
import statsmodels.tsa.stattools
import statsmodels.tsa.arima_model
import statsmodels.stats
import statsmodels.stats.stattools

from rows.analysis import VisitCSVSourceFile, time_to_seconds


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

    @property
    def empty(self):
        return not bool(self.__items)

    def data_frame(self):
        visits_to_use = [visit for visit in self.__items if visit.checkout_method == 1 or visit.checkout_method == 2]

        data_frame = pandas.DataFrame(
            index=pandas.DatetimeIndex(data=[clear_time(visit.original_start)
                                             for visit in visits_to_use]),
            data=[(time_to_seconds(visit.real_start.time()), visit.real_duration.total_seconds())
                  for visit in visits_to_use],
            columns=['Start', 'Duration'])
        data_frame.sort_index(inplace=True)

        if len(data_frame) > 1:
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

    def __eq__(self, other):
        if isinstance(other, Cluster):
            return self.items == other.items
        return False

    def __hash__(self):
        return (str(self.user) + str(self.label)).__hash__()

    def __len__(self):
        return self.__items.__len__()


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
        try:
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

            n_cluster_candidates = [count for count, freq in visit_frequency_counter.items() if
                                    freq >= MIN_CLUSTER_SIZE]
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

            distinct_labels = set(labels)
            if -1 in distinct_labels:
                distinct_labels.remove(-1)
            distinct_labels = list(distinct_labels)
            distinct_labels.sort()

            label_index = dict(zip(distinct_labels, range(len(distinct_labels))))
            clusters = [Cluster(label) for label in range(len(distinct_labels))]
            for visit, label in zip(visit_group, labels):
                if label == -1:
                    continue
                clusters[label_index[label]].add(visit)
            return clusters
        except BaseException:
            logging.exception('Unhandled exception while computing clusters')
            raise

    results = []
    groups = [(user_id, list(group)) for user_id, group in itertools.groupby(visits, lambda v: v.user)]
    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures_list = [executor.submit(compute_user_clusters, visit_group) for user_id, visit_group in groups]
        for f in tqdm.tqdm(concurrent.futures.as_completed(futures_list),
                           total=len(futures_list),
                           unit='users',
                           desc='Clustering', leave=False):
            partial_result = f.result()
            if partial_result:
                results.append(partial_result)
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


def parallel_map(function, iterable, max_workers=None):
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures_list = [executor.submit(function, work_item) for work_item in iterable]
        return [future.result() for future in futures_list]


def find_repeating_component(series):
    period_errors = []
    for period in range(1, 8, 1):
        errors = []
        for nth_item in range(period):
            items_to_consider = series[list(range(nth_item, len(series), period))]
            errors.append(sklearn.metrics.mean_squared_error(items_to_consider,
                                                             numpy.repeat(items_to_consider.mean(),
                                                                          len(items_to_consider))))
        period_errors.append((period, numpy.mean(errors), errors))

    best_period = min(period_errors, key=lambda item: item[1])[0]
    return [series[list(range(nth_item, len(series), best_period))].mean()
            for nth_item in range(best_period)]


def coalesce(data_frame, begin_range=None, end_range=None):
    if data_frame.empty:
        return data_frame.copy()

    data_frame_to_use = data_frame.copy().dropna()
    data_frame_to_use = data_frame_to_use.resample('D').mean()
    data_frame_to_use.interpolate(method='linear', inplace=True)

    if begin_range and data_frame_to_use.first_valid_index() > begin_range:
        step = datetime.timedelta(days=1)
        current_day = data_frame_to_use.first_valid_index() - step
        prototype = [None for _ in range(len(data_frame_to_use.columns))]
        while current_day >= begin_range:
            data_frame_to_use.loc[current_day] = prototype
            current_day -= step
        data_frame_to_use.sort_index(inplace=True)
        data_frame_to_use.fillna(method='bfill', inplace=True)

    if end_range and data_frame_to_use.last_valid_index() < end_range:
        step = datetime.timedelta(days=1)
        current_day = data_frame_to_use.last_valid_index() + step
        prototype = [None for _ in range(len(data_frame_to_use.columns))]
        while current_day <= end_range:
            data_frame_to_use.loc[current_day] = prototype
            current_day += step
        data_frame_to_use.fillna(method='ffill', inplace=True)

    if data_frame_to_use.isnull().values.any():
        raise ValueError('Null values left in the data frame')

    return data_frame_to_use


def analyze_cluster(cluster):
    data_frame = cluster.data_frame()
    training_frame = data_frame[:'2017-9-30']
    if training_frame.Duration.count() < 64:
        # we are predicting for 2 weeks, so any smaller value does not make sense
        # especially the number of observations cannot be 12 to avoid division by 0 in the AICC formula
        return None

    training_frame = coalesce(training_frame, None, datetime.datetime(2017, 10, 1)).copy()

    test_frame = data_frame['2017-10-1':'2017-10-14']
    test_frame = test_frame.dropna()
    if test_frame.empty:
        return None

    data_frame_to_use = coalesce(data_frame)

    correlation_test = statsmodels.stats.stattools.durbin_watson(data_frame_to_use.Duration)
    stationary_test = statsmodels.tsa.stattools.adfuller(data_frame_to_use.Duration)

    decomposition = statsmodels.api.tsa.seasonal_decompose(training_frame.Duration, model='additive')

    seasonal_component = find_repeating_component(decomposition.seasonal)
    seasons = int(numpy.ceil(365.0 / len(seasonal_component)))

    seasonal_component_df = pandas.DataFrame(
        index=pandas.date_range(start=datetime.datetime(2017, 1, 1), periods=7 * seasons, freq='D'),
        data=numpy.tile(seasonal_component, seasons),
        columns=['Duration'])

    def seasonal_effect(x, a, b):
        return a * numpy.asarray(x) + b

    data_frame_to_use = training_frame.copy()

    trend = coalesce(
        pandas.DataFrame(
            decomposition.trend[data_frame_to_use.first_valid_index():data_frame_to_use.last_valid_index()],
            columns=['Duration']),
        begin_range=data_frame_to_use.first_valid_index(),
        end_range=data_frame_to_use.last_valid_index())

    data_frame_to_use.Duration = data_frame_to_use.Duration - trend.Duration
    popt, pcov = scipy.optimize.curve_fit(seasonal_effect,
                                          seasonal_component_df[
                                          training_frame.first_valid_index():training_frame.last_valid_index()].Duration,
                                          data_frame_to_use.Duration)

    season_df = seasonal_component_df.copy()
    season_df.Duration = season_df.Duration * popt[0] + popt[1]
    no_season_df = data_frame_to_use.copy()
    no_season_df.Duration = no_season_df.Duration - season_df.Duration
    data_frame_to_use = coalesce(no_season_df)

    def plot_acf_pacf(series):
        fig = matplotlib.pyplot.figure(figsize=(12, 8))
        ax1 = fig.add_subplot(211)
        statsmodels.graphics.tsaplots.plot_acf(series.values.squeeze(), lags=40, ax=ax1)
        ax2 = fig.add_subplot(212)
        statsmodels.graphics.tsaplots.plot_pacf(series, lags=40, ax=ax2)
        matplotlib.pyplot.show()

    data_frame_to_use = coalesce(data_frame_to_use, end_range=datetime.datetime(2017, 9, 30))[
                        :datetime.datetime(2017, 9, 30)]

    arma_config = statsmodels.api.tsa.ARMA(data_frame_to_use.Duration, (1, 0), freq='D')
    arma_model = arma_config.fit(disp=0)

    ets_config = statsmodels.api.tsa.ExponentialSmoothing(data_frame_to_use.Duration, trend='add', freq='D')
    ets_model = ets_config.fit(optimized=True, use_boxcox=False)

    scipy.stats.normaltest(arma_model.resid)

    r, q, p = statsmodels.tsa.stattools.acf(arma_model.resid.values, qstat=True, missing='drop')
    data = numpy.c_[range(1, 41), r[1:], q, p]
    ljung_box_df = pandas.DataFrame(data, columns=['lag', "AC", "Q", "Prob(>Q)"])

    forecast_df = pandas.DataFrame(index=pandas.date_range(datetime.datetime(2017, 10, 1),
                                                           datetime.datetime(2017, 10, 14),
                                                           freq='D'))
    arma_prediction = arma_model.forecast(14)
    forecast_df['ARIMA'] = arma_prediction[0] \
                           + season_df.Duration[datetime.datetime(2017, 10, 1):datetime.datetime(2017, 10, 14)] \
                           + trend.Duration.mean()
    forecast_df['HoltWinters'] = ets_model.forecast(14) \
                                 + season_df.Duration[datetime.datetime(2017, 10, 1)
                                                      :datetime.datetime(2017, 10, 14)] \
                                 + trend.Duration.mean()
    forecast_df['MEAN'] = training_frame.Duration.mean()

    arima_error = \
        sklearn.metrics.mean_squared_error(test_frame.Duration, forecast_df.ix[test_frame.index]['ARIMA'])
    holt_winters_error = \
        sklearn.metrics.mean_squared_error(test_frame.Duration, forecast_df.ix[test_frame.index]['HoltWinters'])
    mean_error = \
        sklearn.metrics.mean_squared_error(test_frame.Duration, forecast_df.ix[test_frame.index]['MEAN'])

    def get_label(method, error):
        return '{0:15} {1:.2}'.format(method, error)

    import operator

    def get_filename(cluster, user, errors):
        errors_to_use = list(errors.items())
        errors_to_use.sort(key=operator.itemgetter(1))
        prefix = ''.join(map(lambda pair: pair[0][0], errors_to_use))
        return '{0}_c{1}u{2}.png'.format(prefix, cluster, user)

    figure = matplotlib.pyplot.figure(figsize=(12, 8))
    matplotlib.pyplot.plot(data_frame.Duration.ix['2017-1-1':])
    forecast_df.ARIMA.plot(style='r--', label=get_label('ARIMA', arima_error))
    forecast_df.HoltWinters.plot(style='g--', label=get_label('Holt-Winters', holt_winters_error))
    forecast_df.MEAN.plot(label=get_label('Mean', mean_error), style='b--')
    matplotlib.pyplot.legend()

    file_name = get_filename(cluster.label, cluster.user,
                             {'mean': mean_error,
                              'arima': arima_error,
                              'holtwinters': holt_winters_error})
    matplotlib.pyplot.savefig(os.path.join(root_dir, file_name))
    matplotlib.pyplot.close(figure)

    return cluster.label, cluster.user, mean_error, holt_winters_error, arima_error


if __name__ == '__main__':
    tqdm.tqdm.monitor_interval = 0

    # action = 'save_forecasts'
    # action = 'save_clusters'
    # action = 'compare_forecasts'
    # action = 'test_models'
    # action = 'reproduce_error'
    action = 'improve'
    # action = 'plot_error'
    # action = 'inspect'
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
        clusters = []
        paths = Paths('/home/pmateusz/dev/cordia/data/clustering')
        # user_ids = list(user_id for user_id in os.listdir(paths.users_dir())
        #                 if user_id.isdigit() and os.path.isdir(paths.user_dir(user_id)))
        user_ids = ['164', '841', '517', '621', '911']
        for user_id in user_ids:
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


        def get_min_label(one, two, three):
            minimum = min(one, two, three)
            if minimum == one:
                return '1'
            if minimum == two:
                return '2'
            return '3'


        def process_cluster(cluster):
            data_frame = cluster.data_frame()
            data_frame.where(
                (numpy.abs(data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()),
                inplace=True)
            data_frame.dropna(inplace=True)
            data_frame.Duration = data_frame.Duration

            training_frame = data_frame[:'2017-9-30']

            if training_frame.Duration.count() < 64:
                # we are predicting for 2 weeks, so any smaller value does not make sense
                # especially the number of observations cannot be 12 to avoid division by 0 in the AICC formula
                return None

            training_frame = coalesce(training_frame, None, datetime.datetime(2017, 10, 1))

            test_frame = data_frame['2017-10-1':'2017-10-14']
            if test_frame.empty:
                return None

            test_frame = coalesce(test_frame,
                                  begin_range=datetime.datetime(2017, 10, 1),
                                  end_range=datetime.datetime(2017, 10, 14))

            # data_frame = coalesce(data_frame, end_range=datetime.datetime(2017, 10, 14))

            decomposition = statsmodels.api.tsa.seasonal_decompose(training_frame.Duration, model='additive')
            seasonal_component = find_repeating_component(decomposition.seasonal)

            forecast_length = len(test_frame)
            seasons = int(numpy.ceil(365.0 / len(seasonal_component)))
            seasonal_data_frame = pandas.DataFrame(
                index=pandas.date_range(start=datetime.datetime(2017, 1, 1), periods=7 * seasons, freq='D'),
                data=numpy.tile(seasonal_component, seasons),
                columns=['SeasonalDuration'])

            ets_training_series = training_frame.Duration.copy()
            arima_training_series = training_frame.Duration.copy()
            seasonal_effect_series = decomposition.seasonal.copy()
            # forecast_day = datetime.datetime(2017, 10, 1)
            # for nth_day in range(0, forecast_length):
            #     seasonal_effect_forecast_day = seasonal_data_frame.loc[forecast_day].SeasonalDuration
            #     ets_config = statsmodels.api.tsa.ExponentialSmoothing(
            #         numpy.asarray(ets_training_series), trend='add', damped=True)
            #     ets_model = ets_config.fit(optimized=True, remove_bias=True)
            #     ets_forecast = ets_model.forecast(1)
            #     # 0 1 4
            #     arima_config = statsmodels.api.tsa.ARIMA(
            #         numpy.asarray(arima_training_series - seasonal_effect_series),
            #         # 7, 1, 2
            #         order=(0, 1, 1))
            #     arima_model = arima_config.fit(disp=False, transparams=True, trend='c', solver='bfgs')
            #     arima_forecast = arima_model.forecast(1)[0]
            #
            #     ets_forecast_to_use = ets_forecast[0]
            #     arima_forecast_to_use = arima_forecast[0] + seasonal_effect_forecast_day
            #
            #     print(ets_forecast_to_use, arima_forecast_to_use, data_frame.loc[forecast_day].Duration)
            #     ets_training_series[forecast_day] = ets_forecast[0]
            #     arima_training_series[forecast_day] = arima_forecast[0]
            #     seasonal_effect_series[forecast_day] = seasonal_effect_forecast_day
            #     forecast_day += datetime.timedelta(days=1)
            #
            # test_frame_to_use = test_frame.copy()
            # test_frame_to_use['HoltWinters'] = ets_training_series['2017-10-1':]
            # test_frame_to_use['HoltWinters'] = test_frame_to_use['HoltWinters']
            # test_frame_to_use['ARIMA'] = arima_training_series['2017-10-1':]
            # test_frame_to_use['ARIMA'] = test_frame_to_use['ARIMA'] + seasonal_data_frame.SeasonalDuration

            ets_config = statsmodels.api.tsa.ExponentialSmoothing(
                numpy.asarray(ets_training_series - seasonal_effect_series), trend='add')
            ets_model = ets_config.fit(optimized=True, use_boxcox=False, use_basinhopping=True)
            ets_forecast = ets_model.forecast(forecast_length)
            # 0 1 4
            arima_config = statsmodels.api.tsa.ARIMA(
                numpy.asarray(arima_training_series - seasonal_effect_series),
                # 7, 1, 2
                order=(7, 1, 2))
            arima_model = arima_config.fit(disp=False, transparams=True, trend='c', solver='bfgs')
            arima_forecast = arima_model.forecast(forecast_length)

            test_frame_to_use = test_frame.copy()
            test_frame_to_use['HoltWinters'] = ets_forecast
            test_frame_to_use['HoltWinters'] = test_frame_to_use['HoltWinters'] + seasonal_data_frame.SeasonalDuration
            test_frame_to_use['ARIMA'] = arima_forecast[0]
            test_frame_to_use['ARIMA'] = test_frame_to_use['ARIMA'] + seasonal_data_frame.SeasonalDuration
            test_frame_to_use['Average'] = training_frame.Duration.mean()
            holt_winters_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['HoltWinters'])
            average_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['Average'])
            arima_error = \
                sklearn.metrics.mean_squared_error(test_frame_to_use.Duration, test_frame_to_use['ARIMA'])
            print('{0:10.4f} {1:10.4f} {2:10.4f} {3} {4} {5}'.format(holt_winters_error,
                                                                     arima_error,
                                                                     average_error,
                                                                     get_min_label(holt_winters_error,
                                                                                   arima_error,
                                                                                   average_error),
                                                                     cluster.user,
                                                                     cluster.label))
            return holt_winters_error, arima_error, average_error


        def aicc(arima_model):
            m = len(arima_model.data.endog)
            k = arima_model.k_ar + arima_model.k_ma + arima_model.k_trend
            return arima_model.aic + (2 * k ** 2 + 2 * k) / (m - k - 1)


        def train_arima(clusters):
            # models = [(0, 1, 0), (2, 1, 2), (1, 1, 0), (0, 1, 1)]

            def evaluate(model):
                aiccs = []
                failures = 0
                for cluster in clusters:
                    try:
                        data_frame = cluster.data_frame()
                        data_frame.where(
                            (numpy.abs(
                                data_frame.Duration - data_frame.Duration.mean()) <= 1.96 * data_frame.Duration.std()),
                            inplace=True)
                        data_frame.dropna(inplace=True)

                        first_valid_index = data_frame.first_valid_index()
                        last_valid_index = data_frame.last_valid_index()
                        data_frame = coalesce(data_frame)
                        if (last_valid_index - first_valid_index).days < 32:
                            continue
                        data_frame = data_frame.interpolate(method='linear')
                        decomposition = statsmodels.api.tsa.seasonal_decompose(data_frame.Duration,
                                                                               model='multiplicative')
                        seasonal_component = find_repeating_component(decomposition.seasonal)
                        seasons = int(numpy.ceil(365.0 / len(seasonal_component)))
                        seasonal_data_frame = pandas.DataFrame(
                            index=pandas.date_range(start=datetime.datetime(2017, 1, 1), periods=7 * seasons, freq='D'),
                            data=numpy.tile(seasonal_component, seasons),
                            columns=['SeasonalDuration'])
                        seasonal_data_frame = seasonal_data_frame.truncate(before=data_frame.first_valid_index(),
                                                                           after=data_frame.last_valid_index())

                        arima_config = statsmodels.api.tsa.ARIMA(
                            numpy.asarray(data_frame.Duration / seasonal_data_frame.SeasonalDuration),
                            order=model)

                        nons = sum(1 for value in arima_config.endog if numpy.isnan(value))
                        if nons:
                            raise ValueError()
                        arima_model = arima_config.fit(disp=False, transparams=True, trend='c', solver='bfgs')
                        aiccs.append(aicc(arima_model))
                    except ValueError:
                        failures += 1
                        logging.exception('failure')
                return numpy.nanmean(aiccs), failures + sum(1 for value in aiccs if numpy.isnan(value))

            best_model = (0, 1, 1)
            best_aicc, best_failures = evaluate(best_model)
            while True:
                p, d, q = best_model
                q_aicc, q_failures = evaluate((p, d, q + 1))
                p_aicc, p_failures = evaluate((p + 1, d, q))

                if numpy.isnan(p_aicc):
                    if q_aicc > best_aicc:
                        break
                    else:
                        best_aicc = q_aicc
                        best_failures = q_failures
                        best_model = (p, d, q + 1)
                elif numpy.isnan(q_aicc):
                    if p_aicc > best_aicc:
                        break
                    else:
                        best_aicc = p_aicc
                        best_failures = p_failures
                        best_model = (p + 1, d, q)
                else:
                    if p_aicc > best_aicc and q_aicc > best_aicc:
                        break
                    if p_aicc > q_aicc:
                        best_model = (p, d, q + 1)
                        best_aicc = q_aicc
                        best_failures = q_failures
                    else:
                        best_model = (p + 1, d, q)
                        best_aicc = p_aicc
                        best_failures = p_failures
            print('Best model is: ', best_model, best_aicc, best_failures)


        errors = []
        # results = parallel_map(process_cluster, clusters, max_workers=os.cpu_count())
        results = [process_cluster(cluster) for cluster in clusters]
        for result in results:
            if result:
                errors.append(result)
        errors_df = pandas.DataFrame(data=errors, columns=['HoltWinters', 'ARIMA', 'Average'])
        print('Total score: {0:10.4f} {1:10.4f} {2:10.4f} {3}'.format(errors_df.HoltWinters.mean(),
                                                                      errors_df.ARIMA.mean(),
                                                                      errors_df.Average.mean(),
                                                                      get_min_label(errors_df.HoltWinters.mean(),
                                                                                    errors_df.ARIMA.mean(),
                                                                                    errors_df.Average.mean())))

    elif action == 'improve':
        error_data_set = []

        root_dir = '/home/pmateusz/dev/cordia/data/clustering'
        clusters = load_clusters(root_dir)
        items_to_process = list(itertools.chain.from_iterable(clusters))
        for cluster in tqdm.tqdm(items_to_process, total=len(items_to_process)):
            result = analyze_cluster(cluster)
            if result:
                error_data_set.append(result)

        errors_df = pandas.DataFrame(error_data_set,
                                     columns=['Cluster', 'User', 'MeanError', 'HoltWintersError', 'ARIMAError'])
        errors_df.to_pickle('forecast_errors.pickle')

    elif action == 'plot_error':
        errors_df = pandas.read_pickle('forecast_errors.pickle')
        mean_errors = list(errors_df.MeanError)
        mean_errors.sort(reverse=True)
        arima_errors = list(errors_df.ARIMAError)
        arima_errors.sort(reverse=True)

        indices = numpy.arange(len(mean_errors))
        width = 0.10
        matplotlib.pyplot.bar(indices, mean_errors, width, color='y')
        matplotlib.pyplot.bar(indices + width, arima_errors, width, color='g')
        matplotlib.pyplot.show()
    elif action == 'inspect':
        user_id = '8205'
        cluster_id = '0'
        root_dir = '/home/pmateusz/dev/cordia/data/clustering'
        paths = Paths(root_dir)
        visit_source = VisitCSVSourceFile(paths.visit_file(user_id, cluster_id))
        cluster = Cluster(int(cluster_id), items=visit_source.read(), dir_path=paths.cluster_dir(user_id, cluster_id))
        paths.cluster_dir('865', '0')
        data_frame = cluster.data_frame()
        training_frame = data_frame[:'2017-9-30']
        if training_frame.Duration.count() < 64:
            # we are predicting for 2 weeks, so any smaller value does not make sense
            # especially the number of observations cannot be 12 to avoid division by 0 in the AICC formula
            raise ValueError()

        training_frame = coalesce(training_frame, None, datetime.datetime(2017, 10, 1)).copy()

        test_frame = data_frame['2017-10-1':'2017-10-14']
        test_frame = test_frame.dropna()
        if test_frame.empty:
            raise ValueError()

        data_frame_to_use = coalesce(data_frame)

        correlation_test = statsmodels.stats.stattools.durbin_watson(data_frame_to_use.Duration)
        stationary_test = statsmodels.tsa.stattools.adfuller(data_frame_to_use.Duration)

        decomposition = statsmodels.api.tsa.seasonal_decompose(training_frame.Duration, model='additive')

        decomposition.plot()
        matplotlib.pyplot.show()

        seasonal_component = find_repeating_component(decomposition.seasonal)
        seasons = int(numpy.ceil(365.0 / len(seasonal_component)))

        seasonal_component_df = pandas.DataFrame(
            index=pandas.date_range(start=datetime.datetime(2017, 1, 1), periods=7 * seasons, freq='D'),
            data=numpy.tile(seasonal_component, seasons),
            columns=['Duration'])


        def seasonal_effect(x, a, b):
            return a * numpy.asarray(x) + b


        data_frame_to_use = training_frame.copy()

        trend = coalesce(
            pandas.DataFrame(
                decomposition.trend[data_frame_to_use.first_valid_index():data_frame_to_use.last_valid_index()],
                columns=['Duration']),
            begin_range=data_frame_to_use.first_valid_index(),
            end_range=data_frame_to_use.last_valid_index())

        data_frame_to_use.Duration = data_frame_to_use.Duration - trend.Duration
        popt, pcov = scipy.optimize.curve_fit(seasonal_effect,
                                              seasonal_component_df[
                                              training_frame.first_valid_index():training_frame.last_valid_index()].Duration,
                                              data_frame_to_use.Duration)

        season_df = seasonal_component_df.copy()
        season_df.Duration = season_df.Duration * popt[0] + popt[1]
        no_season_df = data_frame_to_use.copy()
        no_season_df.Duration = no_season_df.Duration - season_df.Duration
        data_frame_to_use = coalesce(no_season_df)


        def plot_acf_pacf(series):
            fig = matplotlib.pyplot.figure(figsize=(12, 8))
            ax1 = fig.add_subplot(211)
            statsmodels.graphics.tsaplots.plot_acf(series.values.squeeze(), lags=40, ax=ax1)
            ax2 = fig.add_subplot(212)
            statsmodels.graphics.tsaplots.plot_pacf(series, lags=40, ax=ax2)
            matplotlib.pyplot.show()


        data_frame_to_use = coalesce(data_frame_to_use, end_range=datetime.datetime(2017, 9, 30))[
                            :datetime.datetime(2017, 9, 30)]

        arma_config = statsmodels.api.tsa.ARMA(data_frame_to_use.Duration, (1, 0), freq='D')
        arma_model = arma_config.fit(disp=0)

        ets_config = statsmodels.api.tsa.ExponentialSmoothing(data_frame_to_use.Duration, trend='add', freq='D')
        ets_model = ets_config.fit(optimized=True, use_boxcox=False)

        scipy.stats.normaltest(arma_model.resid)

        r, q, p = statsmodels.tsa.stattools.acf(arma_model.resid.values, qstat=True, missing='drop')
        data = numpy.c_[range(1, 41), r[1:], q, p]
        ljung_box_df = pandas.DataFrame(data, columns=['lag', "AC", "Q", "Prob(>Q)"])

        forecast_df = pandas.DataFrame(index=pandas.date_range(datetime.datetime(2017, 10, 1),
                                                               datetime.datetime(2017, 10, 14),
                                                               freq='D'))
        arma_prediction = arma_model.forecast(14)
        forecast_df['ARIMA'] = arma_prediction[0] \
                               + season_df.Duration[datetime.datetime(2017, 10, 1):datetime.datetime(2017, 10, 14)] \
                               + trend.Duration.mean()
        forecast_df['HoltWinters'] = ets_model.forecast(14) \
                                     + season_df.Duration[datetime.datetime(2017, 10, 1)
                                                          :datetime.datetime(2017, 10, 14)] \
                                     + trend.Duration.mean()
        forecast_df['MEAN'] = training_frame.Duration.mean()

        arima_error = \
            sklearn.metrics.mean_squared_error(test_frame.Duration, forecast_df.ix[test_frame.index]['ARIMA'])
        holt_winters_error = \
            sklearn.metrics.mean_squared_error(test_frame.Duration, forecast_df.ix[test_frame.index]['HoltWinters'])
        mean_error = \
            sklearn.metrics.mean_squared_error(test_frame.Duration, forecast_df.ix[test_frame.index]['MEAN'])


        def get_label(method, error):
            return '{0:15} {1:.2}'.format(method, error)


        import operator


        def get_filename(cluster, user, errors):
            errors_to_use = list(errors.items())
            errors_to_use.sort(key=operator.itemgetter(1))
            prefix = ''.join(map(lambda pair: pair[0][0], errors_to_use))
            return '{0}_c{1}u{2}.png'.format(prefix, cluster, user)


        figure = matplotlib.pyplot.figure(figsize=(12, 8))
        matplotlib.pyplot.plot(data_frame.Duration.ix['2017-1-1':])
        forecast_df.ARIMA.plot(style='r--', label=get_label('ARIMA', arima_error))
        forecast_df.HoltWinters.plot(style='g--', label=get_label('Holt-Winters', holt_winters_error))
        forecast_df.MEAN.plot(label=get_label('Mean', mean_error), style='b--')
        matplotlib.pyplot.legend()
        matplotlib.pyplot.show()

        file_name = get_filename(cluster.label, cluster.user,
                                 {'mean': mean_error,
                                  'arima': arima_error,
                                  'holtwinters': holt_winters_error})
        matplotlib.pyplot.savefig(os.path.join(root_dir, file_name))
        matplotlib.pyplot.close(figure)
