"""Details a requested visit"""

import rows.model.object
import rows.model.datetime

from rows.model.address import Address


class Visit(rows.model.object.DatabaseObject):
    """Details a requested visit"""

    SERVICE_USER = 'service_user'
    ADDRESS = 'address'
    DATE = 'date'
    TIME = 'time'
    DURATION = 'duration'
    CARER_COUNT = 'carer_count'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Visit, self).__init__()

        self.__service_user = kwargs.get(Visit.SERVICE_USER, None)
        self.__date = kwargs.get(Visit.DATE, None)
        self.__time = kwargs.get(Visit.TIME, None)
        self.__duration = kwargs.get(Visit.DURATION, None)
        self.__carer_count = kwargs.get(Visit.CARER_COUNT, 1)
        self.__address = kwargs.get(Visit.ADDRESS, None)

    def as_dict(self):
        bundle = super(Visit, self).as_dict()
        bundle[Visit.SERVICE_USER] = self.__service_user
        bundle[Visit.DATE] = self.__date
        bundle[Visit.TIME] = self.__time
        bundle[Visit.DURATION] = self.__duration
        bundle[Visit.CARER_COUNT] = self.__carer_count
        bundle[Visit.ADDRESS] = self.__address
        return bundle

    @staticmethod
    def from_json(json):
        """Create object from dictionary"""

        user_id = int(json.get(Visit.SERVICE_USER, None))

        address_json = json.get(Visit.ADDRESS, None)
        address = Address.from_json(address_json) if address_json else None

        date_json = json.get(Visit.DATE, None)
        date = rows.model.datetime.try_parse_iso_date(date_json) if date_json else None

        time_json = json.get(Visit.TIME, None)
        time = rows.model.datetime.try_parse_iso_time(time_json) if time_json else None

        duration_json = json.get(Visit.DURATION, None)
        duration = rows.model.datetime.try_parse_duration(duration_json) if duration_json else None

        return Visit(**{Visit.KEY: json.get(Visit.KEY, None),
                        Visit.SERVICE_USER: user_id,
                        Visit.DATE: date,
                        Visit.TIME: time,
                        Visit.DURATION: duration,
                        Visit.CARER_COUNT: 1,
                        Visit.ADDRESS: address})

    @property
    def service_user(self):
        """Return a property"""

        return self.__service_user

    @service_user.setter
    def service_user(self, value):
        """Set a property"""

        self.__service_user = value

    @property
    def carer_count(self):
        """Return a property"""

        return self.__carer_count

    @carer_count.setter
    def carer_count(self, value):
        self.__carer_count = value

    @property
    def date(self):
        """Return a property"""

        return self.__date

    @property
    def time(self):
        """Return a property"""

        return self.__time

    @property
    def duration(self):
        """Return a property"""

        return self.__duration

    @duration.setter
    def duration(self, value):
        """Set a property"""

        self.__duration = value

    @property
    def address(self):
        """Return a property"""

        return self.__address

    @address.setter
    def address(self, value):
        self.__address = value
