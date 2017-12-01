"""Details scheduled events for a certain day"""

import rows.model.object
import rows.model.datetime

from rows.model.event import AbsoluteEvent


class Diary(rows.model.object.DataObject):
    """Details scheduled events for a certain day"""

    SCHEDULE_PATTERN_KEY = 'schedule_pattern'
    DATE = 'date'
    EVENTS = 'events'

    def __init__(self, **kwargs):
        super(Diary, self).__init__()

        self.__date = kwargs.get(Diary.DATE, None)
        self.__events = kwargs.get(Diary.EVENTS, None)
        self.__schedule_pattern_key = kwargs.get(Diary.SCHEDULE_PATTERN_KEY, None)

    def __eq__(self, other):
        return isinstance(self, Diary) \
               and self.date == other.date \
               and self.schedule_pattern_key == other.schedule_pattern_key \
               and self.events == other.events

    def as_dict(self):
        bundle = super(Diary, self).as_dict()
        bundle[Diary.DATE] = self.__date
        bundle[Diary.EVENTS] = self.__events
        bundle[Diary.SCHEDULE_PATTERN_KEY] = self.__schedule_pattern_key
        return bundle

    @staticmethod
    def from_json(json):
        """Create object from dictionary"""

        schedule_pattern_key = json.get(Diary.SCHEDULE_PATTERN_KEY)

        json_date = json.get(Diary.DATE)
        date = rows.model.datetime.try_parse_iso_date(json_date) if json_date else None

        events_json = json.get(Diary.EVENTS)
        events = [AbsoluteEvent.from_json(event_json) for event_json in events_json] if events_json else []

        return Diary(**{Diary.SCHEDULE_PATTERN_KEY: schedule_pattern_key,
                        Diary.DATE: date,
                        Diary.EVENTS: events})

    @property
    def date(self):
        """Get a property"""

        return self.__date

    @property
    def events(self):
        """Get a property"""

        return self.__events

    @property
    def schedule_pattern_key(self):
        """Get a property"""

        return self.__schedule_pattern_key
