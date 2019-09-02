#!/usr/bin/env python3

"""Anonymizes visit information"""

import argparse
import collections
import csv
import os.path
import sys

"""
Header structure:

"""


class ValueGenerator:
    def __init__(self, values):
        values_to_use = list(values)
        values_to_use.sort()
        self.__value_mapping = {}
        local_id = 0
        for value in values_to_use:
            self.__value_mapping[value] = local_id
            local_id += 1

    def __call__(self, value):
        return self.__value_mapping.get(value, -1)


VisitFieldNames = ['VisitID', 'UserID', 'PlannedCarerID', 'PlannedStartDateTime',
                   'PlannedEndDateTime', 'PlannedDuration',
                   'OriginalStartDateTime', 'OriginalEndDateTime',
                   'OriginalDuration', 'CheckInDateTime',
                   'CheckOutDateTime', 'RealDuration', 'CheckOutMethod',
                   'Tasks', 'Area']
Visit = collections.namedtuple('Visit', VisitFieldNames)


def unpack_visit(row):
    visit_id, user_id, carer_id, planned_start, planned_end, planned_duration, original_start, \
    original_end, original_duration, check_in, check_out, real_duration, check_out_method, tasks, area = row
    return Visit(int(visit_id), int(user_id), int(carer_id), planned_start, planned_end, int(planned_duration),
                 original_start, original_end, original_duration, check_in, check_out, real_duration, check_out_method,
                 tasks, area)


class Anonymizer:
    def __init__(self, visit_id_gen, carer_id_gen, user_id_gen, area_id_gen):
        self.__visit_id_gen = visit_id_gen
        self.__carer_id_gen = carer_id_gen
        self.__user_id_gen = user_id_gen
        self.__area_id_gen = area_id_gen

    def __call__(self, visit):
        return visit._replace(VisitID=self.__visit_id_gen(visit.VisitID),
                              UserID=self.__user_id_gen(visit.UserID),
                              PlannedCarerID=self.__carer_id_gen(visit.PlannedCarerID),
                              Area=self.__area_id_gen(visit.Area))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(prog=sys.argv[0], description='File anonymize utility')
    parser.add_argument('file', help='The file to anonymize')
    args = parser.parse_args(sys.argv[1:])
    file_path = getattr(args, 'file')

    filename = os.path.basename(file_path)
    stem, ext = os.path.splitext(filename)
    output_filename = '{0}_anonymized{1}'.format(stem, ext)

    visit_ids = set()
    carer_ids = set()
    user_ids = set()
    area_ids = set()
    with open(file_path, 'r', encoding='utf-8-sig') as input_stream:
        dialect = csv.Sniffer().sniff(input_stream.read(4096))
        input_stream.seek(0)
        reader = csv.reader(input_stream, dialect=dialect)
        for row in reader:
            visit = unpack_visit(row)
            visit_ids.add(visit.VisitID)
            carer_ids.add(visit.PlannedCarerID)
            user_ids.add(visit.UserID)
            area_ids.add(visit.Area)
        visit_id_gen = ValueGenerator(visit_ids)
        carer_id_gen = ValueGenerator(carer_ids)
        user_id_gen = ValueGenerator(user_ids)
        area_id_gen = ValueGenerator(area_ids)
        anonymizer = Anonymizer(visit_id_gen, carer_id_gen, user_id_gen, area_id_gen)
        input_stream.seek(0)
        with open(output_filename, 'w') as output_stream:
            writer = csv.writer(output_stream, dialect=dialect)
            writer.writerow(VisitFieldNames)
            for row in reader:
                visit = unpack_visit(row)
                writer.writerow(anonymizer(visit))
