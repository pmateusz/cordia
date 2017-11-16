"""Postal address of a certain location"""


class Address:
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
        self.__house_number = kwargs.get(Address.HOUSE_NUMBER, None)
        self.__road = kwargs.get(Address.ROAD, None)
        self.__city = kwargs.get(Address.CITY, None)
        self.__country_code = kwargs.get(Address.COUNTRY_CODE, None)
        self.__country = kwargs.get(Address.COUNTRY, None)
        self.__neighbourhood = kwargs.get(Address.NEIGHBOURHOOD, None)
        self.__postcode = kwargs.get(Address.POSTCODE, None)
        self.__state = kwargs.get(Address.STATE, None)
        self.__suburb = kwargs.get(Address.SUBURB, None)

    def __eq__(self, other):
        if not isinstance(other, Address):
            return False

        return self.__house_number == other.house_number and self.__road == other.road and self.__city == other.city \
            and self.__country_code == other.country_code and self.__country == other.country \
            and self.__neighbourhood == other.neighbourhood and self.__postcode == other.postcode \
            and self.__state == other.state and self.__suburb == other.suburb

    def __hash__(self):
        return hash(self.tuple())

    def tuple(self):
        """Returns object as tuple"""

        return (self.__house_number,
                self.__road,
                self.__city,
                self.__country,
                self.__country_code,
                self.__neighbourhood,
                self.__postcode,
                self.__state,
                self.__suburb)

    @property
    def house_number(self):
        """Get property"""

        return self.__house_number

    @property
    def road(self):
        """Get property"""

        return self.__road

    @property
    def city(self):
        """Get property"""

        return self.__city

    @property
    def country(self):
        """Get property"""

        return self.__country

    @property
    def country_code(self):
        """Get property"""

        return self.__country_code

    @property
    def neighbourhood(self):
        """Get property"""

        return self.__neighbourhood

    @property
    def postcode(self):
        """Get property"""

        return self.__postcode

    @property
    def state(self):
        """Get property"""

        return self.__state

    @property
    def suburb(self):
        """Get property"""

        return self.__suburb
