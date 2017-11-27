"""Finds longitude and latitude for an address"""

import urllib.parse

import requests

from rows.model.address import Address
from rows.model.location import Location
from rows.model.point_of_interest import PointOfInterest


class Result:
    """Results of a query to the location service"""

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
    def from_json(rows):
        """Returns an object created from rows"""

        if not rows:
            raise RuntimeError('Location service returned no results')

        result_set = []
        for row in rows:
            if 'lon' not in row or 'lat' not in row:
                raise RuntimeError('Geographic system coordinates not found')

            if 'address' not in row:
                raise RuntimeError('Address not found')

            result_set.append({
                'location': Location(longitude=row['lon'], latitude=row['lat']),
                'address': Address(**row['address']),
                'poi': PointOfInterest(**row)})

        if len(result_set) == 1:
            return Result(result_set=result_set[0])
        return Result(result_set=result_set)


class LocationFinder:  # pylint: disable=too-few-public-methods
    """Finds longitude and latitude for an address"""

    def __init__(self, http_client=requests, timeout=1.0):
        self.__timeout = timeout
        self.__http_client = http_client
        self.__params = urllib.parse.urlencode({'format': 'json', 'addressdetails': 1})
        self.__endpoint = 'http://nominatim.openstreetmap.org/search'

    def find(self, address):
        """Sends a query to the location service"""

        url = self.__get_url(address)
        try:
            response = self.__http_client.request('GET', url, timeout=self.__timeout)
            response.raise_for_status()
            results = response.json()
            if not results:
                raise RuntimeError('Location service returned no results')
            return Result.from_json(results)
        except (requests.exceptions.RequestException, RuntimeError) as ex:
            return Result(exception=ex)

    def __get_url(self, address):
        LocationFinder.__precondition_not_none(address.house_number, Address.HOUSE_NUMBER)
        LocationFinder.__precondition_not_none(address.road, Address.ROAD)
        LocationFinder.__precondition_not_none(address.city, Address.CITY)

        if address.country_code:
            return '/'.join([self.__endpoint,
                             urllib.parse.quote(address.country_code),
                             urllib.parse.quote(address.city),
                             urllib.parse.quote(address.road),
                             urllib.parse.quote(address.house_number) + '?' + self.__params])

        return '/'.join([self.__endpoint,
                         urllib.parse.quote('{self.house_number} {self.road}, {self.city}'.format(self=address))
                         + '?' + self.__params])

    @staticmethod
    def __precondition_not_none(value, field_name):
        if value is None:
            raise ValueError(field_name + ' is None')
