"""Details a shift pattern"""

import datetime
import functools
import logging

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

    def __init__(self, **kwargs):
        super(ExecutableShiftPattern, self).__init__(**kwargs)

        self.__reference_week = kwargs.get(ExecutableShiftPattern.REFERENCE_WEEK, None)
        self.__reference_shift_week = kwargs.get(ExecutableShiftPattern.REFERENCE_SHIFT_WEEK, None)
        self.__week_span = max(event.week for event in self.events)

        if self.__week_span < 1:
            raise ValueError()

    def as_dict(self):
        bundle = super(ExecutableShiftPattern, self).as_dict()
        bundle[ExecutableShiftPattern.REFERENCE_WEEK] = self.__reference_week
        bundle[ExecutableShiftPattern.REFERENCE_SHIFT_WEEK] = self.__reference_shift_week
        return bundle

    def is_available(self, date, time, duration):
        """Returns true if there is a free slot at the requested day and time"""

        effective_day = rows.model.day.from_iso_weekday(date.isoweekday())
        effective_shift_week = self.get_effective_shift_week(date)

        effective_begin = datetime.datetime(day=date.day, month=date.month, year=date.year,
                                            hour=time.hour, minute=time.minute, second=time.second)

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

    def get_effective_shift_week(self, date):
        """Returns effective week number for given date"""

        if self.__week_span <= 1:
            return 1

        if date < self.reference_week:
            logging.warning("Date '%s' is smaller than reference week '%s'", date, self.reference_week)

        days_delta = (date - self.reference_week).days
        weeks_delta = days_delta // 7
        effective_week_number = (self.reference_shift_week - 1 + weeks_delta) % self.__week_span
        return effective_week_number + 1

    @property
    def reference_week(self):
        """Get a property"""

        return self.__reference_week

    @property
    def reference_shift_week(self):
        """Get a property"""

        return self.__reference_shift_week
