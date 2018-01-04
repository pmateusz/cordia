"""Loads test data from CSV"""

import csv
import collections
import datetime
import logging
import itertools

import dateutil.parser

import rows.model.day
from rows.model.address import Address
from rows.model.carer import Carer
from rows.model.event import RelativeEvent
from rows.model.shift_pattern import ExecutableShiftPattern
from rows.model.visit import Visit
from rows.model.past_visit import PastVisit


class CSVDataSource:  # pylint: disable=too-many-instance-attributes
    """Loads test data from CSV"""

    def __init__(self, carers_file, shift_patterns_file, visits_file, past_visits_file):
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

    def reload(self):  # pylint: disable=too-many-locals
        """Loads data from data sources"""

        reference_week = dateutil.parser.parse('29/01/2017').date()
        shift_pattern_carer_mapping = collections.defaultdict(list)

        def create_sniffed_reader(local_file_stream):
            dialect = csv.Sniffer().sniff(local_file_stream.read(4096))
            local_file_stream.seek(0)
            reader = csv.reader(local_file_stream, dialect)

            # skip header
            next(reader)

            return reader

        self.__carers = {}
        with open(self.__carers_file) as file_stream:
            for row in create_sniffed_reader(file_stream):
                if len(row) < 5:
                    logging.warning("Cannot parse carer: '%s'", row)
                    continue

                sap_number, address, position = row[0], Address(post_code=row[1]), row[2]
                carer = Carer(address=address, sap_number=sap_number, position=position)
                self.__carers[carer.sap_number] = carer

                shift_key, shift_week = row[3], int(row[4])
                shift_pattern_carer_mapping[shift_key].append((carer, shift_week))

        self.__visits = collections.defaultdict(list)
        with open(self.__visits_file) as file_stream:
            for row in create_sniffed_reader(file_stream):
                if len(row) < 6:
                    logging.warning("Cannot parse visit: '%s'", row)
                    continue

                service_user, raw_address, post_code, raw_date, raw_time, raw_duration = row
                address = Address.parse(raw_address)
                address.post_code = post_code.strip()
                self.__visits[service_user].append(Visit(service_user=service_user,
                                                         date=datetime.datetime.strptime(raw_date, '%d/%m/%Y').date(),
                                                         time=datetime.datetime.strptime(raw_time, '%H:%M:%S').time(),
                                                         duration=datetime.timedelta(minutes=int(raw_duration)),
                                                         address=address))

        self.__carer_shift_patterns = collections.OrderedDict()
        with open(self.__shift_patterns_file) as file_stream:
            events = collections.defaultdict(list)
            for row in create_sniffed_reader(file_stream):
                if len(row) < 8:
                    print(row)
                    continue

                key, raw_week, raw_day, _raw_day, raw_begin_hour, raw_begin_minutes, raw_end_hour, raw_end_minutes = row
                week = int(raw_week)
                day = rows.model.day.from_short_name(raw_day)
                begin = datetime.time(hour=int(raw_begin_hour), minute=int(raw_begin_minutes))
                end = datetime.time(hour=int(raw_end_hour), minute=int(raw_end_minutes))

                events[key].append(RelativeEvent(week=week, day=day, begin=begin, end=end))

            for key, events in events.items():
                events_to_use = sorted(events)
                for carer, shift_week in shift_pattern_carer_mapping[key]:
                    self.__carer_shift_patterns[carer] = ExecutableShiftPattern(key=key,
                                                                                events=events_to_use,
                                                                                reference_week=reference_week,
                                                                                reference_shift_week=shift_week)

        with open(self.__past_visits_file) as file_stream:
            date_time_format = '%d/%m/%Y %H:%M'
            past_visits = collections.defaultdict(list)
            for row in create_sniffed_reader(file_stream):
                if len(row) < 7:
                    print(row)
                    continue
                service_user, raw_begin, raw_end, raw_check_in, raw_check_out, raw_cancelled, sap_number = row
                begin = datetime.datetime.strptime(raw_begin, date_time_format)
                end = datetime.datetime.strptime(raw_end, date_time_format)
                check_in = None if raw_check_in == 'NULL' \
                    else datetime.datetime.strptime(raw_check_in, date_time_format)
                check_out = None if raw_check_out == 'NULL' \
                    else datetime.datetime.strptime(raw_check_out, date_time_format)

                cancelled = raw_cancelled == 'Y'
                carer = self.__carers.get(sap_number, None)

                visit = None
                if not cancelled:
                    visit = next(filter(
                        lambda local_visit: local_visit.date == begin.date() and local_visit.time == begin.time(),
                        self.__visits[service_user]), None)
                    if not visit:
                        visits_for_the_day = list(filter(lambda local_visit: local_visit.date == begin.date(),
                                                         self.__visits[service_user]))
                        # if no visits have been found, try to find a nearest visit
                        if visits_for_the_day:
                            ref_time_delta = datetime.timedelta(hours=begin.hour, minutes=begin.minute)
                            visit = min(visits_for_the_day, key=lambda local_visit: abs(
                                datetime.timedelta(hours=local_visit.time.hour,
                                                   minutes=local_visit.time.minute) - ref_time_delta))
                past_visits[sap_number].append(
                    PastVisit(cancelled=cancelled,
                              carer=carer,
                              visit=visit,
                              date=begin.date(),
                              time=begin.time(),
                              duration=end - begin,
                              check_in=check_in,
                              check_out=check_out))
            self.__past_visits = past_visits
        self.__loaded = True

    def get_carers(self):
        """Return all carers"""

        self.__reload_if()
        return list(itertools.chain(self.__carers.values()))

    def get_past_visits(self):
        """Return all past visits"""

        self.__reload_if()
        return list(itertools.chain(*self.__past_visits.values()))

    def get_carers_for_visit(self, visit):
        """Return carers available for the visit"""

        self.__reload_if()
        available_carers = set()
        for carer, shift_pattern in self.__carer_shift_patterns.items():
            if shift_pattern.is_available_fully(visit.date, visit.time, visit.duration):
                available_carers.add(carer)

        return list(available_carers)

    def get_interval_for_carer(self, carer, begin_date, end_date):
        """Returns events from the carers' diary between dates"""

        self.__reload_if()
        shift_pattern = self.__carer_shift_patterns[carer]
        return shift_pattern.interval(begin_date, end_date)

    def get_carers_for_area(self, area, begin_date, end_date):
        """Return carers within the area"""

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        available_carers = set()
        for carer in self.__carers.values():
            shift_pattern = self.__carer_shift_patterns[carer]
            if shift_pattern.is_available_partially(begin_date, end_date):
                available_carers.add(carer)

        return list(available_carers)

    def get_visits(self):
        """Return all visits"""

        self.__reload_if()
        return list(itertools.chain(*self.__visits.values()))

    def get_visits_for_area(self, area, begin, end):
        """Return visits within the area"""

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        return [visit for visit in self.get_visits() if begin <= visit.date < end]

    def __reload_if(self):
        if not self.__loaded:
            self.reload()
