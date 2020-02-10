import collections
import datetime
import json
import math
import operator
import typing

import tqdm

import rows.model.datetime
import rows.model.historical_visit
import rows.model.problem


class Sample:
    def __init__(self, values: dict):
        self.__values = values

    def visit_duration(self, visit: int, scenario: int) -> datetime.timedelta:
        return self.__values[visit][scenario]

    @property
    def size(self) -> int:
        first_key = next(iter(self.__values))
        if first_key is None:
            return 0
        return len(self.__values[first_key])


def average_duration(values: typing.List[datetime.timedelta]) -> datetime.timedelta:
    total_duration = datetime.timedelta()
    for value in values:
        total_duration += value
    return datetime.timedelta(seconds=math.ceil(total_duration.total_seconds() / float(len(values))))


class History:
    def __init__(self, visits):
        self.__visits = visits

    def build_sample(self, problem: rows.model.problem.Problem, date: datetime.date, window_time_span: datetime.timedelta) -> Sample:
        problem_visits = []
        for visit_batch in problem.visits:
            for visit in visit_batch.visits:
                if visit.date == date:
                    problem_visits.append(visit)

        visit_index = collections.defaultdict(list)
        for visit in self.__visits:
            visit_index[visit.service_user].append(visit)

        for service_user in visit_index:
            visit_index[service_user].sort(key=operator.attrgetter('planned_check_in'))

        matched_visits = {}
        for visit in tqdm.tqdm(problem_visits, desc='Matching visits'):
            if visit.service_user not in visit_index:
                continue

            time_before = visit.datetime - window_time_span
            time_after = visit.datetime + window_time_span

            matches = collections.defaultdict(list)
            for indexed_visit in visit_index[visit.service_user]:
                if indexed_visit.tasks != visit.tasks:
                    continue

                if time_before.time() <= indexed_visit.planned_check_in.time() <= time_after.time():
                    matches[indexed_visit.planned_check_in.date()].append(indexed_visit.real_duration)
            matched_visits[visit.key] = matches

        dates = set()
        for visit_key in matched_visits:
            for visit_date in matched_visits[visit_key]:
                if visit_date < date:
                    dates.add(visit_date)
        dates_sorted = list(dates)
        dates_sorted.sort()

        samples = {}
        for visit in problem_visits:
            past_visit_duration = []

            for current_date in dates_sorted:
                visit_duration = visit.duration

                if current_date in matched_visits[visit.key]:
                    visit_duration = average_duration(matched_visits[visit.key][current_date])
                past_visit_duration.append(visit_duration)

            samples[visit.key] = past_visit_duration

        return Sample(samples)

    @staticmethod
    def load(input_stream) -> 'History':
        document = json.load(input_stream)

        visits = []
        for visit_json in document:
            visit = visit_json['visit']
            service_user = visit_json['service_user']
            planned_check_in = rows.model.datetime.try_parse_iso_datetime(visit_json['planned_check_in'])
            planned_check_out = rows.model.datetime.try_parse_iso_datetime(visit_json['planned_check_out'])
            planned_duration = rows.model.datetime.try_parse_duration(visit_json['planned_duration'])
            real_check_in = rows.model.datetime.try_parse_iso_datetime(visit_json['real_check_in'])
            real_check_out = rows.model.datetime.try_parse_iso_datetime(visit_json['real_check_out'])
            real_duration = rows.model.datetime.try_parse_duration(visit_json['real_duration'])

            assert planned_check_in and planned_check_out and planned_duration is not None
            assert real_check_in and real_check_out and real_duration is not None

            tasks = visit_json['tasks']
            carer_count = visit_json['carer_count']
            visits.append(rows.model.historical_visit.HistoricalVisit(visit=visit,
                                                                      service_user=service_user,
                                                                      planned_check_in=planned_check_in,
                                                                      planned_check_out=planned_check_out,
                                                                      planned_duration=planned_duration,
                                                                      real_check_in=real_check_in,
                                                                      real_check_out=real_check_out,
                                                                      real_duration=real_duration,
                                                                      tasks=tasks,
                                                                      carer_count=carer_count))

        return History(visits)
