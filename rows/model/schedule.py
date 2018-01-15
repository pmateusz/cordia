"""Details visits to carers assignments"""

import rows.model.object


class Schedule(rows.model.object.DataObject):
    """Details visits to carers assignments"""

    METADATA = 'metadata'
    VISITS = 'visits'

    def __init__(self, **kwargs):
        self.__metadata = kwargs.get(Schedule.METADATA, None)
        self.__visits = kwargs.get(Schedule.VISITS, [])

    def as_dict(self):
        bundle = super(Schedule, self).as_dict()

        if self.__metadata:
            bundle[Schedule.METADATA] = self.__metadata

        if self.__visits:
            bundle[Schedule.VISITS] = self.__visits

        return bundle

    @property
    def metadata(self):
        """Get a property"""

        return self.__metadata

    @property
    def visits(self):
        """Get a property"""

        return self.__visits
