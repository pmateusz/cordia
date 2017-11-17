"""Coordinates in graphic coordination system"""

import math


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

        return Coordinates.__is_close(float(self.__longitude), float(other.longitude)) \
            and Coordinates.__is_close(float(self.__latitude), float(other.latitude))

    def __hash__(self):
        return hash(self.tuple())

    def __repr__(self):
        return self.tuple().__repr__()

    def __str__(self):
        return "({self.longitude}, {self.latitude})".format(self=self)

    @staticmethod
    def __is_close(left, right):
        return math.isclose(float(left), float(right), abs_tol=Coordinates.ABSOLUTE_ACCURACY)
