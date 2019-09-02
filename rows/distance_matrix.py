#!/usr/bin/env python3

import csv
import os
import sys
import pathlib
import collections

import pandas

import pyodbc

import tqdm

import rows.settings
import rows.routing_server
import rows.model.location

from rows.util.file_system import real_path


class Connector:
    def __init__(self, settings):
        self.__settings = settings
        self.__connection = None

    def __enter__(self):
        try:
            self.__connection = pyodbc.connect(self.connection_string())
        except pyodbc.OperationalError as ex:
            error_msg = "Failed to establish connection with the database server: '{0}'. " \
                        "Ensure that the database server is available in the network," \
                        " database '{1}' exists, username '{2}' is authorized to access the database." \
                        " and the password is valid".format(self.__settings.database_server,
                                                            self.__settings.database_name,
                                                            self.__settings.database_user)
            raise RuntimeError(error_msg, ex)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.__connection.close()
        self.__connection = None

    def __load_credentials(self):
        path = pathlib.Path(real_path(self.__settings.database_credentials_path))
        try:
            with path.open() as file_stream:
                return file_stream.read().strip()
        except FileNotFoundError as ex:
            raise RuntimeError(
                "Failed to open the the file '{0}' which is expected to store the database credentials."
                " Create the file in the specified location and try again.".format(path), ex)

    def connection_string(self):
        config = {'Driver': '{' + self.__settings.database_driver + '}',
                  'Server': self.__settings.database_server,
                  'Database': self.__settings.database_name,
                  'UID': self.__settings.database_user,
                  'PWD': self.__load_credentials(),
                  'Encrypt': 'yes',
                  'Connection Timeout': 5,
                  'TrustServerCertificate': 'yes'}
        return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])

    def connection(self):
        assert self.__connection
        return self.__connection


def load_locations(file_paths):
    database = {}
    for file_path in file_paths:
        with open(file_path, 'r') as file:
            dialect = csv.Sniffer().sniff(file.read(1024))
            file.seek(0)
            reader = csv.reader(file, dialect=dialect)
            for user, latitude, longitude in reader:
                database[int(user)] = rows.model.location.Location(latitude=latitude, longitude=longitude)
    return database


def build_distance_matrix():
    __script_file = os.path.realpath(__file__)
    __install_dir = os.path.dirname(os.path.dirname(__script_file))

    __settings = rows.settings.Settings(__install_dir)
    __settings.reload()
    with Connector(__settings) as __connector:
        __connection = __connector.connection()
        __cursor = __connection.cursor().execute('SELECT DISTINCT service_user_id'
                                                 ' FROM dbo.ListVisitsWithinWindow visits'
                                                 ' ORDER BY service_user_id')
        __users = [int(row[0]) for row in __cursor]
    __user_locations = load_locations(['/home/pmateusz/dev/cordia/data/user_geo_tagging.csv'])
    __users.sort()

    __old_frame = pandas.read_csv('/home/pmateusz/dev/cordia/old_distance_matrix.txt')

    __distance_matrix = [[source_user] + [0 for _ in __users] for source_user in __users]
    __users_count = len(__users)
    __distance_count = __users_count * (__users_count - 1) // 2
    __routing_server = rows.routing_server.RoutingServer(real_path('~/dev/cordia/build/rows-routing-server'),
                                                         real_path('~/dev/cordia/data/cars/scotland-latest.osrm'))
    with tqdm.tqdm(total=__distance_count) as __progress_bar:
        with __routing_server as __routing_session:
            for __source_index in range(__users_count):
                for __destination_index in range(__source_index + 1, __users_count):
                    source_id = __users[__source_index]
                    destination_id = __users[__destination_index]
                    distance = __routing_session.distance(__user_locations[source_id],
                                                          __user_locations[destination_id])
                    assert distance is not None
                    __distance_matrix[__source_index][__destination_index + 1] = distance
                    __distance_matrix[__destination_index][__source_index + 1] = distance
                    __progress_bar.update(1)

    frame = pandas.DataFrame(columns=['UserId'] + __users, data=__distance_matrix)
    frame.set_index('UserId')
    with open('old_car_distance_matrix.txt', 'w') as file:
        frame.to_csv(file)


def update_ids_in_distance_matrix(file_path, output_file_path):
    script_file = os.path.realpath(__file__)
    install_dir = os.path.dirname(os.path.dirname(script_file))

    settings = rows.settings.Settings(install_dir)
    settings.reload()

    with Connector(settings) as connector:
        connection = connector.connection()
        cursor = connection.cursor().execute('SELECT DISTINCT OldId, AnonymizedId'
                                             ' FROM dbo.ServiceUserIds')
        old_to_new = collections.OrderedDict()
        new_to_old = collections.OrderedDict()
        for row in cursor:
            old_id = int(row[0])
            new_id = int(row[1])
            old_to_new[old_id] = new_id
            new_to_old[new_id] = old_id

    with open(file_path, 'r') as stream:
        old_frame = pandas.read_csv(stream)

    items = old_frame.shape[0]
    data = []
    for index, row in old_frame.iterrows():
        new_row = [old_to_new[row[1]]]
        new_row.extend(row[2:])
        data.append(new_row)

    columns = [old_frame.columns[1]]
    for old_column in old_frame.columns[2:]:
        columns.append(old_to_new[int(old_column)])

    trimmed_columns = columns[:31]
    trimmed_data = [row[:31] for row in data[:30]]

    new_frame = pandas.DataFrame(columns=trimmed_columns, data=trimmed_data)
    with open(output_file_path, 'w') as stream:
        new_frame.to_csv(stream)


if __name__ == '__main__':
    update_ids_in_distance_matrix('/home/pmateusz/dev/cordia/old_distance_matrix.txt',
                                  '/home/pmateusz/dev/cordia/old_updated_distance_matrix.txt')

    sys.exit(0)
