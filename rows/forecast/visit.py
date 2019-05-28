import itertools
import operator
import re


class Tasks:
    def __init__(self, value):
        if isinstance(value, str):
            raw_tasks = re.split('[;\-]', value)
            tasks = list(map(int, raw_tasks))
            tasks.sort()
            self.__tasks = tasks
        else:
            raise ValueError(value)

    def __eq__(self, other):
        if not isinstance(Tasks, other):
            return False
        return self.__tasks == other.__tasks

    def __hash__(self):
        return hash(self.__tasks)

    def __getitem__(self, item):
        return self.__tasks.__getitem__(item)

    def __str__(self):
        return self.__tasks.__str__()

    def __repr__(self):
        return self.__tasks.__str__()


class Visit:
    def __init__(self,
                 visit_id,
                 client_id,
                 tasks,
                 area,
                 carer_id,
                 planned_start,
                 planned_end,
                 planned_duration,
                 real_start,
                 real_end,
                 real_duration,
                 check_in_processed):
        self.__visit_id = visit_id
        self.__client_id = client_id
        self.__tasks = tasks
        self.__area = area
        self.__carer_id = carer_id
        self.__planned_start = planned_start
        self.__planned_end = planned_end
        self.__planned_duration = planned_duration
        self.__real_start = real_start
        self.__real_end = real_end
        self.__real_duration = real_duration
        self.__check_in_processed = check_in_processed

    def to_list(self):
        return [self.visit_id,
                self.client_id,
                self.tasks,
                self.area,
                self.carer_id,
                self.planned_start,
                self.planned_end,
                self.planned_duration,
                self.real_start,
                self.real_end,
                self.real_duration,
                self.check_in_processed]

    @property
    def visit_id(self):
        return self.__visit_id

    @property
    def client_id(self):
        return self.__client_id

    @property
    def tasks(self):
        return self.__tasks

    @property
    def area(self):
        return self.__area

    @property
    def carer_id(self):
        return self.__carer_id

    @property
    def planned_start(self):
        return self.__planned_start

    @property
    def planned_end(self):
        return self.__planned_end

    @property
    def planned_duration(self):
        return self.__planned_duration

    @property
    def real_start(self):
        return self.__real_start

    @property
    def real_end(self):
        return self.__real_end

    @property
    def real_duration(self):
        return self.__real_duration

    @property
    def check_in_processed(self):
        return self.__check_in_processed

    @staticmethod
    def columns():
        return ['visit_id',
                'client_id',
                'tasks',
                'area',
                'carer',
                'planned_start_time',
                'planned_end_time',
                'planned_duration',
                'check_in',
                'check_out',
                'real_duration',
                'check_in_processed']

    @staticmethod
    def load_from_tuples(tuples):
        visits = []
        for row in tuples:
            visits.append(Visit(row.visit_id,
                                row.client_id,
                                row.tasks,
                                row.area,
                                row.carer,
                                row.planned_start_time,
                                row.planned_end_time,
                                row.planned_duration,
                                row.check_in,
                                row.check_out,
                                row.real_duration,
                                row.check_in_processed))
        return visits


def filter_incorrect_visits(visits):
    def __is_valid(visit):
        real_duration = visit.real_duration.total_seconds()
        return real_duration > 0 and visit.real_duration != visit.planned_duration

    visits_to_use = list(filter(__is_valid, visits))
    visits_to_use.sort(key=operator.attrgetter('visit_id'))
    visit_batches = {visit_id: list(visit_batch)
                     for visit_id, visit_batch in itertools.groupby(visits_to_use, operator.attrgetter('visit_id'))}

    results = []
    for visit_id, visit_batch in visit_batches.items():
        if len(visit_batch) == 1:
            results.append(visit_batch[0])
        elif len(visit_batch) == 2:
            visit_delta_pairs = [(visit,
                                  abs(visit.planned_duration - visit.real_duration) / visit.planned_duration)
                                 for visit in visit_batch]
            winner_visit = min(visit_delta_pairs, key=operator.itemgetter(1))[0]
            results.append(winner_visit)
        else:
            raise ValueError('Visit id: {0}'.format(visit_id))
    return results
