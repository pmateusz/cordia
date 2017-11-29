"""Details a shift pattern"""

import datetime
import functools

import rows.model.plain_object
import rows.model.day


class ShiftPattern(rows.model.plain_object.PlainOldDatabaseObject):
    """Details a shift pattern"""

    EVENTS = 'events'

    @functools.total_ordering
    class Event(rows.model.plain_object.PlainOldDataObject):
        """Details an event in a shift pattern"""

        WEEK = 'week'
        DAY = 'day'
        BEGIN = 'begin'
        END = 'end'

        def __init__(self, **kwargs):
            super(ShiftPattern.Event, self).__init__()

            self.__week = kwargs.get(ShiftPattern.Event.WEEK, None)
            self.__day = kwargs.get(ShiftPattern.Event.DAY, None)
            self.__begin = kwargs.get(ShiftPattern.Event.BEGIN, None)
            self.__end = kwargs.get(ShiftPattern.Event.END, None)

        def __lt__(self, other):
            if self.__week.__lt__(other.week):
                return True
            if self.__week == other.week:
                if self.__day.__lt__(other.day):
                    return True
                if self.__day == other.day:
                    if self.__begin.__lt__(other.begin):
                        return True
                    if self.__begin == other.begin:
                        return self.__end.__lt__(other.end)
            return False

        def as_dict(self):
            bundle = super(ShiftPattern.Event, self).as_dict()
            bundle[ShiftPattern.Event.WEEK] = self.__week
            bundle[ShiftPattern.Event.DAY] = self.__day
            bundle[ShiftPattern.Event.BEGIN] = self.__begin
            bundle[ShiftPattern.Event.END] = self.__end
            return bundle

        @property
        def week(self):
            """Get a property"""

            return self.__week

        @property
        def day(self):
            """Get a property"""

            return self.__day

        @property
        def begin(self):
            """Get a property"""

            return self.__begin

        @property
        def end(self):
            """Get a property"""

            return self.__end

    def __init__(self, **kwargs):
        super(ShiftPattern, self).__init__(**kwargs)

        self.__events = kwargs.get(ShiftPattern.EVENTS, None)

    def as_dict(self):
        bundle = super(ShiftPattern, self).as_dict()
        bundle[ShiftPattern.EVENTS] = self.__events
        return bundle

    @property
    def events(self):
        """Get a property"""

        return self.__events


class ExecutableShiftPattern(ShiftPattern):
    """Details a shift pattern that is assigned to a carer"""

    REFERENCE_WEEK = 'reference_week'
    REFERENCE_SHIFT_WEEK = 'reference_shift_week'

    class ShiftWeekFactory:
        """Calculates shift week"""

        def __init__(self, reference_week, reference_shift_week, week_span):
            if reference_week.isoweekday() != 7:
                raise ValueError('reference_week must be Sunday')

            if week_span < 1:
                raise ValueError('week_span')
            elif week_span == 1:
                self.__reference_shift_week = 1
                self.__week_span = week_span
            else:
                self.__reference_week = reference_week
                self.__reference_shift_week = reference_shift_week
                self.__week_span = week_span

        def __call__(self, date):
            if self.__week_span <= 1:
                return 1

            date_to_use = date
            if isinstance(date_to_use, datetime.datetime):
                date_to_use = date_to_use.date()

            days_delta = (date_to_use - self.reference_week).days
            weeks_delta = days_delta // 7
            return ((self.__reference_shift_week - 1 + weeks_delta) % self.__week_span) + 1

        @property
        def reference_shift_week(self):
            """Return a property"""

            return self.__reference_shift_week

        @property
        def reference_week(self):
            """Return a property"""

            return self.__reference_week

        @property
        def week_span(self):
            """Return a property"""

            return self.__week_span

    def __init__(self, **kwargs):
        super(ExecutableShiftPattern, self).__init__(**kwargs)

        week = kwargs.get(ExecutableShiftPattern.REFERENCE_WEEK, None)
        shift_week = kwargs.get(ExecutableShiftPattern.REFERENCE_SHIFT_WEEK, None)
        week_span = max(event.week for event in self.events)
        self.__shift_week_fac = ExecutableShiftPattern.ShiftWeekFactory(week, shift_week, week_span)

    def as_dict(self):
        bundle = super(ExecutableShiftPattern, self).as_dict()
        bundle[ExecutableShiftPattern.REFERENCE_WEEK] = self.reference_week
        bundle[ExecutableShiftPattern.REFERENCE_SHIFT_WEEK] = self.reference_shift_week
        return bundle

    def is_available_fully(self, date, time, duration):
        """Returns true if there is a free slot at the requested day and time"""

        effective_day = rows.model.day.from_date(date)
        effective_shift_week = self.__shift_week_fac(date)
        effective_begin = self.__date_to_datetime(date, time)

        effective_end = effective_begin + duration
        if effective_end.date() != date:
            raise ValueError(
                "Time span across multiple days (begin='{0}', end=='{1}')".format(effective_begin, effective_end))

        effective_end_time = effective_end.time()
        for event in self.events:
            if event.week == effective_shift_week and event.day == effective_day and event.begin <= time \
                    and effective_end_time <= event.end:
                return True
        return False

    def is_available_partially(self, begin_date, end_date):
        """Returns true if the is a free slot in the requested date interval"""

        return any(self.__generate_intervals(begin_date, end_date))

    def __generate_intervals(self, begin_date, end_date):
        effective_end = ExecutableShiftPattern.__date_to_datetime(end_date)

        time_step = datetime.timedelta(days=1)
        current_begin = ExecutableShiftPattern.__date_to_datetime(begin_date)
        current_day = rows.model.day.from_date(current_begin.date())
        current_shift_week = self.__shift_week_fac(current_begin.date())

        while current_begin < effective_end:
            for event in self.events:
                if event.week == current_shift_week and event.day == current_day:
                    yield (current_day, event.begin, event.end)

            current_begin += time_step
            current_day = rows.model.day.from_date(current_begin.date())
            current_shift_week = self.__shift_week_fac(current_begin.date())

    @staticmethod
    def __date_to_datetime(date, time=None):
        if time:
            return datetime.datetime(day=date.day, month=date.month, year=date.year,
                                     hour=time.hour, minute=time.minute, second=time.second)
        return datetime.datetime(day=date.day, month=date.month, year=date.year)

    @property
    def reference_week(self):
        """Get a property"""

        return self.__shift_week_fac.reference_week

    @property
    def reference_shift_week(self):
        """Get a property"""

        return self.__shift_week_fac.reference_shift_week
