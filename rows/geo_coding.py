#!/usr/bin/env python3

import logging
import json
import os
import re
import sys
import requests
import time


def load_api_key():
    with open('./data/settings.json', 'r') as input_stream:
        settings = json.load(input_stream)
        return settings.get('api_key', None)


def load_addresses():
    ROW_PATTERN = re.compile('(?P<user_id>\d+),"(?P<address>.*?)"')
    results = []
    with open('/media/sf_D_DRIVE//addresses_old.csv', 'r', encoding='utf-8-sig') as input_stream:
        for line in input_stream.readlines():
            match = ROW_PATTERN.match(line)
            if match:
                results.append((match.group('user_id'), match.group('address')))
    return results


def load_resolved_users():
    GEOCODING_PATTERN = re.compile('geocoding_.*?.txt')
    ROOT_DIR = './data/'
    resolved_users = set()
    for file_item in os.listdir(ROOT_DIR):
        if GEOCODING_PATTERN.match(file_item):
            filepath = os.path.join(ROOT_DIR, file_item)
            with open(filepath, 'r') as input_stream:
                for line in input_stream:
                    resolved_users.add(line.split(',')[0])
    return resolved_users


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
            logging.error('response does not contain results', body)
        return None, None


class Logger():
    def __init__(self):
        self.__log_file = './data/geocoding_log.txt'

    def add(self, user_id, longitude, latitude):
        with open(self.__log_file, 'a') as output_stream:
            print('{0},{1},{2}'.format(user_id, longitude, latitude), file=output_stream)


if __name__ == '__main__':
    logger = Logger()
    api_key = load_api_key()
    address_pairs = load_addresses()
    resolved_users = load_resolved_users()
    used_requests = 0
    max_requests = 1000
    for user, address in address_pairs:
        if user not in resolved_users:
            print('Query {0}'.format(user))
            location = resolve(address, api_key)
            logger.add(user, location[0], location[1])
            time.sleep(2)
            used_requests += 1
        if used_requests >= max_requests:
            print('Exceeded request quota', file=sys.stderr)
            exit(2)
    print(used_requests)
