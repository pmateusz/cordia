#!/usr/bin/env python3

import collections
import logging
import json
import os
import re
import sys
import requests
import time

import tqdm


def load_api_key():
    with open('./data/settings.json', 'r') as input_stream:
        settings = json.load(input_stream)
        return settings.get('api_key', None)


def load_addresses(file_path):
    ROW_PATTERN = re.compile('(?P<user_id>\d+),"(?P<address>.*?)"')
    results = {}
    with open(file_path, 'r', encoding='utf-8-sig') as input_stream:
        for line in input_stream.readlines():
            match = ROW_PATTERN.match(line)
            if match:
                results[match.group('user_id')] = match.group('address')
    return results


def load_resolved_users_from_file(file_path):
    result = set()
    if not os.path.isfile(file_path):
        return result

    with open(file_path, 'r') as input_stream:
        for line in input_stream:
            result.add(line.split(',')[0])
    return result


AddressDatabase = collections.namedtuple('AddressDatabase', ['version', 'address_file', 'geo_tagging_file'])

AddressDatabaseRecord = collections.namedtuple('AddressDatabaseRecord', ['version', 'address'])

AddressDatabases = [
    AddressDatabase(1, '/media/sf_D_DRIVE/addresses_february.csv', './data/user_geo_tagging_february.csv'),
    AddressDatabase(2, '/media/sf_D_DRIVE/addresses_april.csv', './data/user_geo_tagging_april.csv'),
    AddressDatabase(3, '/media/sf_D_DRIVE/addresses_september.csv', './data/user_geo_tagging_september.csv')
]


def load_resolved_users():
    master_address_database = {}
    for database in AddressDatabases:
        addresses = load_addresses(database.address_file)
        for user in addresses:
            local_address = addresses[user]
            if (user not in master_address_database) or (user in master_address_database
                                                         and master_address_database[user].address != local_address
                                                         and master_address_database[user].version < database.version):
                master_address_database[user] = AddressDatabaseRecord(database.version, local_address)

    master_resolved_users = set()
    for database in AddressDatabases:
        if not os.path.isfile(database.geo_tagging_file):
            continue
        locations = load_resolved_users_from_file(database.geo_tagging_file)
        for user in locations:
            if user in master_address_database and master_address_database[user].version == database.version:
                master_resolved_users.add(user)
            else:
                logging.error('User %s not found in the master address database', user)

    return master_resolved_users


def resolve(address, api_key):
    with requests.get('https://maps.googleapis.com/maps/api/geocode/json',
                      params={'key': api_key, 'address': address},
                      allow_redirects=True) as r:
        body = r.json()
        if 'error_message' in body:
            logging.error(body['error_message'])
            exit(1)
        results = body.get('results', list())
        if isinstance(results, list):
            if results:
                geometry = results[0].get('geometry')
                if geometry:
                    location = geometry.get('location')
                    if location:
                        return location.get('lat'), location.get('lng')
                    else:
                        logging.error('geometry does not contain location', geometry)
                else:
                    logging.error('results does not contain geometry', results)
            else:
                logging.error('No results for address %s', address)
        else:
            logging.error('response does not contain results', body)
        return None, None


class Logger:
    def __init__(self):
        self.__log_file = AddressDatabases[-1].geo_tagging_file

    def add(self, user_id, longitude, latitude):
        with open(self.__log_file, 'a') as output_stream:
            print('{0},{1},{2}'.format(user_id, longitude, latitude), file=output_stream)


if __name__ == '__main__':
    address_file = '/media/sf_D_DRIVE/addresses_september.csv'
    if not os.path.isfile(address_file):
        sys.exit(1)
    address_pairs = load_addresses(address_file)

    logger = Logger()
    api_key = load_api_key()

    resolved_users = load_resolved_users()
    used_requests = 0
    max_requests = 2000

    users_to_resolve = [user for user in address_pairs if user not in resolved_users]
    for user in users_to_resolve:
        print(user, address_pairs[user])
    # for user in tqdm.tqdm(users_to_resolve):
    #     if used_requests >= max_requests:
    #         print('Exceeded request quota', file=sys.stderr)
    #         exit(2)
    #     print('Query {0}'.format(user))
    #     location = resolve(address_pairs[user], api_key)
    #     if location[0] and location[1]:
    #         logger.add(user, location[0], location[1])
    #         time.sleep(2)
    #     used_requests += 1
    #
    # print(used_requests)
