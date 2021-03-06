"""Loads test data from CSV"""

import csv
import collections
import datetime
import logging
import itertools

import dateutil.parser

import rows.model.day
from rows.model.area import Area

from rows.model.service_user import ServiceUser
from rows.model.diary import Diary
from rows.model.address import Address
from rows.model.problem import Problem
from rows.model.carer import Carer
from rows.model.event import RelativeEvent
from rows.model.metadata import Metadata
from rows.model.schedule import Schedule
from rows.model.shift_pattern import ExecutableShiftPattern
from rows.model.visit import Visit
from rows.model.past_visit import PastVisit


class CSVDataSource:
    """Loads test data from CSV"""

    DEFAULT_AREA = Area(key=0, code='Default area')

    def __init__(self,
                 location_finder,
                 carers_file,
                 shift_patterns_file,
                 visits_file,
                 past_visits_file):
        self.__location_finder = location_finder
        self.__carers_file = carers_file
        self.__shift_patterns_file = shift_patterns_file
        self.__visits_file = visits_file
        self.__past_visits_file = past_visits_file

        self.__loaded = False
        self.__carers = {}
        self.__visits = collections.defaultdict(list)
        self.__past_visits = collections.defaultdict(list)

        # in this data set it is not possible to have carers assigned to multiple shift patterns
        self.__carer_shift_patterns = collections.OrderedDict()

    def reload(self):
        """Loads data from data sources"""

        reference_week = dateutil.parser.parse('29/01/2017').date()

        def create_sniffed_reader(local_file_stream):
            """Creates CVS reader that adapts to the format of the file"""

            dialect = csv.Sniffer().sniff(local_file_stream.read(4096))
            local_file_stream.seek(0)
            reader = csv.reader(local_file_stream, dialect)

            # skip header
            next(reader)

            return reader

        def load_carers(file_path):
            """Loads carers"""

            carers = {}
            local_shift_pattern_mapping = collections.defaultdict(list)
            with open(file_path) as file_stream:
                for row in create_sniffed_reader(file_stream):
                    if len(row) < 5:
                        logging.warning("Cannot parse carer: '%s'", row)
                        continue

                    sap_number, address, position = row[0], Address(post_code=row[1]), row[2]
                    carer = Carer(address=address, sap_number=sap_number, position=position)
                    carers[carer.sap_number] = carer

                    shift_key, shift_week = row[3], int(row[4])
                    local_shift_pattern_mapping[shift_key].append((carer, shift_week))
            return carers, local_shift_pattern_mapping

        def load_visits(file_path):
            """Loads visits"""
            visits = collections.defaultdict(list)
            with open(file_path) as file_stream:
                for row in create_sniffed_reader(file_stream):
                    if len(row) < 6:
                        logging.warning("Cannot parse visit: '%s'", row)
                        continue

                    service_user, raw_address, post_code, raw_date, raw_time, raw_duration = row
                    address = Address.parse(raw_address)
                    address.post_code = post_code.strip()
                    visits[service_user].append(Visit(service_user=service_user,
                                                      date=datetime.datetime.strptime(raw_date,
                                                                                      '%d/%m/%Y').date(),
                                                      time=datetime.datetime.strptime(raw_time,
                                                                                      '%H:%M:%S').time(),
                                                      duration=datetime.timedelta(minutes=int(raw_duration)),
                                                      address=address))
            return visits

        def load_shift_patters(file_path):
            """Loads shift patters"""

            carer_shift_patterns = collections.OrderedDict()
            with open(file_path) as file_stream:
                events = collections.defaultdict(list)
                for row in create_sniffed_reader(file_stream):
                    if len(row) < 8:
                        print(row)
                        continue

                    key, raw_week, raw_day, _raw_day, raw_begin_hour, raw_begin_minutes, raw_end_hour, raw_end_minutes \
                        = row
                    week = int(raw_week)
                    day = rows.model.day.from_short_name(raw_day)
                    begin = datetime.time(hour=int(raw_begin_hour), minute=int(raw_begin_minutes))
                    end = datetime.time(hour=int(raw_end_hour), minute=int(raw_end_minutes))
                    events[key].append(RelativeEvent(week=week, day=day, begin=begin, end=end))

                for key, events in events.items():
                    events_to_use = sorted(events)
                    for carer, shift_week in shift_pattern_carer_mapping[key]:
                        carer_shift_patterns[carer] = ExecutableShiftPattern(key=key,
                                                                             events=events_to_use,
                                                                             reference_week=reference_week,
                                                                             reference_shift_week=shift_week)
            return carer_shift_patterns

        def load_past_visits(file_path, visits, carers):  # pylint: disable=too-many-locals
            """Loads past visits"""

            with open(file_path) as file_stream:
                date_time_format = '%d/%m/%Y %H:%M'
                past_visits = collections.defaultdict(list)
                for row in create_sniffed_reader(file_stream):
                    if len(row) < 7:
                        print(row)
                        continue
                    service_user, raw_begin, raw_end, raw_check_in, raw_check_out, raw_cancelled, sap_number = row
                    local_begin = datetime.datetime.strptime(raw_begin, date_time_format)
                    local_end = datetime.datetime.strptime(raw_end, date_time_format)
                    check_in = None if raw_check_in == 'NULL' \
                        else datetime.datetime.strptime(raw_check_in, date_time_format)
                    check_out = None if raw_check_out == 'NULL' \
                        else datetime.datetime.strptime(raw_check_out, date_time_format)

                    cancelled = raw_cancelled == 'Y'
                    carer = carers.get(sap_number, None)

                    visit = next(filter(lambda local_visit: local_visit.date == local_begin.date() \
                                                            and local_visit.time == local_begin.time(),
                                        visits[service_user]), None)
                    if not visit:
                        visits_for_the_day = list(filter(lambda local_visit: local_visit.date == local_begin.date(),
                                                         visits[service_user]))
                        # if no visits have been found, try to find a nearest visit
                        if visits_for_the_day:
                            ref_time_delta = datetime.timedelta(hours=local_begin.hour, minutes=local_begin.minute)
                            visit = min(visits_for_the_day, key=lambda local_visit: abs(
                                datetime.timedelta(hours=local_visit.time.hour,
                                                   minutes=local_visit.time.minute) - ref_time_delta))
                    past_visits[sap_number].append(
                        PastVisit(cancelled=cancelled,
                                  carer=carer,
                                  visit=visit,
                                  date=local_begin.date(),
                                  time=local_begin.time(),
                                  duration=local_end - local_begin,
                                  check_in=check_in,
                                  check_out=check_out))
            return past_visits

        self.__carers, shift_pattern_carer_mapping = load_carers(self.__carers_file)
        self.__visits = load_visits(self.__visits_file)
        self.__carer_shift_patterns = load_shift_patters(self.__shift_patterns_file)
        self.__past_visits = load_past_visits(self.__past_visits_file, self.__visits, self.__carers)

        visit_attendance = collections.defaultdict(int)
        for service_user in self.__past_visits:
            for past_visit in self.__past_visits[service_user]:
                if not past_visit.cancelled:
                    visit_attendance[past_visit.visit] += 1

        for visit_group_key, carer_count in visit_attendance.items():
            if not visit_group_key or carer_count <= 1:
                continue
            for calendar_visit in self.__visits[visit_group_key.service_user]:
                if calendar_visit.date == visit_group_key.date \
                        and calendar_visit.time == visit_group_key.time \
                        and calendar_visit.duration == visit_group_key.duration:
                    calendar_visit.carer_count = carer_count

        self.__loaded = True

    def get_areas(self):
        return [CSVDataSource.DEFAULT_AREA]

    def get_carers(self, area, begin_date, end_date):
        """Return all carers"""

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        carers = []
        for carer in self.__carers.values():
            if not self.__carer_shift_patterns[carer].is_available_partially(begin_date, end_date):
                continue
            shift_pattern = self.__carer_shift_patterns[carer]
            absolute_events = shift_pattern.interval(begin_date, end_date)
            diaries = [Diary(**{Diary.DATE: date, Diary.EVENTS: list(events), Diary.SCHEDULE_PATTERN_KEY: None})
                       for date, events in itertools.groupby(absolute_events, key=lambda event: event.begin.date())]
            carers.append(Problem.CarerShift(**{Problem.CarerShift.CARER: carer, Problem.CarerShift.DIARIES: diaries}))

        return carers

    def get_visits(self, area, begin_date, end_date):
        """Return visits within the area"""

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        visits_by_service_user = collections.OrderedDict()
        address_by_service_user = {}
        location_by_service_user = {}

        for visit in self.__get_visits_between(area, begin_date, end_date):
            local_visit = Problem.LocalVisit(date=visit.date,
                                             time=visit.time,
                                             duration=visit.duration,
                                             carer_count=visit.carer_count)
            if visit.service_user in visits_by_service_user:
                visits_by_service_user[visit.service_user].append(local_visit)
            else:
                visits_by_service_user[visit.service_user] = [local_visit]
                location = self.__location_finder.find(visit.address)
                if location is None:
                    logging.error("Failed to find location of the address '%s'", location)
                location_by_service_user[visit.service_user] = location
                address_by_service_user[visit.service_user] = visit.address

        return [Problem.LocalVisits(service_user=service_user, visits=visits)
                for service_user, visits in visits_by_service_user.items()]

    def get_service_users(self, area, begin_date, end_date):

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        address_by_service_user = {}
        location_by_service_user = {}

        for visit in self.__get_visits_between(area, begin_date, end_date):
            if visit.service_user in address_by_service_user:
                continue
            else:
                location = self.__location_finder.find(visit.address)
                if location is None:
                    logging.error("Failed to find location of the address '%s'", location)
                location_by_service_user[visit.service_user] = location
                address_by_service_user[visit.service_user] = visit.address

        service_users = []
        for service_user_id in address_by_service_user.keys():
            carers_frequency = collections.Counter()
            all_visits = 0
            for visit in self.__get_past_visits_for_service_user(service_user_id):
                carers_frequency[visit.carer.sap_number] += 1
                all_visits += 1

            carer_pref_to_use = [(carer, float(count) / all_visits) for carer, count in carers_frequency.most_common(8)]
            carer_pref_to_use.sort(key=lambda element: element[1], reverse=True)

            service_users.append(ServiceUser(key=service_user_id,
                                             address=address_by_service_user[service_user_id],
                                             location=location_by_service_user[service_user_id],
                                             carer_preference=carer_pref_to_use))

    def get_past_schedule(self, area, begin_date, end_date):
        """Return a schedule for a given duration"""

        metadata = Metadata(area=area, begin=begin_date, end=end_date)
        visits = self.__get_past_visits(area, begin_date, end_date)
        return Schedule(visits=visits, metadata=metadata)

    def __get_visits_between(self, area, begin_date, end_date):

        logging.warning("Area '%s' is ignored by the data source", area)

        for visit in itertools.chain(*self.__visits.values()):
            if begin_date <= visit.date < end_date:
                yield visit

    def __get_past_visits(self, area, begin_date, end_date):
        """Return all past visits"""

        logging.warning("Area '%s' is ignored by the data source", area)

        self.__reload_if()
        return [visit for visit in itertools.chain(*self.__past_visits.values()) if begin_date <= visit.date < end_date]

    def __get_past_visits_for_service_user(self, service_user):
        """Return visits of the service user"""

        return [past_visit for past_visit in itertools.chain(*self.__past_visits.values())
                if past_visit.visit and past_visit.visit.service_user == service_user]

    def __reload_if(self):
        if not self.__loaded:
            self.reload()
