"""Details relative and absolute events"""

import functools
import rows.model.plain_object


@functools.total_ordering
class RelativeEvent(rows.model.plain_object.PlainOldDataObject):
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


class AbsoluteEvent(rows.model.plain_object.PlainOldDataObject):
    """Details an event with specified date and time"""

    BEGIN = 'begin'
    END = 'end'

    def __init__(self, **kwargs):
        super(AbsoluteEvent, self).__init__()

        self.__begin = kwargs.get(AbsoluteEvent.BEGIN, None)
        self.__end = kwargs.get(AbsoluteEvent.END, None)

    @property
    def begin(self):
        """Get a property"""

        return self.__begin

    @property
    def end(self):
        """Get a property"""

        return self.__end
