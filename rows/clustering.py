#!/usr/bin/env python3

import csv
import collections
import datetime
import functools
import itertools

import numpy

import rows.analysis

import numpy as np

from sklearn.cluster import DBSCAN
from sklearn import metrics
from sklearn.datasets.samples_generator import make_blobs
from sklearn.preprocessing import StandardScaler

import matplotlib.pyplot


def example():
    # #############################################################################
    # Generate sample data
    centers = [[1, 1], [-1, -1], [1, -1]]
    X, labels_true = make_blobs(n_samples=750, centers=centers, cluster_std=0.4,
                                random_state=0)

    X = StandardScaler().fit_transform(X)

    # #############################################################################
    # Compute DBSCAN
    db = DBSCAN(eps=0.3, min_samples=10).fit(X)
    core_samples_mask = np.zeros_like(db.labels_, dtype=bool)
    core_samples_mask[db.core_sample_indices_] = True
    labels = db.labels_

    # Number of clusters in labels, ignoring noise if present.
    n_clusters_ = len(set(labels)) - (1 if -1 in labels else 0)

    print('Estimated number of clusters: %d' % n_clusters_)
    print("Homogeneity: %0.3f" % metrics.homogeneity_score(labels_true, labels))
    print("Completeness: %0.3f" % metrics.completeness_score(labels_true, labels))
    print("V-measure: %0.3f" % metrics.v_measure_score(labels_true, labels))
    print("Adjusted Rand Index: %0.3f"
          % metrics.adjusted_rand_score(labels_true, labels))
    print("Adjusted Mutual Information: %0.3f"
          % metrics.adjusted_mutual_info_score(labels_true, labels))
    print("Silhouette Coefficient: %0.3f"
          % metrics.silhouette_score(X, labels))

    # #############################################################################
    # Plot result
    import matplotlib.pyplot as plt

    # Black removed and is used for noise instead.
    unique_labels = set(labels)
    colors = [plt.cm.Spectral(each)
              for each in np.linspace(0, 1, len(unique_labels))]
    for k, col in zip(unique_labels, colors):
        if k == -1:
            # Black used for noise.
            col = [0, 0, 0, 1]

        class_member_mask = (labels == k)

        xy = X[class_member_mask & core_samples_mask]
        plt.plot(xy[:, 0], xy[:, 1], 'o', markerfacecolor=tuple(col),
                 markeredgecolor='k', markersize=14)

        xy = X[class_member_mask & ~core_samples_mask]
        plt.plot(xy[:, 0], xy[:, 1], 'o', markerfacecolor=tuple(col),
                 markeredgecolor='k', markersize=6)

    plt.title('Estimated number of clusters: %d' % n_clusters_)
    plt.show()


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
            tasks = rows.analysis.str_to_tasks(raw_tasks)
            original_start = rows.analysis.str_to_date_time(raw_original_start)
            original_duration = rows.analysis.str_to_time_delta(raw_original_duration)
            planned_start = rows.analysis.str_to_date_time(raw_planned_start)
            planned_duration = rows.analysis.str_to_time_delta(raw_planned_duration)
            real_start = rows.analysis.str_to_date_time(raw_real_start)
            real_duration = rows.analysis.str_to_time_delta(raw_real_duration)
            carer_count = int(raw_carer_count)
            checkout_method = int(raw_checkout_method)

            results.append(rows.analysis.SimpleVisit(id=visit_id,
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


if __name__ == '__main__':
    visits = load_visits('/home/pmateusz/dev/cordia/output.csv')
    user_counter = collections.Counter()
    for visit in visits:
        user_counter[visit.user] += 1

    user_id = 5509
    print('Loaded users', user_counter)

    visits_to_use = [v for v in visits if v.user == user_id]
    print('Focusing on user', user_id, len(visits_to_use))

    raw_visits_to_use = np.array(visits_to_use)


    def distance(left, right):
        return abs(left.original_start_ord - right.original_start_ord)


    distances = calculate_distance(raw_visits_to_use, distance)
    db = DBSCAN(eps=0.3, min_samples=10, metric='precomputed')
    print(db)
    db.fit(distances)

    # get number of clusters
    labels = db.labels_
    print(labels)

    cluster_count = len(set(labels)) - (1 if -1 in labels else 0)
    print(cluster_count)

    fig, ax = matplotlib.pyplot.subplots()

    ax.grid(True)

    unfilled_markers = (',', '.', '+', 'x', ',', '2')

    group_handles = []
    group_number = 0

    clusters = [Cluster(label) for label in range(-1, cluster_count, 1)]

    matplotlib.pyplot.set_cmap('Paired')
    color_map = matplotlib.cm.get_cmap('Paired')
    color_it = iter(color_map.colors)
    next(color_it)
    shapes = ['s']
    shape_it = iter(shapes)
    task_shapes = {}
    label_colors = {}

    for visit, label in zip(visits_to_use, labels):
        clusters[label + 1].add(visit)

    for cluster in clusters:
        for tasks, group_it in itertools.groupby(cluster.items, lambda item: item.tasks):
            fill_color = 'w'
            edge_color = 'black'
            shape = 's'
            edge_width = 0.5
            size = 8

            group_to_use = list(group_it)
            if cluster.label != -1:
                edge_width = 0
                item = group_to_use[0]
                if item.tasks in task_shapes:
                    shape = task_shapes[item.tasks]
                else:
                    shape = next(shape_it)
                    task_shapes[item.tasks] = shape

                if cluster.label in label_colors:
                    fill_color = label_colors[cluster.label]
                else:
                    fill_color = next(color_it)
                    next(color_it)
                    label_colors[cluster.label] = fill_color

            start_days = []
            start_times = []
            for visit in group_to_use:
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


            def get_series_name(group, cluster):
                if cluster.label == -1:
                    return "{0} - outliers".format(group[0].tasks)
                centroid = cluster.centroid()
                return "{0} - {1}".format(group[0].tasks, centroid.original_start.time())


            group_handles.append((task_label_handle, get_series_name(group_to_use, cluster)))

    group_handles.sort(key=lambda item: item[1])
    fig.legend(map(lambda item: item[0], group_handles), map(lambda item: item[1], group_handles))

    ax.yaxis.set_major_formatter(matplotlib.dates.DateFormatter('%H:%M'))
    ax.set_ylim(__ZERO_DATE, __ZERO_DATE + datetime.timedelta(days=1))
    matplotlib.pyplot.xticks(rotation=-60)

    fig.tight_layout()
    matplotlib.pyplot.show()
