"""Details a day of a week"""


class Day:
    """Details a day of a week"""

    def __init__(self, iso_number, name, short_name):
        self.__iso_number = iso_number
        self.__name = name
        self.__short_name = short_name

    def __eq__(self, other):
        return isinstance(other, Day) and self.__iso_number == other.iso_number

    def __lt__(self, other):
        return self.__iso_number.__lt__(other.iso_number)

    def __hash__(self):
        return self.__iso_number

    def __str__(self):
        return self.__name

    def __repr__(self):
        return str(self.__iso_number)

    @property
    def iso_number(self):
        """Get a property"""

        return self.__iso_number

    @property
    def name(self):
        """Get a property"""

        return self.__name

    @property
    def short_name(self):
        """Get a property"""

        return self.__short_name


MONDAY = Day(1, 'Monday', 'MON')
TUESDAY = Day(2, 'Tuesday', 'TUE')
WEDNESDAY = Day(3, 'Wednesday', 'WED')
THURSDAY = Day(4, 'Thursday', 'THU')
FRIDAY = Day(5, 'Friday', 'FRI')
SATURDAY = Day(6, 'Saturday', 'SAT')
SUNDAY = Day(7, 'Sunday', 'SUN')
DAYS = [MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY, SUNDAY]


def from_short_name(text):
    """Parse input text to a day"""

    short_name = text.strip().upper()
    for day in DAYS:
        if short_name == day.short_name:
            return day
    return None


def from_date(date):
    """Convert input number to a day"""

    return DAYS[date.isoweekday() - 1]
