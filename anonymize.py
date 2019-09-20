#!/usr/bin/env python3

import argparse
import csv
import itertools
import json
import math
import os
import random

import numpy
import pandas

import rows.database_connector
import rows.model.visit
import rows.plot
import rows.settings


class DatabaseAnonymizer:

    def __init__(self, settings):
        self.__settings = settings
        self.__connector = rows.database_connector.DatabaseConnector(settings)
        self.__aom_ids = None
        self.__carer_ids = None
        self.__user_ids = None
        self.__visit_ids = None

    def reload(self):
        self.__aom_ids = {}
        self.__carer_ids = {}
        self.__user_ids = {}
        self.__visit_ids = {}

        connector = rows.database_connector.DatabaseConnector(settings)
        with connector as connection:
            for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.CarerIds'):
                self.__carer_ids[old_id] = anonymized_id

            for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.ServiceUserIds'):
                self.__user_ids[old_id] = anonymized_id

            for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.VisitIds'):
                self.__visit_ids[old_id] = anonymized_id

            for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.AomIds'):
                self.__aom_ids[old_id] = anonymized_id

    def visit(self, real_id: int):
        return self.__visit_ids[real_id]

    def user(self, real_id: int):
        return self.__user_ids[real_id]

    def carer(self, real_id: int):
        return self.__carer_ids[real_id]

    def aom(self, real_id: int):
        return self.__aom_ids[real_id]


class RandomAnonymizer:
    class RandomMapping:
        MAX_TRIES = 5

        def __init__(self, generator: random.Random, max_id: int):
            self.__generator = generator
            self.__max_id = max_id
            self.__used_ids = set()
            self.__mapping = {}

        def __call__(self, real_id: int):
            if real_id in self.__mapping:
                return self.__mapping[real_id]

            for _ in range(RandomAnonymizer.RandomMapping.MAX_TRIES):
                anonymized_id = self.__generator.randint(0, self.__max_id)
                if anonymized_id not in self.__used_ids:
                    self.__used_ids.add(anonymized_id)
                    self.__mapping[real_id] = anonymized_id
                    return anonymized_id

            raise ValueError('Failed to generate a new id within {0} iterations. Consider increasing the max_id parameter.'
                             .format(RandomAnonymizer.RandomMapping.MAX_TRIES))

    def __init__(self):
        self.__generator = None
        self.__aom_ids = None
        self.__carer_ids = None
        self.__user_ids = None
        self.__visit_ids = None

    def reload(self):
        self.__generator = random.Random(0)
        self.__aom_ids = RandomAnonymizer.RandomMapping(self.__generator, 10)
        self.__carer_ids = RandomAnonymizer.RandomMapping(self.__generator, 1000)
        self.__user_ids = RandomAnonymizer.RandomMapping(self.__generator, 1000)
        self.__visit_ids = RandomAnonymizer.RandomMapping(self.__generator, 500)

    def aom(self, real_id: int):
        return self.__aom_ids(real_id)

    def carer(self, real_id: int):
        return self.__carer_ids(real_id)

    def user(self, real_id: int):
        return self.__user_ids(real_id)

    def visit(self, real_id: int):
        return self.__visit_ids(real_id)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(os.path.basename(__file__))
    parser.add_argument('problem_file')
    parser.add_argument('--use-database', type=bool, default=False)
    return parser.parse_args()


if __name__ == '__main__':
    args = parse_args()

    problem_file = getattr(args, 'problem_file')
    use_database = getattr(args, 'use_database')

    base_problem_name, _ = os.path.splitext(os.path.basename(problem_file))
    visits_file = base_problem_name + '_visits.csv'
    distance_file = base_problem_name + '_distance.csv'
    carers_file = base_problem_name + '_carers.csv'

    script_file = os.path.realpath(__file__)
    install_dir = os.path.dirname(script_file)
    settings = rows.settings.Settings(install_dir)
    settings.reload()

    if use_database:
        anonymizer = DatabaseAnonymizer(settings)
        anonymizer.reload()
    else:
        anonymizer = RandomAnonymizer()
        anonymizer.reload()

    with open(problem_file, 'r') as input_stream:
        problem_json = json.load(input_stream)

    for carer in problem_json['carers']:
        old_number = int(carer['carer']['sap_number'])
        carer['carer']['sap_number'] = anonymizer.carer(old_number)

    reverse_user_mapping = {}
    for visit in problem_json['visits']:
        old_number = int(visit['service_user'])
        new_number = anonymizer.user(old_number)
        visit['service_user'] = new_number
        reverse_user_mapping[new_number] = old_number

        for inner_visit in visit['visits']:
            old_number = int(inner_visit['key'])
            inner_visit['key'] = anonymizer.visit(old_number)

    problem_json['metadata']['area']['key'] = anonymizer.aom(int(problem_json['metadata']['area']['key']))
    del problem_json['metadata']['area']['code']

    for service_user in problem_json['service_users']:
        del service_user['location']
        del service_user['address']
        service_user['key'] = anonymizer.user(int(service_user['key']))
        for preference in service_user['carer_preference']:
            preference[0] = anonymizer.carer(int(preference[0]))

    with open(visits_file, 'w') as visits_file:
        writer = csv.DictWriter(visits_file, fieldnames=['VisitId', 'UserId', 'Date', 'Time', 'Duration', 'CarerCount'])
        writer.writeheader()
        for service_user in problem_json['visits']:
            for visit in service_user['visits']:
                writer.writerow({'VisitId': visit['key'],
                                 'UserId': service_user['service_user'],
                                 'Date': visit['date'],
                                 'Time': visit['time'],
                                 'Duration': math.ceil(float(visit['duration'])),
                                 'CarerCount': visit['carer_count']})

    with open(carers_file, 'w') as carers_file:
        writer = csv.DictWriter(carers_file, fieldnames=['CarerId', 'Date', 'Begin', 'End'])
        writer.writeheader()
        for carer in problem_json['carers']:
            sap_number = carer['carer']['sap_number']
            for diary in carer['diaries']:
                for event in diary['events']:
                    writer.writerow({'CarerId': sap_number,
                                     'Date': diary['date'],
                                     'Begin': event['begin'],
                                     'End': event['end']})

    with rows.plot.create_routing_session() as routing_session:
        distance_estimator = rows.plot.DistanceEstimator(settings, routing_session)
        referenced_user_ids = list({visit['service_user'] for visit in problem_json['visits']})
        user_index = {referenced_user_ids[index]: index for index in range(len(referenced_user_ids))}
        user_visits = {user_id: [rows.model.visit.Visit.from_json(visit_json)
                                 for visit_json in problem_json['visits'] if visit_json['service_user'] == user_id]
                       for user_id in referenced_user_ids}
        raw_distance_matrix = numpy.zeros((len(user_index), len(user_index)))
        for user_a, user_b in itertools.combinations(referenced_user_ids, 2):
            assert user_visits[user_a]
            assert user_visits[user_b]

            user_a_index = user_index[user_a]
            user_b_index = user_index[user_b]

            user_a_old_id = reverse_user_mapping[user_a]
            user_b_old_id = reverse_user_mapping[user_b]
            distance = distance_estimator.user_distance(user_a_old_id, user_b_old_id)
            assert distance.total_seconds() >= 0

            raw_distance_matrix[user_a_index, user_b_index] = distance.total_seconds()
            raw_distance_matrix[user_b_index, user_a_index] = distance.total_seconds()

        reduced_distance_matrix = pandas.DataFrame(data=raw_distance_matrix, columns=referenced_user_ids)
        reduced_distance_matrix.to_csv(distance_file)
