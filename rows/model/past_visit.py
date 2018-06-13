"""Details a visit in the past"""

import rows.model.object
import rows.model.carer
import rows.model.visit

import rows.util.parse


class PastVisit(rows.model.object.DatabaseObject):  # pylint: disable=too-many-instance-attributes
    """Details a visit in the past"""

    VISIT = 'visit'
    CARER = 'carer'
    CANCELLED = 'cancelled'
    DATE = 'date'
    TIME = 'time'
    DURATION = 'duration'
    CHECK_IN = 'check_in'
    CHECK_OUT = 'check_out'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(PastVisit, self).__init__()

        self.__visit = kwargs.get(PastVisit.VISIT, None)
        self.__date = kwargs.get(PastVisit.DATE, None)
        self.__time = kwargs.get(PastVisit.TIME, None)
        self.__duration = kwargs.get(PastVisit.DURATION, None)
        self.__carer = kwargs.get(PastVisit.CARER, None)
        self.__cancelled = kwargs.get(PastVisit.CANCELLED, None)
        self.__check_in = kwargs.get(PastVisit.CHECK_IN, None)
        self.__check_out = kwargs.get(PastVisit.CHECK_OUT, None)

    def as_dict(self):
        bundle = super(PastVisit, self).as_dict()
        bundle[PastVisit.VISIT] = self.__visit
        bundle[PastVisit.DATE] = self.__date
        bundle[PastVisit.TIME] = self.__time
        bundle[PastVisit.DURATION] = self.__duration
        bundle[PastVisit.CARER] = self.__carer
        bundle[PastVisit.CANCELLED] = self.__cancelled
        bundle[PastVisit.CHECK_IN] = self.__check_in
        bundle[PastVisit.CHECK_OUT] = self.__check_out
        return bundle

    @staticmethod
    def from_json(json):
        date = None
        json_date = json.get('date')
        if json_date:
            date = rows.util.parse.parse_date(json_date)

        time = None
        json_time = json.get('time')
        if json_time:
            time = rows.util.parse.parse_time(json_time)

        duration = None
        json_duration = json.get('duration')
        if json_duration:
            duration = rows.util.parse.parse_timedelta(json_duration)

        cancelled = json.get('cancelled')

        carer = None
        json_carer = json.get('carer')
        if json_carer:
            carer = rows.model.carer.Carer.from_json(json_carer)

        visit = None
        json_visit = json.get('visit')
        if json_visit:
            visit = rows.model.visit.Visit.from_json(json_visit)

        check_in = None
        json_check_in = json.get('check_in')
        if json_check_in:
            check_in = rows.util.parse.parse_datetime(json_check_in)

        check_out = None
        json_check_out = json.get('check_out')
        if json_check_out:
            check_out = rows.util.parse.parse_datetime(json_check_out)

        return PastVisit(visit=visit,
                         date=date,
                         time=time,
                         duration=duration,
                         carer=carer,
                         cancelled=cancelled,
                         check_in=check_in,
                         check_out=check_out)

    @property
    def visit(self):
        """Return a property"""

        return self.__visit

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
    def carer(self):
        """Return a property"""

        return self.__carer

    @property
    def check_in(self):
        """Return a property"""

        return self.__check_in

    @property
    def check_out(self):
        """Return a property"""

        return self.__check_out

    @property
    def cancelled(self):
        """Return a property"""

        return self.__cancelled
