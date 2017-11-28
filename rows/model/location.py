"""Coordinates in graphic coordination system"""

import math

import rows.model.plain_object


class Location(rows.model.plain_object.PlainOldDataObject):
    """Coordinates in graphic coordination system"""

    ABSOLUTE_ACCURACY = 0.001

    LONGITUDE = 'longitude'
    LATITUDE = 'latitude'

    def __init__(self, longitude, latitude):
        self.__longitude = longitude
        self.__latitude = latitude

    def __eq__(self, other):
        return isinstance(other, Location) \
               and Location.__is_close(float(self.__longitude), float(other.longitude)) \
               and Location.__is_close(float(self.__latitude), float(other.latitude))

    def as_dict(self):
        bundle = super(Location, self).as_dict()
        bundle[Location.LONGITUDE] = self.__longitude
        bundle[Location.LATITUDE] = self.__latitude
        return bundle

    @staticmethod
    def __is_close(left, right):
        return math.isclose(float(left), float(right), abs_tol=Location.ABSOLUTE_ACCURACY)

    @property
    def longitude(self):
        """Get a property"""

        return self.__longitude

    @property
    def latitude(self):
        """Get a property"""

        return self.__latitude
