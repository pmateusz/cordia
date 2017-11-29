"""Loads test data from CSV"""

import csv
import collections
import datetime
import logging

import os.path

import dateutil.parser

import rows.model.day
from rows.model.address import Address
from rows.model.carer import Carer
from rows.model.shift_pattern import ExecutableShiftPattern, ShiftPattern
from rows.model.visit import Visit


class CSVDataSource:
    """Loads test data from CSV"""

    def __init__(self, carers, shift_patterns, visits):
        self.__carers_file = os.path.realpath(carers)
        self.__shift_patterns_file = os.path.realpath(shift_patterns)
        self.__visits_file = os.path.realpath(visits)

        self.__loaded = False
        self.__carers = []
        self.__visits = []
        self.__carer_shift_patterns = collections.OrderedDict()

    def reload(self):  # pylint: disable=too-many-locals
        """Loads data from data sources"""

        reference_week = dateutil.parser.parse('29/01/2017').date()
        shift_pattern_carer_mapping = collections.defaultdict(list)

        self.__carers = []
        with open(self.__carers_file) as file_stream:
            dialect = csv.Sniffer().sniff(file_stream.read(4096))
            file_stream.seek(0)
            reader = csv.reader(file_stream, dialect)

            # skip header
            next(reader)

            for row in reader:
                if len(row) < 5:
                    logging.warning("Cannot parse carer: '%s'", row)
                    continue

                sap_number, address, position = row[0], Address(post_code=row[1]), row[2]
                carer = Carer(address=address, sap_number=sap_number, position=position)
                self.__carers.append(carer)

                shift_key, shift_week = row[3], int(row[4])
                shift_pattern_carer_mapping[shift_key].append((carer, shift_week))

        self.__visits = []
        with open(self.__visits_file) as file_stream:
            dialect = csv.Sniffer().sniff(file_stream.read(4096))
            file_stream.seek(0)
            reader = csv.reader(file_stream, dialect)

            # skip header
            next(reader)

            for row in reader:
                if len(row) < 6:
                    logging.warning("Cannot parse visit: '%s'", row)
                    continue

                service_user, raw_address, post_code, raw_date, raw_time, raw_duration = row
                address = Address.parse(raw_address)
                address.post_code = post_code.strip()
                self.__visits.append(Visit(service_user=service_user,
                                           date=dateutil.parser.parse(raw_date).date(),
                                           time=datetime.datetime.strptime(raw_time, '%H:%M:%S').time(),
                                           duration=datetime.timedelta(minutes=int(raw_duration)),
                                           address=address))

        self.__carer_shift_patterns = collections.OrderedDict()
        with open(self.__shift_patterns_file) as file_stream:
            dialect = csv.Sniffer().sniff(file_stream.read(4096))
            file_stream.seek(0)
            reader = csv.reader(file_stream, dialect)

            # skip header
            next(reader)

            events = collections.defaultdict(list)
            for row in reader:
                if len(row) < 8:
                    print(row)
                    continue

                key, raw_week, raw_day, _raw_day, raw_begin_hour, raw_begin_minutes, raw_end_hour, raw_end_minutes = row
                week = int(raw_week)
                day = rows.model.day.from_short_name(raw_day)
                begin = datetime.time(hour=int(raw_begin_hour), minute=int(raw_begin_minutes))
                end = datetime.time(hour=int(raw_end_hour), minute=int(raw_end_minutes))

                events[key].append(ShiftPattern.Event(week=week, day=day, begin=begin, end=end))

            for key, events in events.items():
                events_to_use = sorted(events)
                for carer, shift_week in shift_pattern_carer_mapping[key]:
                    self.__carer_shift_patterns[carer] = ExecutableShiftPattern(key=key,
                                                                                events=events_to_use,
                                                                                reference_week=reference_week,
                                                                                reference_shift_week=shift_week)

        self.__loaded = True

    def get_carers(self):
        """Return all carers"""

        self.__reload_if()
        return self.__carers

    def get_carers_for_visit(self, visit):
        """Return carers available for the visit"""

        self.__reload_if()
        available_carers = set()
        for carer, shift_pattern in self.__carer_shift_patterns.items():
            if shift_pattern.is_available_fully(visit.date, visit.time, visit.duration):
                available_carers.add(carer)

        return list(available_carers)

    def get_carers_for_area(self, area, begin_date, end_date):
        """Return carers within the area"""

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        available_carers = set()
        for carer in self.__carers:
            shift_pattern = self.__carer_shift_patterns[carer]
            if shift_pattern and shift_pattern.is_available_partially(begin_date, end_date):
                available_carers.add(carer)

        return list(available_carers)

    def get_visits(self):
        """Return all visits"""

        self.__reload_if()
        return self.__visits

    def get_visits_for_area(self, area, begin, end):
        """Return visits within the area"""

        self.__reload_if()

        logging.warning("Area '%s' is ignored by the data source", area)

        return [visit for visit in self.__visits if begin <= visit.date < end]

    def __reload_if(self):
        if not self.__loaded:
            self.reload()
