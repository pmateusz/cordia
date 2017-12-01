"""Coordinates in graphic coordination system"""

import math

import rows.model.object


class Location(rows.model.object.DataObject):
    """Coordinates in graphic coordination system"""

    ABSOLUTE_ACCURACY = 0.001

    LONGITUDE = 'longitude'
    LATITUDE = 'latitude'

    def __init__(self, **kwargs):
        self.__latitude = kwargs.get(Location.LATITUDE, None)
        self.__longitude = kwargs.get(Location.LONGITUDE, None)

    def __eq__(self, other):
        return isinstance(other, Location) \
               and Location.__is_close(float(self.__latitude), float(other.latitude)) \
               and Location.__is_close(float(self.__longitude), float(other.longitude))

    def as_dict(self):
        bundle = super(Location, self).as_dict()
        bundle[Location.LATITUDE] = self.__latitude
        bundle[Location.LONGITUDE] = self.__longitude
        return bundle

    @staticmethod
    def from_json(json):
        """Creates object from dictionary"""

        latitude = json.get(Location.LATITUDE)
        longitude = json.get(Location.LONGITUDE)

        return Location(**{Location.LONGITUDE: longitude, Location.LATITUDE: latitude})

    @staticmethod
    def __is_close(left, right):
        return math.isclose(float(left), float(right), abs_tol=Location.ABSOLUTE_ACCURACY)

    @property
    def latitude(self):
        """Get a property"""

        return self.__latitude

    @property
    def longitude(self):
        """Get a property"""

        return self.__longitude
