"""Details an instance of the Home Care Scheduling Problem"""

import datetime
import json

from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.visit import Visit


class Problem:
    """Details an instance of the Home Care Scheduling Problem"""

    METADATA = 'metadata'
    CARERS = 'carers'
    VISITS = 'visits'

    class Metadata:
        """Home Care Scheduling Problem metadata"""

        AREA = 'area'
        BEGIN = 'begin'
        END = 'end'

        def __init__(self, **kwargs):
            self.__area = kwargs.get(Problem.Metadata.AREA, None)
            self.__begin = kwargs.get(Problem.Metadata.BEGIN, None)
            self.__end = kwargs.get(Problem.Metadata.END, None)

        def __eq__(self, other):
            if not isinstance(other, Problem.Metadata):
                return False

            return self.__area == other.__area \
                   and self.__begin == other.__begin \
                   and self.__end == other.__end

        def __hash__(self):
            return hash(self.tuple())

        def __str__(self):
            return self.dict().__str__()

        def __repr__(self):
            return self.dict().__str__()

        def tuple(self):
            return self.__area.tuple() if self.__area else None, self.__begin, self.__end

        def dict(self):
            return {Problem.Metadata.AREA: self.__area,
                    Problem.Metadata.BEGIN: self.__begin,
                    Problem.Metadata.END: self.__end}

        @staticmethod
        def from_json(json_obj):
            area = None
            area_obj = json_obj.get(Problem.Metadata.AREA, None)
            if area_obj:
                area = Area.from_json(area_obj)
            return Problem.Metadata(area=area,
                                    begin=json_obj.get(Problem.Metadata.BEGIN),
                                    end=json_obj.get(Problem.Metadata.END))

    def __init__(self, **kwargs):
        self.__metadata = kwargs.get(Problem.METADATA, None)
        self.__carers = kwargs.get(Problem.CARERS, None)
        self.__visits = kwargs.get(Problem.VISITS, None)

    def __eq__(self, other):
        if not isinstance(other, Problem):
            return False

        return self.__metadata == other.__metadata \
               and self.__carers == other.__carers \
               and self.__visits == other.__visits

    def __hash__(self):
        return hash(self.tuple())

    def __str__(self):
        return self.dict().__str__()

    def __repr__(self):
        return self.dict().__repr__()

    def tuple(self):
        """Get object as tuple"""

        return self.__metadata, self.__carers, self.__visits

    def dict(self):
        """Get object as dictionary"""

        return {Problem.METADATA: self.__metadata,
                Problem.CARERS: self.__carers,
                Problem.VISITS: self.__visits}

    def to_json(self, stream):
        """Serialize object to a stream"""

        json.dump(self.dict(), stream, cls=JSONEncoder)

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from a stream"""

        metadata = None
        carers = []
        visits = []

        metadata_json = json_obj.get(Problem.METADATA, None)
        if metadata_json:
            metadata = Problem.Metadata.from_json(metadata_json)

        return Problem(metadata=metadata, carers=carers, visits=visits)

    @property
    def metadata(self):
        """Get a property"""

        return self.__metadata

    @property
    def carers(self):
        """Get a property"""

        return self.__carers

    @property
    def visits(self):
        """Get a property"""

        return self.__visits


class JSONEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, datetime.date):
            return obj.isoformat()
        elif isinstance(obj, Area):
            return obj.dict()
        elif isinstance(obj, Carer):
            return obj.dict()
        elif isinstance(obj, Visit):
            return obj.dict()
        elif isinstance(obj, Problem.Metadata):
            return obj.dict()
        return json.JSONEncoder.default(self, obj)
