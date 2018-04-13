"""Details relative and absolute events"""

import functools

import rows.model.datetime
import rows.model.object


@functools.total_ordering
class RelativeEvent(rows.model.object.DataObject):
    """Details an recurring event"""

    WEEK = 'week'
    DAY = 'day'
    BEGIN = 'begin'
    END = 'end'

    def __init__(self, **kwargs):
        super(RelativeEvent, self).__init__()

        self.__week = kwargs.get(RelativeEvent.WEEK, None)
        self.__day = kwargs.get(RelativeEvent.DAY, None)
        self.__begin = kwargs.get(RelativeEvent.BEGIN, None)
        self.__end = kwargs.get(RelativeEvent.END, None)

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
        bundle = super(RelativeEvent, self).as_dict()
        bundle[RelativeEvent.WEEK] = self.__week
        bundle[RelativeEvent.DAY] = self.__day
        bundle[RelativeEvent.BEGIN] = self.__begin
        bundle[RelativeEvent.END] = self.__end
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


class AbsoluteEvent(rows.model.object.DataObject):
    """Details an event with specified date and time"""

    BEGIN = 'begin'
    END = 'end'

    def __init__(self, **kwargs):
        super(AbsoluteEvent, self).__init__()

        self.__begin = kwargs.get(AbsoluteEvent.BEGIN, None)
        self.__end = kwargs.get(AbsoluteEvent.END, None)

    def __eq__(self, other):
        return isinstance(other, AbsoluteEvent) and self.begin == other.begin and self.end == other.end

    @staticmethod
    def from_json(json):
        """Create object from dictionary"""

        begin_json = json.get(AbsoluteEvent.BEGIN)
        begin = rows.model.datetime.try_parse_iso_datetime(begin_json) if begin_json else None

        end_json = json.get(AbsoluteEvent.END)
        end = rows.model.datetime.try_parse_iso_datetime(end_json) if end_json else None

        return AbsoluteEvent(**{AbsoluteEvent.BEGIN: begin, AbsoluteEvent.END: end})

    def as_dict(self):
        bundle = super(AbsoluteEvent, self).as_dict()
        bundle[AbsoluteEvent.BEGIN] = self.__begin
        bundle[AbsoluteEvent.END] = self.__end
        return bundle

    def contains(self, other):
        return self.begin <= other.begin and other.end <= self.end

    def overlaps(self, other):
        return self != other \
               and ((self.begin >= other.begin and other.end <= self.end)
                    or (self.begin <= other.begin and other.end >= self.end))

    @property
    def begin(self):
        """Get a property"""

        return self.__begin

    @property
    def end(self):
        """Get a property"""

        return self.__end

    @property
    def duration(self):
        """Get a property"""
        return self.end - self.begin
