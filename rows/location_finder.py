"""Finds longitude and latitude for an address"""

import math
import urllib.parse

import requests

from address import Address


class Coordinates:
    """Coordinates in graphic coordination system"""

    ABSOLUTE_ACCURACY = 0.001

    def __init__(self, longitude, latitude):
        self.__longitude = longitude
        self.__latitude = latitude

    @property
    def longitude(self):
        """Get property"""

        return self.__longitude

    @property
    def latitude(self):
        """Get property"""

        return self.__latitude

    def tuple(self):
        """Returns object as tuple"""

        return self.__longitude, self.__latitude

    def __eq__(self, other):
        if not isinstance(other, Coordinates):
            return False

        return math.isclose(float(self.__longitude), float(other.longitude), abs_tol=Coordinates.ABSOLUTE_ACCURACY) \
            and math.isclose(float(self.__latitude), float(other.latitude), abs_tol=Coordinates.ABSOLUTE_ACCURACY)

    def __hash__(self):
        return hash(self.tuple())

    def __repr__(self):
        return self.tuple().__repr__()

    def __str__(self):
        return "({self.longitude}, {self.latitude})".format(self=self)


class Result:
    """Results of a query to the location service"""

    def __init__(self, address=None, coordinates=None, exception=None):
        self.__address = address
        self.__coordinates = coordinates
        self.__exception = exception

    @property
    def address(self):
        """Get property"""

        return self.__address

    @property
    def coordinates(self):
        """Get property"""

        return self.__coordinates

    @property
    def exception(self):
        """Get property"""

        return self.__exception

    @property
    def is_faulted(self):
        """Get property"""

        return bool(self.__exception)


class LocationFinder:  # pylint: disable=too-few-public-methods
    """Finds longitude and latitude for an address"""

    def __init__(self):
        self.timeout = 1.0
        self.http = requests
        self.params = urllib.parse.urlencode({'format': 'json', 'addressdetails': 1})
        self.endpoint = 'http://nominatim.openstreetmap.org/search'

    def find(self, address):
        """Sends a query to the location service"""

        url = self.__get_url(address)
        response = self.http.request('GET', url)

        try:
            response.raise_for_status()
            results = response.json()
            if not results:
                raise RuntimeError('Service returned no results')

            addresses = []
            coordinates = []
            for row in results:
                if 'lon' not in row or 'lat' not in row:
                    raise RuntimeError('Geographic system coordinates not returned in results')
                coordinates.append(Coordinates(longitude=row['lon'], latitude=row['lat']))

                if 'address' not in row:
                    raise RuntimeError('Address not returned in results')
                addresses.append(Address(**row['address']))

            if not addresses:
                raise RuntimeError('Service returned no results')
            elif len(addresses) == 1:
                return Result(address=addresses[0], coordinates=coordinates[0])
            else:
                return Result(address=addresses, coordinates=coordinates)
        except (requests.RequestException, RuntimeError) as ex:
            return Result(exception=ex)

    def __get_url(self, address):
        LocationFinder.__precondition_not_none(address.house_number, Address.HOUSE_NUMBER)
        LocationFinder.__precondition_not_none(address.road, Address.ROAD)
        LocationFinder.__precondition_not_none(address.city, Address.CITY)

        if address.country_code:
            return '/'.join([self.endpoint,
                             urllib.parse.quote(address.country_code),
                             urllib.parse.quote(address.city),
                             urllib.parse.quote(address.road),
                             urllib.parse.quote(address.house_number) + '?' + self.params])

        return '/'.join([self.endpoint,
                         urllib.parse.quote('{self.house_number} {self.road}, {self.city}'.format(self=address))
                         + '?' + self.params])

    @staticmethod
    def __precondition_not_none(value, field_name):
        if value is None:
            raise ValueError(field_name + ' is None')
