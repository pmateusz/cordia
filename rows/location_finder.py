"""Finds longitude and latitude for an address"""

import copy
import json
import logging
import re
import sys
import csv
import urllib.parse
import os.path

import requests

import rows.model.json

from rows.model.address import Address
from rows.model.location import Location
from rows.model.point_of_interest import PointOfInterest
from rows.util.file_system import real_path


class Result:
    """Results of a query to the location service"""

    LOCATION = 'location'
    ADDRESS = 'address'
    POINT_OF_INTEREST = 'poi'

    def __init__(self, result_set=None, exception=None):
        self.__result_set = result_set
        self.__exception = exception

    @property
    def result_set(self):
        """Get property"""

        return self.__result_set

    @property
    def exception(self):
        """Get property"""

        return self.__exception

    @property
    def is_faulted(self):
        """Get property"""

        return bool(self.__exception)

    @staticmethod
    def from_json(json_array):
        """Returns an object created from rows"""

        if not json_array:
            raise RuntimeError('Location service returned no results')

        result_set = []
        for row in json_array:
            if 'lon' not in row or 'lat' not in row:
                raise RuntimeError('Geographic system coordinates not found')

            if 'address' not in row:
                raise RuntimeError('Address not found')

            result_set.append({
                Result.LOCATION: Location(longitude=row['lon'], latitude=row['lat']),
                Result.ADDRESS: Address(**row['address']),
                Result.POINT_OF_INTEREST: PointOfInterest(**row)})

        if len(result_set) == 1:
            return Result(result_set=result_set[0])
        return Result(result_set=result_set)


GLASGOW_LOCATION = Location(latitude=55.858, longitude=-4.259)


def is_within_range(location):
    local_distance = GLASGOW_LOCATION.distance(location)
    return local_distance <= 1.0


class UserLocationFinder:

    def __init__(self, settings):
        self.__user_geo_tagging_file_path = settings.user_geo_tagging_path
        self.__user_locations = {}

    def find(self, user_id):
        if user_id in self.__user_locations:
            return self.__user_locations[user_id]
        return None

    def reload(self):
        try:
            self.__user_locations = {}
            with open(self.__user_geo_tagging_file_path, 'r') as input_stream:
                dialect = csv.Sniffer().sniff(input_stream.read(4096))
                input_stream.seek(0)
                reader = csv.reader(input_stream, dialect=dialect)
                for row in reader:
                    raw_user_id, raw_latitude, raw_longitude = row
                    self.__user_locations[int(raw_user_id)] = Location(longitude=float(raw_longitude),
                                                                       latitude=float(raw_latitude))
        except RuntimeError:
            logging.exception('Failed to reload the user geo tagging file: %s', self.__user_geo_tagging_file_path)


class AddressLocationFinder:
    """Finds longitude and latitude for an address"""

    DEFAULT_TIMEOUT = 1.0

    def __init__(self, http_client=requests, timeout=DEFAULT_TIMEOUT):
        self.__timeout = timeout
        self.__http_client = http_client
        self.__params = urllib.parse.urlencode({'format': 'json', 'addressdetails': 1})
        self.__endpoint = 'http://nominatim.openstreetmap.org/search'
        self.__headers = {'User-Agent': 'University of Strathclyde, Mechanical and Engineering Department'}

    def find(self, address):
        """Sends a query to the location service"""

        url = self.__get_url(address)
        try:
            response = self.__http_client.request('GET', url, timeout=self.__timeout, headers=self.__headers)
            response.raise_for_status()
            results = response.json()
            if not results:
                raise RuntimeError('Location service returned no results')
            return Result.from_json(results)
        except (requests.exceptions.RequestException, RuntimeError) as ex:
            return Result(exception=ex)

    def __get_url(self, address):
        AddressLocationFinder.__precondition_not_none(address.road, Address.ROAD)
        AddressLocationFinder.__precondition_not_none(address.city, Address.CITY)

        url_chunks = [self.__endpoint]

        if not address.country_code and address.house_number and address.road and address.city:
            url_chunks.append(urllib.parse.quote('{self.house_number} {self.road}, {self.city}'.format(self=address)))
        else:
            if address.country_code:
                url_chunks.append(urllib.parse.quote(address.country_code))

            url_chunks.append(urllib.parse.quote(address.city))
            url_chunks.append(urllib.parse.quote(address.road))

            if address.house_number:
                url_chunks.append(urllib.parse.quote(str(address.house_number)))

        return '/'.join(url_chunks) + '?' + self.__params

    @staticmethod
    def __precondition_not_none(value, field_name):
        if value is None:
            raise ValueError(field_name + ' is None')


class FileSystemCache:
    """Caches address locations in a file"""

    def __init__(self, settings):
        self.__settings = settings
        self.__cache = {}

    def find(self, address):
        """Returns cached location"""

        if address in self.__cache:
            return self.__cache[address]
        return None

    def insert_or_update(self, address, location):
        """Updates cache"""

        self.__cache[address] = location

    def try_reload(self):
        """Tries to reload cache"""

        try:
            locations = {}
            if not self.__load(self.__location_cache_path(True), locations):
                logging.warning('Failed to load addresses already resolved by the web service from: {0}'
                                .format(self.__location_cache_path(False)))

            if not self.__load(self.__difficult_locations_path(True), locations):
                logging.warning('Failed to load addresses that cannot be resolved by the web service from: {0}'
                                .format(self.__difficult_locations_path(False)))
            self.__cache = locations
            return True
        except RuntimeError:
            logging.error('Failed to reload location cache due to error: %s', sys.exc_info()[0])
            return False

    def reload(self):
        """Removes local cache entries and tries to reload cache"""

        self.__cache.clear()
        self.try_reload()

    def save(self):
        """Saves a copy of cached entries in the file system"""

        if self.__cache:
            try:
                self.__save_as_json(list(self.__cache.items()), self.__location_cache_path())
            except RuntimeError:
                logging.error('Failed to save location cache due to error: %s', sys.exc_info()[0])

        try:
            if not os.path.exists(self.__difficult_locations_path()):
                self.__save_as_json([], self.__difficult_locations_path())
        except RuntimeError:
            logging.error('Failed to create the difficult location cache due to error: %s', sys.exc_info()[0])

    def __location_cache_path(self, resolve_env_vars=True):
        file_path = self.__settings.location_cache_path
        return real_path(file_path) if resolve_env_vars else file_path

    def __difficult_locations_path(self, resolve_env_vars=True):
        file_path = self.__settings.difficult_locations_path
        return real_path(file_path) if resolve_env_vars else file_path

    @staticmethod
    def __save_as_json(value, file_path):
        with open(file_path, 'w') as stream:
            json.dump(value, stream, indent=2, sort_keys=True, cls=rows.model.json.JSONEncoder)

    @staticmethod
    def __load(path, acc):
        try:
            with open(path, 'r') as file_stream:
                try:
                    pairs = json.load(file_stream)
                    if pairs:
                        for raw_address, raw_location in pairs:
                            acc[Address(**raw_address)] = Location(**raw_location)
                    return True
                except (RuntimeError, json.decoder.JSONDecodeError) as ex:
                    logging.error('Failed to load cache due to unknown format: %s', ex)
                    return False
        except FileNotFoundError:
            logging.info("Cache file '%s' does not exist", path)
            return False


class MultiModeLocationFinder:
    """Location finder with fallback if the primary address cannot be found by the external service"""

    def __init__(self, cache, user_tagging_finder, timeout=AddressLocationFinder.DEFAULT_TIMEOUT):
        self.__cache = cache
        self.__user_tagging_finder = user_tagging_finder
        self.__web_service = AddressLocationFinder(timeout=timeout)

    def find(self, user_id, address):
        """Returns location for an address"""

        location = self.__user_tagging_finder.find(user_id)

        if not location:
            location = self.__cache.find(address)

            if not location:
                location = self.__find(address)
                if location:
                    self.__cache.insert_or_update(address, location)

        if not location:
            logging.error('Failed to find location of the address {0}'.format(address))
            return None

        if not is_within_range(location):
            logging.error('Location {0}:{1} is too far from Glasgow'.format(address, location))
            return None

        return location

    def __find(self, address):
        result = self.__web_service.find(address)
        if result.is_faulted:
            # failed to find a location for the exact address, so try to find an approximate location

            if address.house_number:
                # if house number has multiple components, use the last one

                chunks = re.split(r"[\s\\/]+", address.house_number)
                if chunks:
                    address_to_use = copy.copy(address)
                    address_to_use.house_number = chunks[-1]

                    logging.warning("Failed to find location for exact address '%s'."
                                    " Retrying for an approximate location '%s'",
                                    address, address_to_use)

                    fallback_result = self.__web_service.find(address_to_use)
                    if not fallback_result.is_faulted:
                        return self.__extract_location(fallback_result, address_to_use)

            if address.house_number or address.post_code:
                # try to find the street location only

                address_to_use = copy.copy(address)
                address_to_use.house_number = None
                address_to_use.post_code = None

                logging.warning("Failed to find location for exact address '%s'."
                                " Retrying for an approximate location '%s'",
                                address, address_to_use)

                fallback_result = self.__web_service.find(address_to_use)
                if not fallback_result.is_faulted:
                    return MultiModeLocationFinder.__extract_location(fallback_result, address_to_use)

            logging.error("Failed to find an address '%s' due to the error: %s", address, result.exception)
            return None

        return MultiModeLocationFinder.__extract_location(result, address)

    @staticmethod
    def __extract_location(result, address):
        if result.is_faulted:
            logging.error("Failed to find an address '%s' due to the error: %s", address, result.exception)
            return None

        if not result.result_set:
            logging.error("Failed to find an address '%s' due to unknown error", address)
            return None

        if isinstance(result.result_set, list):
            for row in result.result_set:
                if Result.LOCATION in row:
                    location = row[Result.LOCATION]
                    if is_within_range(location):
                        return location
            logging.error("Failed to find an address '%s'. None of returned results contains a GPS location.", address)
            return None
        else:
            return result.result_set[Result.LOCATION]
