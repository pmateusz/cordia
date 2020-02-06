import copy
import collections
import datetime
import json
import operator

import tqdm

import rows.model.datetime
import rows.model.historical_visit
import rows.model.schedule


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


class History:
    def __init__(self, visits):
        self.__visits = visits

    def build_sample(self, schedule: rows.model.schedule.Schedule, window_time_span: datetime.timedelta) -> Sample:

        visit_index = collections.defaultdict(list)

        for visit in self.__visits:
            visit_index[visit.service_user].append(visit)

        for service_user in visit_index:
            visit_index[service_user].sort(key=operator.attrgetter('planned_check_in'))

        matched_visits = {}
        for visit in tqdm.tqdm(schedule.visits, desc='Matching visits'):
            if visit.visit.service_user not in visit_index:
                continue

            time_before = visit.check_in - window_time_span
            time_after = visit.check_in + window_time_span

            matches = {}

            if visit.visit.key == 8533569:
                print('here')

            for indexed_visit in visit_index[visit.visit.service_user]:
                if indexed_visit.tasks != visit.visit.tasks:
                    continue

                if time_before.time() <= indexed_visit.planned_check_in.time() <= time_after.time():
                    if indexed_visit.planned_check_in.date() in matches:
                        local_copy = copy.copy(matches[indexed_visit.planned_check_in.date()])
                        local_copy.real_duration \
                            = datetime.timedelta(seconds=(local_copy.real_duration + indexed_visit.real_duration).total_seconds() // 2)
                        matches[indexed_visit.planned_check_in.date()] = local_copy
                    else:
                        matches[indexed_visit.planned_check_in.date()] = indexed_visit

            matched_visits[visit.visit.key] = matches

        min_date = datetime.date.max
        max_date = datetime.date.min
        for visit_key in matched_visits:
            for date in matched_visits[visit_key]:
                min_date = min(min_date, date)
                max_date = max(max_date, date)
        max_date_to_use = min(max_date, schedule.date())

        min_date_time = datetime.datetime.combine(min_date, datetime.time())
        max_date_time = datetime.datetime.combine(max_date_to_use, datetime.time())
        step = datetime.timedelta(days=1)

        samples = {}
        for visit in schedule.visits:
            past_visit_duration = []

            current_date_time = min_date_time
            while current_date_time < max_date_time:
                visit_duration = visit.duration
                if current_date_time.date() in matched_visits[visit.visit.key]:
                    visit_duration = matched_visits[visit.visit.key][current_date_time.date()].real_duration
                past_visit_duration.append(visit_duration)
                current_date_time += step

            samples[visit.visit.key] = past_visit_duration

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
