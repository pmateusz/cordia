import abc
import collections
import logging

import datetime

import numpy
import sklearn.metrics
import sklearn.cluster


def timedelta(value):
    if value is datetime.datetime:
        current_time = value.time()
    else:
        current_time = value
    return datetime.timedelta(hours=current_time.hour,
                              minutes=current_time.minute,
                              seconds=current_time.second,
                              microseconds=current_time.microsecond)


def start_time_diff(left, right):
    left_time = left.planned_start.hour * 3600 + left.planned_start.minute * 60
    right_time = right.planned_start.hour * 3600 + right.planned_start.minute * 60
    return abs(left_time - right_time)


def start_time_distance(left, right):
    return start_time_diff(left, right) ** 2


def duration_diff(left, right):
    return abs(left.planned_duration.total_seconds() - right.planned_duration.total_seconds()) / 60.0


def duration_distance(left, right):
    return duration_diff(left, right) ** 2


def actual_duration_diff(left, right):
    if left.actual_duration == left.planned_duration or right.actual_duration == right.planned_duration:
        return 0.0

    return abs(left.actual_duration.total_seconds() - right.actual_duration.total_seconds()) / 60.0


def actual_duration_distance(left, right):
    return actual_duration_diff(left, right) ** 2


def task_distance(left, right):
    return (len(left.tasks.difference(right.tasks)) + len(right.tasks.difference(left.tasks))) ** 2


def same_day_no_overlap_distance(left, right):
    if left.planned_start.date() != right.planned_start.date():
        return 0.0

    # visits are known to happen on the same day
    # return 1.0 if these visits do not overlap
    earliest_finish = timedelta(min(left.planned_end.time(), right.planned_end.time()))
    latest_start = timedelta(max(left.planned_start.time(), right.planned_start.time()))
    return int(earliest_finish <= latest_start)


def get_unique_tasks(visits):
    tasks = set()
    for visit in visits:
        tasks = tasks.union(visit.tasks)
    tasks = list(tasks)
    tasks.sort()
    return tasks


class DistanceMatrix(abc.ABC):
    def __init__(self):
        self.__fitted_values = None

    def fit(self, visits):
        self.__fitted_values = self.compute_fit(visits)

    @abc.abstractmethod
    def distance(self, left, right):
        raise NotImplemented()

    @abc.abstractmethod
    def compute_fit(self, values):
        raise NotImplemented()

    @property
    def fitted_values(self):
        return self.__fitted_values

    @staticmethod
    def calculate_metric(records, metric):
        length = len(records)
        out = numpy.zeros((length, length), dtype='float')
        for i in range(length):
            for j in range(i, length):
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


class VisitDistanceMatrix(DistanceMatrix):

    def __init__(self):
        super().__init__()

        self.__visit_index = None
        self.__overlap_norm_weight = 0
        self.__start_time_norm_weight = 0
        self.__duration_norm_weight = 0
        self.__task_norm_weight = 0
        self.__tasks = []

    def compute_fit(self, visits):
        self.__tasks = get_unique_tasks(visits)

        overlap_matrix = DistanceMatrix.calculate_metric(visits, same_day_no_overlap_distance)
        overlap_norm_factor = overlap_matrix.max()

        start_time_matrix = DistanceMatrix.calculate_metric(visits, start_time_distance)
        start_time_norm_factor = start_time_matrix.max()

        duration_matrix = DistanceMatrix.calculate_metric(visits, actual_duration_distance)
        duration_norm_factor = duration_matrix.max()

        task_matrix = DistanceMatrix.calculate_metric(visits, task_distance)
        task_norm_factor = task_matrix.max()

        def get_weight(normalization_factor):
            __MIN_WEIGHT = 10.0E-8
            if normalization_factor >= __MIN_WEIGHT:
                return 1000.0 / normalization_factor
            return 1000.0

        self.__visit_index = {}
        for position, visit in enumerate(visits):
            self.__visit_index[visit.client_id] = position

        self.__overlap_norm_weight = get_weight(overlap_norm_factor)
        self.__start_time_norm_weight = get_weight(start_time_norm_factor)
        self.__duration_norm_weight = get_weight(duration_norm_factor)
        self.__task_norm_weight = get_weight(task_norm_factor)
        return self.__overlap_norm_weight * overlap_matrix \
               + self.__start_time_norm_weight * start_time_matrix \
               + self.__duration_norm_weight * duration_matrix \
               + self.__task_norm_weight * task_matrix

    def distance(self, left, right):
        return self.__overlap_norm_weight * same_day_no_overlap_distance(left, right) \
               + self.__start_time_norm_weight * start_time_distance(left, right) \
               + self.__duration_norm_weight * actual_duration_distance(left, right) \
               + self.__task_norm_weight * task_distance(left, right)

    @property
    def start_time_weight(self):
        return self.__start_time_norm_weight

    @property
    def duration_weight(self):
        return self.__duration_norm_weight

    @property
    def task_weight(self):
        return self.__task_norm_weight

    @property
    def overlap_weight(self):
        return self.__overlap_norm_weight

    @property
    def tasks(self):
        return self.__tasks


class PlannedStartDistanceMatrix(DistanceMatrix):

    def compute_fit(self, records):
        return DistanceMatrix.calculate_metric(records, self.distance)

    def distance(self, left, right):
        return start_time_diff(left, right) / 60.0


class NoSameDayPlannedStartDistanceMatrix(DistanceMatrix):
    def compute_fit(self, records):
        return DistanceMatrix.calculate_metric(records, self.distance)

    def distance(self, left, right):
        value = 0.0
        value += same_day_no_overlap_distance(left, right) * 24 * 60.0
        value += start_time_diff(left, right) / 60.0
        return value


class NoSameDayPlannedStarDurationDistanceMatrix(DistanceMatrix):
    def compute_fit(self, records):
        return DistanceMatrix.calculate_metric(records, self.distance)

    def distance(self, left, right):
        value = 0.0
        value += same_day_no_overlap_distance(left, right) * 24 * 60.0
        value += start_time_diff(left, right) / 60.0
        value += max(12.0 * (duration_diff(left, right) / 60.0) - 120.0, 0.0)
        return value


class Cluster:

    def __init__(self, distance_matrix, visits):
        self.__distance_matrix = distance_matrix
        self.__visits = visits

        client_ids = {visit.client_id for visit in self.__visits}
        assert len(client_ids) == 1

        self.__client_id = next(iter(client_ids))

    def distance(self, visit):
        distances = [self.__distance_matrix.distance(sample_visit, visit) for sample_visit in self.__visits]
        return numpy.mean(distances)

    @property
    def visits(self):
        return self.__visits

    @property
    def client_id(self):
        return self.__client_id


class AgglomerativeModel:
    DISTANCE_THRESHOLD = 120.5

    def __init__(self, distance_matrix):
        self.__distance_matrix = distance_matrix

    def build_sample(self, records):
        self.__distance_matrix.fit(records)
        return self.__distance_matrix.fitted_values

    def cluster(self, records):
        if len(records) <= 1:
            return [Cluster(self.__distance_matrix, records)]

        sample = self.build_sample(records)
        clustering = sklearn.cluster.AgglomerativeClustering(affinity='precomputed',
                                                             linkage='complete',
                                                             n_clusters=None,
                                                             distance_threshold=AgglomerativeModel.DISTANCE_THRESHOLD)
        cluster_batches = collections.defaultdict(list)
        try:
            cluster_labels = clustering.fit_predict(sample)
            for record, label in zip(records, cluster_labels):
                cluster_batches[label].append(record)
            return [Cluster(self.__distance_matrix, batch) for batch in cluster_batches.values()]
        except:
            client_id = None
            if records:
                client_id = records[0].client_id
            logging.exception('Failed to compute clusters for user: %s', client_id)
            return [Cluster(self.__distance_matrix, records)]
