#!/usr/bin/env python3

import abc
import collections
import csv
import datetime
import logging
import functools
import math
import re
import tqdm
import statistics

__DATE_TIME_MATCHER = re.compile(
    '^(?P<year>\d+)-(?P<month>\d+)-(?P<day>\d+)\s+(?P<hour>\d+):(?P<minute>\d+):(?P<second>\d+)(?:\.\d+)?$')

__TIME_DELTA_MATCHER = re.compile('^(:?(?P<day>-?\d+)\s+\w+,\s+)?'
                                  '(?P<hour>\d+):(?P<minute>\d+):(?P<second>\d+)(?:\.\d+)?$')


# def str_to_tasks(text):
#     if text:
#         split_char = next((char for char in text if not char.isdigit()), None)
#         if split_char:
#             tasks = [int(raw_task) for raw_task in text.split(split_char)]
#         else:
#             tasks = [int(text)]
#         return Tasks(tasks)
#     return Tasks([])


def str_to_date_time(text):
    match = __DATE_TIME_MATCHER.match(text)
    if not match:
        raise ValueError("'{0}' does not is not recognized as date time."
                         " Pass a value compliant with the pattern '%Y-%m-%d %H:%M:%S.%f'".format(text))
    return datetime.datetime(int(match.group('year')), int(match.group('month')), int(match.group('day')),
                             int(match.group('hour')), int(match.group('minute')), int(match.group('second')), 0)


def str_to_time_delta(text):
    match = __TIME_DELTA_MATCHER.match(text)
    if not match:
        raise ValueError("'{0}' does not is not recognized as time delta."
                         " Pass a value compliant with the pattern '%H:%M:%S.%f'".format(text))

    raw_days = match.group('day')
    day = int(raw_days) if raw_days else 0
    return datetime.timedelta(days=day,
                              hours=int(match.group('hour')),
                              minutes=int(match.group('minute')),
                              seconds=int(match.group('second')))


def time_to_seconds(value):
    delta = datetime.timedelta(hours=value.hour, minutes=value.minute, seconds=value.second)
    return int(delta.total_seconds())


class Tasks:

    def __init__(self, tasks):
        self.__tasks = set(tasks)
        self.__raw_tasks = self.__to_str(tasks)

    def __eq__(self, other):
        return isinstance(other, Tasks) and self.__raw_tasks == other.__raw_tasks

    def __hash__(self):
        return self.__raw_tasks.__hash__()

    def __str__(self):
        return self.__raw_tasks

    def issubset(self, other):
        return self.__tasks.issubset(other.tasks)

    @property
    def tasks(self):
        return self.__tasks

    @staticmethod
    def __to_str(tasks):
        tasks_to_use = list(tasks)
        tasks_to_use.sort()
        return '-'.join((str(t) for t in tasks_to_use))


class Visit(abc.ABC):

    def __init__(self):
        super().__init__()

    @property
    @abc.abstractmethod
    def id(self):
        pass

    @property
    @abc.abstractmethod
    def user(self):
        pass

    @property
    @abc.abstractmethod
    def carer(self):
        pass

    @property
    @abc.abstractmethod
    def area(self):
        pass

    @property
    @abc.abstractmethod
    def tasks(self):
        pass

    @property
    @abc.abstractmethod
    def planned_start(self):
        pass

    @property
    def planned_start_ord(self):
        return time_to_seconds(self.planned_start.time())

    @property
    @abc.abstractmethod
    def planned_duration(self):
        pass

    @property
    def planned_duration_ord(self):
        return int(self.planned_duration.total_seconds())

    @property
    @abc.abstractmethod
    def original_start(self):
        pass

    @property
    def original_start_ord(self):
        return time_to_seconds(self.original_start.time())

    @property
    @abc.abstractmethod
    def original_duration(self):
        pass

    @property
    def original_duration_ord(self):
        return int(self.original_duration.total_seconds())

    @property
    @abc.abstractmethod
    def real_start(self):
        pass

    @property
    def real_start_ord(self):
        return time_to_seconds(self.real_start.time())

    @property
    @abc.abstractmethod
    def real_duration(self):
        pass

    @property
    def real_duration_ord(self):
        return int(self.real_duration.total_seconds())

    @property
    @abc.abstractmethod
    def carer_count(self):
        pass

    @property
    @abc.abstractmethod
    def checkout_method(self):
        pass


class SimpleVisit(Visit):

    def __init__(self, **kwargs):
        super().__init__()
        self.__id = kwargs.get('id', None)
        self.__user = kwargs.get('user', None)
        self.__carer = kwargs.get('carer', None)
        self.__area = kwargs.get('area', None)
        self.__tasks = kwargs.get('tasks', None)
        self.__planned_start = kwargs.get('planned_start', None)
        self.__planned_duration = kwargs.get('planned_duration', None)
        self.__original_start = kwargs.get('original_start', None)
        self.__original_duration = kwargs.get('original_duration', None)
        self.__real_start = kwargs.get('real_start', None)
        self.__real_duration = kwargs.get('real_duration', None)
        self.__checkout_method = kwargs.get('checkout_method', None)

    @property
    def carer(self):
        return self.__carer

    @property
    def carer_count(self):
        return 1

    @property
    def id(self):
        return self.__id

    @property
    def user(self):
        return self.__user

    @property
    def area(self):
        return self.__area

    @property
    def tasks(self) -> Tasks:
        return self.__tasks

    @property
    def planned_start(self):
        return self.__planned_start

    @property
    def planned_duration(self):
        return self.__planned_duration

    @property
    def original_start(self):
        return self.__original_start

    @property
    def original_duration(self):
        return self.__original_duration

    @property
    def real_start(self):
        return self.__real_start

    @property
    def real_duration(self):
        return self.__real_duration

    @property
    def checkout_method(self):
        return self.__checkout_method


class CompositeVisit(Visit):

    def __init__(self, *visits):
        if not visits:
            raise ValueError('visit do not have any element')

        visit_it = iter(visits)
        visit = next(visit_it)
        for other_visit in visit_it:
            if not CompositeVisit.__is_equal(visit, other_visit):
                raise ValueError('visits do not correspond to the same scheduling event')

        super().__init__()
        self.__visits = list(visits)

    @property
    def id(self):
        return self.__visits[0].id

    @property
    def user(self):
        return self.__visits[0].user

    @property
    def area(self):
        return self.__visits[0].area

    @property
    def tasks(self):
        return self.__visits[0].tasks

    @property
    def carer(self):
        return self.__visits[0].carer

    @property
    def carer_count(self):
        return sum(v.carer_count for v in self.__visits)

    @property
    def planned_start(self):
        return self.__visits[0].planned_start

    @property
    def planned_duration(self):
        return self.__visits[0].planned_duration

    @property
    def original_start(self):
        return self.__visits[0].original_start

    @property
    def original_duration(self):
        return self.__visits[0].original_duration

    @property
    def real_start(self):
        visits = self.__admissible_visits()
        timestamp = int(math.ceil(statistics.mean(v.real_start.timestamp() for v in visits)))
        return datetime.datetime.fromtimestamp(timestamp)

    @property
    def real_duration(self):
        visits = self.__admissible_visits()
        return max((visit.real_duration for visit in visits))

    @property
    def checkout_method(self):
        visits = self.__admissible_visits()
        return min((visit.checkout_method for visit in visits))

    @staticmethod
    def __is_equal(left, right):
        return left.id == right.id \
               and left.user == right.user \
               and left.area == right.area \
               and left.tasks == right.tasks \
               and left.planned_start.date() == right.planned_start.date() \
               and left.original_start.date() == right.original_start.date()

    def __admissible_visits(self):
        visits = [visit for visit in self.__visits if visit.checkout_method == 1 or visit.checkout_method == 2]
        if visits:
            return visits
        return self.__visits


class VisitCSVSourceFile:
    COLUMNS = ['VisitId',
               'UserId',
               'AreaId',
               'Tasks',
               'StartDate',
               'OriginalStart',
               'OriginalStartOrd',
               'OriginalDuration',
               'OriginalDurationOrd',
               'PlannedStart',
               'PlannedStartOrd',
               'PlannedDuration',
               'PlannedDurationOrd',
               'RealStart',
               'RealStartOrd',
               'RealDuration',
               'RealDurationOrd',
               'CarerCount',
               'CheckoutMethod']

    def __init__(self, file_path):
        self.__file_path = file_path

    def write(self, visit_it):
        with open(self.__file_path, 'w') as output_stream:
            writer = csv.writer(output_stream, dialect=csv.get_dialect('unix'))
            writer.writerow(self.COLUMNS)
            for visit in visit_it:
                writer.writerow([visit.id,
                                 visit.user,
                                 visit.area,
                                 visit.tasks,
                                 visit.original_start.date(),
                                 visit.original_start,
                                 visit.original_start_ord,
                                 visit.original_duration,
                                 visit.original_duration_ord,
                                 visit.planned_start,
                                 visit.planned_start_ord,
                                 visit.planned_duration,
                                 visit.planned_duration_ord,
                                 visit.real_start,
                                 visit.real_start_ord,
                                 visit.real_duration,
                                 visit.real_duration_ord,
                                 visit.carer_count,
                                 visit.checkout_method])
            output_stream.flush()

    def read(self):
        try:
            with open(self.__file_path, 'r') as input_stream:
                reader = csv.reader(input_stream, dialect=csv.get_dialect('unix'))
                header = next(reader, None)
                if not header:
                    return []
                if header != self.COLUMNS:
                    raise ValueError('Unexpected header: {0}'.format(', '.join(header)))

                visits = []
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
                    tasks = str_to_tasks(raw_tasks)
                    original_start = str_to_date_time(raw_original_start)
                    original_duration = str_to_time_delta(raw_original_duration)
                    planned_start = str_to_date_time(raw_planned_start)
                    planned_duration = str_to_time_delta(raw_planned_duration)
                    real_start = str_to_date_time(raw_real_start)
                    real_duration = str_to_time_delta(raw_real_duration)
                    carer_count = int(raw_carer_count)
                    checkout_method = int(raw_checkout_method)

                    visits.append(SimpleVisit(id=visit_id,
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
                return visits
        except ValueError:
            logging.exception('Failed to save file: %s', self.__file_path)


if __name__ == '__main__':

    def load_data_csv():
        # './data/visits_old_2017_anonymized.csv'
        # './data/part_anonymized.csv'
        file_path = './data/visits_old_2017_anonymized.csv'
        with open(file_path, 'r') as input_stream:
            sniffer = csv.Sniffer()
            dialect = sniffer.sniff(input_stream.read(4096))
            input_stream.seek(0)
            with tqdm.tqdm(desc='Loading CSV data...',
                           leave=False,
                           unit='records',
                           iterable=csv.reader(input_stream, dialect=dialect)) as progress_stream:
                stream_it = iter(progress_stream)
                next(stream_it)
                return list(stream_it)


    def row_to_visit(row):
        visit_raw_id, \
        user_raw_id, \
        planned_carer_id, \
        planned_start_raw_date_time, \
        planned_end_raw_date_time, \
        planned_duration, \
        original_start_raw_date_time, \
        original_end_raw_date_time, \
        original_duration, \
        check_in_date_raw_time, \
        check_out_date_raw_time, \
        real_duration, \
        check_out_raw_method, \
        raw_tasks, \
        area = row
        tasks = str_to_tasks(raw_tasks)
        planned_start_date_time = str_to_date_time(planned_start_raw_date_time)
        planned_end_date_time = str_to_date_time(planned_end_raw_date_time)
        original_start_date_time = str_to_date_time(original_start_raw_date_time)
        original_end_date_time = str_to_date_time(original_end_raw_date_time)
        real_start_date_time = str_to_date_time(check_in_date_raw_time)
        real_end_date_time = str_to_date_time(check_out_date_raw_time)
        return SimpleVisit(id=int(visit_raw_id),
                           user=int(user_raw_id),
                           area=int(area),
                           carer=int(planned_carer_id),
                           tasks=tasks,
                           planned_start=planned_start_date_time,
                           planned_duration=(planned_end_date_time - planned_start_date_time),
                           original_start=original_start_date_time,
                           original_duration=(original_end_date_time - original_start_date_time),
                           real_start=real_start_date_time,
                           real_duration=(real_end_date_time - real_start_date_time),
                           checkout_method=int(check_out_raw_method))


    data_ = load_data_csv()
    visits_ = collections.defaultdict(list)
    with tqdm.tqdm(total=len(data_),
                   desc='Parsing records...',
                   unit='records',
                   unit_divisor=1000,
                   leave=False,
                   unit_scale=True) as t:
        for row_ in data_:
            visit_ = row_to_visit(row_)
            if visit_.id is None \
                    or visit_.user is None \
                    or visit_.area is None \
                    or not visit_.tasks \
                    or not visit_.planned_start \
                    or not visit_.original_start \
                    or not visit_.real_start:
                raise ValueError(visit_)
            if visit_.original_duration.days < 0 \
                    or visit_.planned_duration.days < 0:
                raise ValueError(visit_)
            visits_[visit_.id].append(visit_)
            t.update(1)
    del data_

    with tqdm.tqdm(total=len(visits_),
                   desc='Aggregating records...',
                   unit='records',
                   unit_divisor=1000,
                   leave=False,
                   unit_scale=True) as t:
        visits_to_use_ = []
        for visit_group_ in visits_.values():
            len_group_ = len(visit_group_)
            if len_group_ == 1:
                visits_to_use_.append(visit_group_[0])
            elif len_group_ == 2:
                visits_to_use_.append(CompositeVisit(*visit_group_))
            elif len_group_ > 2:
                visit_ = visit_group_[0]
                carer_count = sum(v.carer_count for v in visit_group_)
                carer_ids = ', '.join((str(v.carer) for v in visit_group_))
                logging.warning('Visits %d is serviced by %d carers: %s', visit_.id, carer_count, carer_ids)
            t.update(1)

    with tqdm.tqdm(total=len(visits_to_use_),
                   desc='Filtering records...',
                   unit='records',
                   unit_divisor=1000,
                   leave=False,
                   unit_scale=True) as t:
        restricted_visits_ = []
        for visit in visits_to_use_:
            if visit.area < 2:
                restricted_visits_.append(visit)
            t.update(1)


    def visit_comp(left, right):
        area_cmp = left.area - right.area
        if not area_cmp:
            return area_cmp
        user_cmp = left.user - right.user
        if not user_cmp:
            return user_cmp
        date_cmp = left.original_start.toordinal() - right.original_start.toordinal()
        if not date_cmp:
            return date_cmp
        if left.original_start.time() < right.original_start.time():
            return -1
        elif left.original_start.time() == right.original_start.time():
            return 0
        else:
            return 1


    restricted_visits_.sort(key=functools.cmp_to_key(visit_comp))

    visit_file = VisitCSVSourceFile('output.csv')
    with tqdm.tqdm(restricted_visits_,
                   desc='Saving results...',
                   unit='records',
                   leave=False,
                   unit_scale=True) as t:
        visit_file.write(t)
