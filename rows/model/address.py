"""Postal address of a certain location"""
import itertools
import re

import rows.model.object


class Address(rows.model.object.DataObject):  # pylint: disable=too-many-instance-attributes
    """Postal address of a certain location"""

    ROAD_WITH_NUMBER_PATTERN = re.compile('^(?P<house_number>\d+)\s+(?P<road>.*)$')
    ALPHA_NUM_PATTERN = re.compile('\w')

    ROAD = 'road'
    HOUSE_NUMBER = 'house_number'
    CITY = 'city'
    COUNTRY_CODE = 'country_code'
    COUNTRY = 'country'
    NEIGHBOURHOOD = 'neighbourhood'
    POST_CODE = 'post_code'
    STATE = 'state'
    SUBURB = 'suburb'

    def __init__(self, **kwargs):
        self.__road = kwargs.get(Address.ROAD, None)
        self.__house_number = kwargs.get(Address.HOUSE_NUMBER, None)
        self.__city = kwargs.get(Address.CITY, None)
        self.__country_code = kwargs.get(Address.COUNTRY_CODE, None)
        self.__country = kwargs.get(Address.COUNTRY, None)
        self.__neighbourhood = kwargs.get(Address.NEIGHBOURHOOD, None)
        self.__postcode = kwargs.get(Address.POST_CODE, None)
        self.__state = kwargs.get(Address.STATE, None)
        self.__suburb = kwargs.get(Address.SUBURB, None)

    def __eq__(self, other):
        return isinstance(other, Address) \
               and self.road == other.road \
               and self.house_number == other.house_number \
               and self.city == other.city \
               and self.country_code == other.country_code \
               and self.country == other.country \
               and self.neighbourhood == other.neighbourhood \
               and self.post_code == other.post_code \
               and self.state == other.state \
               and self.suburb == other.suburb

    def __hash__(self):
        return hash((self.road,
                     self.house_number,
                     self.city,
                     self.country_code,
                     self.country,
                     self.neighbourhood,
                     self.post_code,
                     self.state,
                     self.suburb))

    def as_dict(self):
        bundle = super(Address, self).as_dict()
        if self.__road:
            bundle[Address.ROAD] = self.__road
        if self.__house_number:
            bundle[Address.HOUSE_NUMBER] = self.__house_number
        if self.__city:
            bundle[Address.CITY] = self.__city
        if self.__postcode:
            bundle[Address.POST_CODE] = self.__postcode
        if self.__country_code:
            bundle[Address.COUNTRY_CODE] = self.__country_code
        if self.__country:
            bundle[Address.COUNTRY] = self.__country
        if self.__neighbourhood:
            bundle[Address.NEIGHBOURHOOD] = self.__neighbourhood
        if self.__state:
            bundle[Address.STATE] = self.__state
        if self.__suburb:
            bundle[Address.SUBURB] = self.__suburb
        return bundle

    @staticmethod
    def from_json(json):
        """Create object from dictionary"""

        return Address(**json)

    @property
    def road(self):
        """Return a property"""

        return self.__road

    @property
    def house_number(self):
        """Get a property"""

        return self.__house_number

    @house_number.setter
    def house_number(self, value):
        self.__house_number = value

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
    def post_code(self):
        """Get a property"""

        return self.__postcode

    @post_code.setter
    def post_code(self, value):
        """Set a property"""

        self.__postcode = value

    @property
    def state(self):
        """Get a property"""

        return self.__state

    @property
    def suburb(self):
        """Get a property"""

        return self.__suburb

    @staticmethod
    def __parse_from_csv(text):
        address_default_matcher = re.compile(
            r'^(?P<house_number>\d+?)\s*,\s*(?P<road>(?:\w+\s+)*\w+)\s*,\s*(?P<city>(?:\w+\s+)*\w+)$')
        address_backup_matcher = re.compile(
            r'^(?P<house_number>\d+?)\s+(?P<road>(?:\w+\s+)*\w+)\s*,\s*(?P<city>(?:\w+\s+)*\w+)$')
        street_matcher = re.compile(r'^(?P<house_number>\d+)\s+(?P<road>.*)$')
        match = address_default_matcher.match(text_to_use)
        if not match:
            match = address_backup_matcher.match(text_to_use)

        if match:
            return Address(house_number=match.group('house_number').strip(),
                           road=match.group('road').strip(),
                           city=match.group('city').strip().capitalize())
        else:
            chunks = text_to_use.split(',')
            if len(chunks) < 2:
                # no unequivocal way to parse input
                return None
            raw_house_number = chunks[0:-2]
            raw_road = chunks[-2].strip()
            city = chunks[-1].strip()

            match = street_matcher.match(raw_road)
            if match:
                raw_house_number.append(match.group('house_number'))
                raw_road = match.group('road')

            house_number = ' '.join([part.strip() for part in raw_house_number])
            road = raw_road
            city = city.capitalize()

            return Address(house_number=house_number, road=road, city=city)

    @staticmethod
    def parse(text):
        """Parses input text in natural language to an address"""

        text_to_use = text.strip()
        parts = [part.strip() for part in text_to_use.split(',') if Address.ALPHA_NUM_PATTERN.search(part)]

        if len(parts) < 3:
            raise ValueError(text_to_use)

        post_code = parts[-1]
        city = parts[-2]
        road = parts[-3]
        house_number = ''

        digit_pattern = re.compile('\d+')
        # how many of them are plain numbers >= 1 select is as the house number
        # none => extract all numbers and select last one as household
        house_number_parts = parts[0: len(parts) - 3]

        if house_number_parts:
            plain_numbers = [part for part in house_number_parts if part.isdigit()]
            if plain_numbers:
                house_number = plain_numbers[-1]
            else:
                matcher = Address.ROAD_WITH_NUMBER_PATTERN.match(road)
                if matcher:
                    road = matcher.group('road')
                    house_number = matcher.group('house_number')
                else:
                    numbers = list(itertools.chain((digit_pattern.findall(part) for part in house_number_parts)))
                    if numbers:
                        house_number = numbers[-1]
        else:
            # house number is most probably appended to the road
            matcher = Address.ROAD_WITH_NUMBER_PATTERN.match(road)
            if matcher:
                road = matcher.group('road')
                house_number = matcher.group('house_number')

        return Address(post_code=post_code, city=city, road=road, house_number=house_number)
