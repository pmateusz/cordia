"""Details an instance of the Home Care Scheduling Problem"""
import datetime
import typing

import rows.model.datetime
import rows.model.object
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.metadata import Metadata
from rows.model.service_user import ServiceUser


class Problem(rows.model.object.DataObject):
    """Details an instance of the Home Care Scheduling Problem"""

    METADATA = 'metadata'
    DATA = 'data'
    CARERS = 'carers'
    VISITS = 'visits'
    SERVICE_USERS = 'service_users'

    class CarerShift(rows.model.object.DataObject):
        """Carers shift details"""

        CARER = 'carer'
        DIARIES = 'diaries'

        def __init__(self, **kwargs):
            super(Problem.CarerShift, self).__init__()

            self.__carer = kwargs.get(Problem.CarerShift.CARER, None)
            self.__diaries = kwargs.get(Problem.CarerShift.DIARIES, None)

        def __eq__(self, other):
            return isinstance(other, Problem.CarerShift) \
                   and self.carer == other.carer \
                   and self.diaries == other.diaries

        def as_dict(self):
            bundle = super(Problem.CarerShift, self).as_dict()
            bundle[Problem.CarerShift.CARER] = self.__carer
            bundle[Problem.CarerShift.DIARIES] = self.__diaries
            return bundle

        @property
        def carer(self):
            """Return a property"""

            return self.__carer

        @property
        def diaries(self):
            """Return a property"""

            return self.__diaries

        @staticmethod
        def from_json(json):
            """Create object from dictionary"""

            carer_json = json.get(Problem.CarerShift.CARER, None)
            carer = Carer.from_json(carer_json) if carer_json else None

            json_diaries = json.get(Problem.CarerShift.DIARIES, None)
            diaries = [Diary.from_json(json_diary) for json_diary in json_diaries] if json_diaries else []

            return Problem.CarerShift(**{Problem.CarerShift.CARER: carer, Problem.CarerShift.DIARIES: diaries})

    class LocalVisit(rows.model.object.DatabaseObject):
        """Visit to be performed at the known location"""

        DATE = 'date'
        TIME = 'time'
        TASKS = 'tasks'
        DURATION = 'duration'
        CARER_COUNT = 'carer_count'
        SERVICE_USER = 'service_user'

        def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
            super(Problem.LocalVisit, self).__init__(**kwargs)

            self.__date = kwargs.get(Problem.LocalVisit.DATE, None)
            self.__time = kwargs.get(Problem.LocalVisit.TIME, None)
            self.__duration = kwargs.get(Problem.LocalVisit.DURATION, None)
            self.__tasks = kwargs.get(Problem.LocalVisit.TASKS, None)
            self.__service_user = kwargs.get(Problem.LocalVisit.SERVICE_USER, None)
            self.__carer_count = kwargs.get(Problem.LocalVisit.CARER_COUNT, None)

        def __eq__(self, other):
            if not isinstance(other, Problem.LocalVisit):
                return False
            return self.__date == other.date and self.__time == other.time and self.__duration == other.duration and self.__tasks == other.tasks \
                   and self.__service_user == other.service_user

        def __hash__(self):
            return hash(tuple([self.__date, self.__time, self.__duration, self.__service_user, self.__carer_count]))

        def as_dict(self):
            bundle = super(Problem.LocalVisit, self).as_dict()
            bundle[Problem.LocalVisit.DATE] = self.__date
            bundle[Problem.LocalVisit.TIME] = self.__time
            bundle[Problem.LocalVisit.DURATION] = self.__duration
            bundle[Problem.LocalVisit.CARER_COUNT] = self.__carer_count
            bundle[Problem.LocalVisit.TASKS] = self.__tasks
            # ignore the service user
            return bundle

        @staticmethod
        def from_json(json):
            """Create object from dictionary"""

            key = json.get(Problem.LocalVisit.KEY, None)

            date_json = json.get(Problem.LocalVisit.DATE, None)
            date = rows.model.datetime.try_parse_iso_date(date_json) if date_json else None

            time_json = json.get(Problem.LocalVisit.TIME, None)
            time = rows.model.datetime.try_parse_iso_time(time_json) if time_json else None

            duration_json = json.get(Problem.LocalVisit.DURATION, None)
            duration = rows.model.datetime.try_parse_duration(duration_json) if duration_json else None

            carer_count = json.get(Problem.LocalVisit.CARER_COUNT, None)

            tasks = json.get(Problem.LocalVisit.TASKS, [])

            return Problem.LocalVisit(**{Problem.LocalVisit.KEY: key,
                                         Problem.LocalVisit.DATE: date,
                                         Problem.LocalVisit.TIME: time,
                                         Problem.LocalVisit.DURATION: duration,
                                         Problem.LocalVisit.CARER_COUNT: carer_count,
                                         Problem.LocalVisit.TASKS: tasks})

        @property
        def service_user(self):
            """Return a property"""

            return self.__service_user

        @service_user.setter
        def service_user(self, value):
            self.__service_user = value

        @property
        def date(self):
            """Return a property"""

            return self.__date

        @property
        def time(self):
            """Return a property"""

            return self.__time

        @property
        def duration(self) -> datetime.timedelta:
            """Return a property"""

            return self.__duration

        @duration.setter
        def duration(self, value: datetime.timedelta):
            """Set a property"""

            self.__duration = value

        @property
        def tasks(self) -> typing.List[int]:
            """Return a property"""

            return self.__tasks

        @property
        def carer_count(self):
            """Return a property"""

            return self.__carer_count

    class LocalVisits(rows.model.object.DataObject):
        """Groups visits to be performed at the same location"""

        SERVICE_USER = 'service_user'
        VISITS = 'visits'

        def __init__(self, **kwargs):
            self.__service_user = kwargs.get(Problem.LocalVisits.SERVICE_USER, None)
            self.__visits = kwargs.get(Problem.LocalVisits.VISITS, [])

        def as_dict(self):
            bundle = super(Problem.LocalVisits, self).as_dict()
            bundle[Problem.LocalVisits.SERVICE_USER] = self.__service_user
            bundle[Problem.LocalVisits.VISITS] = self.__visits
            return bundle

        @property
        def service_user(self):
            """Return a property"""

            return self.__service_user

        @property
        def visits(self) -> 'typing.List[Problem.LocalVisit]':
            """Return a property"""

            return self.__visits

        @staticmethod
        def from_json(json):
            """Create object from dictionary"""

            service_user = int(json.get(Problem.LocalVisits.SERVICE_USER))

            visits_json = json.get(Problem.LocalVisits.VISITS)
            visits = [Problem.LocalVisit.from_json(visit_json) for visit_json in visits_json] if visits_json else []

            return Problem.LocalVisits(service_user=service_user, visits=visits)

    def __init__(self, **kwargs):
        super(Problem, self).__init__()

        self.__metadata = kwargs.get(Problem.METADATA, None)
        self.__carers = kwargs.get(Problem.CARERS, None)
        self.__visits = kwargs.get(Problem.VISITS, None)
        self.__service_users = kwargs.get(Problem.SERVICE_USERS, None)

    def as_dict(self):
        bundle = super(Problem, self).as_dict()

        if self.__metadata:
            bundle[Problem.METADATA] = self.__metadata

        if self.__carers:
            bundle[Problem.CARERS] = self.__carers

        if self.__visits:
            bundle[Problem.VISITS] = self.__visits

        if self.__service_users:
            bundle[Problem.SERVICE_USERS] = self.__service_users

        return bundle

    def get_diary(self, carer: Carer, date: datetime.date) -> typing.Optional[Diary]:
        for carer_shift in self.__carers:
            if carer_shift.carer.sap_number != carer.sap_number:
                continue

            for diary in carer_shift.diaries:
                if diary.date == date:
                    return diary
        return None

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        metadata_json = json_obj.get(Problem.METADATA, None)
        metadata = Metadata.from_json(metadata_json) if metadata_json else None

        carers_json = json_obj.get(Problem.CARERS, None)
        carers = [Problem.CarerShift.from_json(json) for json in carers_json] if carers_json else []

        visits_json = json_obj.get(Problem.VISITS, None)
        visits = [Problem.LocalVisits.from_json(json) for json in visits_json] if visits_json else []

        service_users_json = json_obj.get(Problem.SERVICE_USERS, None)
        service_users = [ServiceUser.from_json(json) for json in service_users_json] if service_users_json else []
        return Problem(metadata=metadata, carers=carers, visits=visits, service_users=service_users)

    @property
    def metadata(self):
        """Get a property"""

        return self.__metadata

    @property
    def carers(self):
        """Get a property"""

        return self.__carers

    @property
    def visits(self) -> typing.List[LocalVisits]:
        """Get a property"""

        return self.__visits

    @property
    def service_users(self):
        """Get a property"""

        return self.__service_users
