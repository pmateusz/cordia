"""Details a requested visit"""

import rows.model.plain_object


class Visit(rows.model.plain_object.PlainOldDatabaseObject):
    """Details a requested visit"""

    SERVICE_USER = 'service_user'
    ADDRESS = 'address'
    DATE = 'date'
    TIME = 'time'
    DURATION = 'duration'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Visit, self).__init__()

        self.__service_user = kwargs.get(Visit.SERVICE_USER, None)
        self.__date = kwargs.get(Visit.DATE, None)
        self.__time = kwargs.get(Visit.TIME, None)
        self.__duration = kwargs.get(Visit.DURATION, None)
        self.__address = kwargs.get(Visit.ADDRESS, None)

    def as_dict(self):
        bundle = super(Visit, self).as_dict()
        bundle[Visit.SERVICE_USER] = self.__service_user
        bundle[Visit.ADDRESS] = self.__address
        bundle[Visit.DATE] = self.__date
        bundle[Visit.TIME] = self.__time
        bundle[Visit.DURATION] = self.__duration
        return bundle

    @staticmethod
    def from_json(json_obj):
        """Create object from dictionary"""

        return Visit(**json_obj)

    @property
    def service_user(self):
        """Return a property"""

        return self.__service_user

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

    @property
    def address(self):
        """Return a property"""

        return self.__address
