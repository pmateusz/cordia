#!/usr/bin/env python3

import csv
import json
import math
import os
import pandas

import rows.settings
import rows.database_connector

# open distance map

if __name__ == '__main__':
    __problem_file = '/home/pmateusz/dev/cordia/simulations/c350_forecasted_problem.json'
    __distance_matrix = '/home/pmateusz/dev/cordia/old_distance_matrix.txt'

    script_file = os.path.realpath(__file__)
    install_dir = os.path.dirname(script_file)

    settings = rows.settings.Settings(install_dir)
    settings.reload()

    aom_ids = {}
    carer_ids = {}
    user_ids = {}
    visit_ids = {}
    connector = rows.database_connector.DatabaseConnector(settings)
    with connector as connection:
        for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.CarerIds'):
            carer_ids[old_id] = anonymized_id

        for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.ServiceUserIds'):
            user_ids[old_id] = anonymized_id

        for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.VisitIds'):
            visit_ids[old_id] = anonymized_id

        for old_id, anonymized_id in connection.cursor().execute('SELECT OldId, AnonymizedId FROM dbo.AomIds'):
            aom_ids[old_id] = anonymized_id

    with open(__problem_file) as input_stream:
        problem_json = json.load(input_stream)

    referenced_user_ids = set()
    for service_user in problem_json['visits']:
        referenced_user_ids.add(service_user['service_user'])

    for carer in problem_json['carers']:
        old_number = int(carer['carer']['sap_number'])
        carer['carer']['sap_number'] = carer_ids[old_number]

    for visit in problem_json['visits']:
        old_number = int(visit['service_user'])
        visit['service_user'] = user_ids[old_number]

        for inner_visit in visit['visits']:
            old_number = int(inner_visit['key'])
            inner_visit['key'] = visit_ids[old_number]

    problem_json['metadata']['area']['key'] = aom_ids[int(problem_json['metadata']['area']['key'])]
    del problem_json['metadata']['area']['code']

    for service_user in problem_json['service_users']:
        del service_user['location']
        del service_user['address']
        service_user['key'] = user_ids[int(service_user['key'])]
        for preference in service_user['carer_preference']:
            preference[0] = carer_ids[int(preference[0])]

    with open('visits.csv', 'w') as visits_file:
        writer = csv.DictWriter(visits_file, fieldnames=['VisitId', 'UserId', 'Date', 'Time', 'Duration', 'CarerCount'])
        writer.writeheader()
        for service_user in problem_json['visits']:
            for visit in service_user['visits']:
                writer.writerow({
                    'VisitId': visit['key'],
                    'UserId': service_user['service_user'],
                    'Date': visit['date'],
                    'Time': visit['time'],
                    'Duration': math.ceil(float(visit['duration'])),
                    'CarerCount': visit['carer_count']
                })

    with open('carers.csv', 'w') as carers_file:
        writer = csv.DictWriter(carers_file, fieldnames=['CarerId', 'Date', 'Begin', 'End'])
        writer.writeheader()
        for carer in problem_json['carers']:
            sap_number = carer['carer']['sap_number']
            for diary in carer['diaries']:
                for event in diary['events']:
                    writer.writerow({
                        'CarerId': sap_number, 'Date': diary['date'], 'Begin': event['begin'], 'End': event['end']
                    })

    distance_matrix = pandas.read_csv(__distance_matrix)

    sorted_referenced_user_ids = list(referenced_user_ids)
    sorted_referenced_user_ids.sort()

    raw_distance_matrix = []
    row_number = 0
    col_number = 0

    for index, row in distance_matrix.iterrows():
        if str(row['UserId']) in referenced_user_ids:
            filtered_row = [user_ids[row['UserId']]]
            for column in distance_matrix:
                if column in referenced_user_ids:
                    filtered_row.append(row[column])
            raw_distance_matrix.append(filtered_row)
    raw_columns = ['UserId']
    raw_columns.extend([user_ids[int(column)] for column in distance_matrix if column in referenced_user_ids])
    reduced_distance_matrix = pandas.DataFrame(data=raw_distance_matrix, columns=raw_columns)
    reduced_distance_matrix.to_csv('distance.csv')
