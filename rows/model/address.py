"""Postal address of a certain location"""

import rows.model.plain_object


class Address(rows.model.plain_object.PlainOldDataObject):  # pylint: disable=too-many-instance-attributes
    """Postal address of a certain location"""

    ROAD = 'road'
    HOUSE_NUMBER = 'house_number'
    CITY = 'city'
    COUNTRY_CODE = 'country_code'
    COUNTRY = 'country'
    NEIGHBOURHOOD = 'neighbourhood'
    POSTCODE = 'postcode'
    STATE = 'state'
    SUBURB = 'suburb'

    def __init__(self, **kwargs):
        self.__road = kwargs.get(Address.ROAD, None)
        self.__house_number = kwargs.get(Address.HOUSE_NUMBER, None)
        self.__city = kwargs.get(Address.CITY, None)
        self.__country_code = kwargs.get(Address.COUNTRY_CODE, None)
        self.__country = kwargs.get(Address.COUNTRY, None)
        self.__neighbourhood = kwargs.get(Address.NEIGHBOURHOOD, None)
        self.__postcode = kwargs.get(Address.POSTCODE, None)
        self.__state = kwargs.get(Address.STATE, None)
        self.__suburb = kwargs.get(Address.SUBURB, None)

    def as_dict(self):
        bundle = super(Address, self).as_dict()
        if self.__road:
            bundle[Address.ROAD] = self.__road
        if self.__house_number:
            bundle[Address.HOUSE_NUMBER] = self.__house_number
        if self.__city:
            bundle[Address.CITY] = self.__city
        if self.__country_code:
            bundle[Address.COUNTRY_CODE] = self.__country_code
        if self.__country:
            bundle[Address.COUNTRY] = self.__country
        if self.__neighbourhood:
            bundle[Address.NEIGHBOURHOOD] = self.__neighbourhood
        if self.__postcode:
            bundle[Address.POSTCODE] = self.__postcode
        if self.__state:
            bundle[Address.STATE] = self.__state
        if self.__suburb:
            bundle[Address.SUBURB] = self.__suburb
        return bundle

    @property
    def road(self):
        """Return a property"""

        return self.__road

    @property
    def house_number(self):
        """Get a property"""

        return self.__house_number

    @property
    def city(self):
        """Get a property"""

        return self.__city

    @property
    def country_code(self):
        """Get a property"""

        return self.__country_code

    @property
    def country(self):
        """Get a property"""

        return self.__country

    @property
    def neighbourhood(self):
        """Get a property"""

        return self.__neighbourhood

    @property
    def postcode(self):
        """Get a property"""

        return self.__postcode

    @property
    def state(self):
        """Get a property"""

        return self.__state

    @property
    def suburb(self):
        """Get a property"""

        return self.__suburb
