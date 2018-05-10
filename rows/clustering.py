#!/usr/bin/env python3

import csv
import collections
import datetime
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

    no_clusters = len(set(labels)) - (1 if -1 in labels else 0)
    print(no_clusters)

    fig, ax = matplotlib.pyplot.subplots()

    ax.grid(True)

    unfilled_markers = (',', '.', '+', 'x', ',', '2')

    group_handles = []
    group_number = 0

    matplotlib.pyplot.set_cmap('Paired')

    color_map = matplotlib.cm.get_cmap('Paired')
    color_it = iter(color_map.colors)
    shapes = ['s']
    shape_it = iter(shapes)
    task_shapes = {}
    label_colors = {}

    task_label_visit_groups = collections.defaultdict(lambda: collections.defaultdict(list))
    for visit, label in zip(visits_to_use, labels):
        task_label_visit_groups[visit.tasks][label].append(visit)

    for task in task_label_visit_groups:
        for label in task_label_visit_groups[task]:
            group = task_label_visit_groups[task][label]
            fill_color = 'w'
            edge_color = 'black'
            shape = 's'
            edge_width = 0.5
            size = 8

            group_elem = group[0]
            if label != -1:
                edge_width = 0

                if group_elem.tasks in task_shapes:
                    shape = task_shapes[group_elem.tasks]
                else:
                    shape = next(shape_it)
                    task_shapes[group_elem.tasks] = shape

                if label in label_colors:
                    fill_color = label_colors[label]
                else:
                    fill_color = next(color_it)
                    next(color_it)
                    label_colors[label] = fill_color

            group_start_day = []
            group_start_time = []
            for visit in group:
                group_start_day.append(clear_time(visit.original_start))
                group_start_time.append(clear_date(visit.original_start))
            task_label_handle = ax.scatter(group_start_day,
                                           group_start_time,
                                           c=fill_color,
                                           marker='s',
                                           linewidth=edge_width,
                                           edgecolor=edge_color,
                                           alpha=0.7,
                                           s=size)
            group_handles.append(('{0} {1}'.format(str(task), label), task_label_handle))

    handles = [handle for _, handle in group_handles]
    names = [name for name, _ in group_handles]
    fig.legend(handles, names)

    ax.yaxis.set_major_formatter(matplotlib.dates.DateFormatter('%H:%M'))
    ax.set_ylim(__ZERO_DATE, __ZERO_DATE + datetime.timedelta(days=1))
    matplotlib.pyplot.xticks(rotation=-60)

    fig.tight_layout()
    matplotlib.pyplot.show()
