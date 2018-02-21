"""Finds longitude and latitude for an address"""

import urllib.parse

import copy
import json
import logging
import re
import sys

import requests

import rows.model.json

from rows.model.address import Address
from rows.model.location import Location
from rows.model.point_of_interest import PointOfInterest


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


class WebLocationFinder:
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
        WebLocationFinder.__precondition_not_none(address.road, Address.ROAD)
        WebLocationFinder.__precondition_not_none(address.city, Address.CITY)

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

    def __init__(self, file_path):
        self.__file_path = file_path
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
            candidate = FileSystemCache.__get_difficult_locations()
            try:
                with open(self.__file_path, 'r') as file_stream:
                    try:
                        pairs = json.load(file_stream)
                        if pairs:
                            for raw_address, raw_location in pairs:
                                candidate[Address(**raw_address)] = Location(**raw_location)
                    except (RuntimeError, json.decoder.JSONDecodeError) as ex:
                        logging.error('Failed to load cache due to unknown format: %s', ex)
                        return False
            except FileNotFoundError:
                logging.info("Cache file '%s' does not exist", self.__file_path)
            self.__cache = candidate
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

        try:
            with open(self.__file_path, 'w') as stream:
                json.dump(list(self.__cache.items()), stream, indent=2, sort_keys=True, cls=rows.model.json.JSONEncoder)
        except RuntimeError:
            logging.error('Failed to save location cache due to error: %s', sys.exc_info()[0])

    @staticmethod
    def __get_difficult_locations():
        return {Address(road='Brackla Avenue', house_number='32', city='Glasgow', post_code='G13 4HZ'):
                    Location(latitude=55.8962479, longitude=-4.3835123)}


class RobustLocationFinder:
    """Location finder with fallback if the primary address cannot be found by the external service"""

    def __init__(self, cache, timeout=WebLocationFinder.DEFAULT_TIMEOUT):
        self.__cache = cache
        self.__web_service = WebLocationFinder(timeout=timeout)

    def find(self, address):
        """Returns location for an address"""

        location = self.__cache.find(address)
        if not location:
            location = self.__find(address)
            if location:
                self.__cache.insert_or_update(address, location)
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
                    return RobustLocationFinder.__extract_location(fallback_result, address_to_use)

            logging.error("Failed to find an address '%s' due to the error: %s", address, result.exception)
            return None

        return RobustLocationFinder.__extract_location(result, address)

    @staticmethod
    def __extract_location(result, address):
        if result.is_faulted:
            logging.error("Failed to find an address '%s' due to the error: %s", address, result.exception)
            return None

        if not result.result_set:
            logging.error("Failed to find an address '%s' due to unknown error", address)
            return None

        if isinstance(result.result_set, list):
            logging.warning(
                "Too many locations returned for an address '%s'."
                " Selecting the first one to resolve conflict.",
                address)
            bundle = result.result_set[0]
        else:
            bundle = result.result_set

        return bundle[Result.LOCATION]
