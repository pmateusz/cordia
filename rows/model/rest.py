import datetime

import rows.model.object


class Rest(rows.model.object.DatabaseObject):

    def __init__(self, **kwargs):
        super(Rest, self).__init__(**kwargs)

        self.__carer = kwargs.get('carer')
        self.__start_time = kwargs.get('start_time')
        self.__duration = kwargs.get('duration')

    @property
    def carer(self) -> str:
        return self.__carer

    @property
    def start_time(self) -> datetime.datetime:
        return self.__start_time

    @property
    def duration(self) -> datetime.timedelta:
        return self.__duration
