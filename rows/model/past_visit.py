"""Details a visit in the past"""

import rows.model.object


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
        self.___carer = kwargs.get(PastVisit.CARER, None)
        self.___cancelled = kwargs.get(PastVisit.CANCELLED, None)
        self.___check_in = kwargs.get(PastVisit.CHECK_IN, None)
        self.___check_out = kwargs.get(PastVisit.CHECK_OUT, None)

    def as_dict(self):
        bundle = super(PastVisit, self).as_dict()
        bundle[PastVisit.VISIT] = self.__visit
        bundle[PastVisit.DATE] = self.__date
        bundle[PastVisit.TIME] = self.__time
        bundle[PastVisit.DURATION] = self.__duration
        bundle[PastVisit.CARER] = self.___carer
        bundle[PastVisit.CANCELLED] = self.___cancelled
        bundle[PastVisit.CHECK_IN] = self.___check_in
        bundle[PastVisit.CHECK_OUT] = self.___check_out
        return bundle

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

        return self.___carer

    @property
    def check_in(self):
        """Return a property"""

        return self.___check_in

    @property
    def check_out(self):
        """Return a property"""

        return self.___check_out

    @property
    def cancelled(self):
        """Return a property"""

        return self.___cancelled
