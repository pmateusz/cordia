"""Details an instance of the Home Care Scheduling Problem"""

import datetime
import json

import dateutil.parser

from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.visit import Visit


class Problem(object):
    """Details an instance of the Home Care Scheduling Problem"""

    METADATA = 'metadata'
    CARERS = 'carers'
    VISITS = 'visits'

    class Metadata(object):
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

            return self.__area == other.area \
                   and self.__begin == other.begin \
                   and self.__end == other.end

        def __hash__(self):
            return hash(self.tuple())

        def __str__(self):
            return self.__dict__.__str__()

        def __repr__(self):
            return self.__dict__.__str__()

        def tuple(self):
            """Converts object to tuple"""

            return self.__area.tuple() if self.__area else None, self.__begin, self.__end

        def dict(self):
            """Converts object to dictionary"""

            return {Problem.Metadata.AREA: self.__area,
                    Problem.Metadata.BEGIN: self.__begin,
                    Problem.Metadata.END: self.__end}

        @staticmethod
        def from_json(json_obj):
            """Deserialize object from dictionary"""

            area = None
            area_obj = json_obj.get(Problem.Metadata.AREA, None)
            if area_obj:
                area = Area.from_json(area_obj)

            begin = None
            begin_obj = json_obj.get(Problem.Metadata.BEGIN, None)
            if begin_obj:
                begin = dateutil.parser.parse(begin_obj)
                if begin:
                    begin = begin.date()

            end = None
            end_obj = json_obj.get(Problem.Metadata.END, None)
            if end_obj:
                end = dateutil.parser.parse(end_obj)
                if end:
                    end = end.date()

            return Problem.Metadata(area=area, begin=begin, end=end)

    def __init__(self, **kwargs):
        self.__metadata = kwargs.get(Problem.METADATA, None)
        self.__carers = kwargs.get(Problem.CARERS, None)
        self.__visits = kwargs.get(Problem.VISITS, None)

    def __eq__(self, other):
        if not isinstance(other, Problem):
            return False

        return self.__metadata == other.metadata \
               and self.__carers == other.carers \
               and self.__visits == other.visits

    def __str__(self):
        return self.__dict__.__str__()

    def __repr__(self):
        return self.__dict__.__repr__()

    def to_json(self, stream):
        """Serialize object to a stream"""

        json.dump(self.__dict__, stream, cls=JSONEncoder)

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        metadata_json = json_obj.get(Problem.METADATA, None)
        if metadata_json:
            metadata = Problem.Metadata.from_json(metadata_json)
        else:
            metadata = None

        carers_json = json_obj.get(Problem.CARERS, None)
        carers = [Carer.from_json(carer_json) for carer_json in carers_json] if carers_json else []

        visits_json = json_obj.get(Problem.VISITS, None)
        visits = [Visit.from_json(visit_json) for visit_json in visits_json] if visits_json else []

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
    """Encodes the Problem class in JSON format"""

    UNZIP_CLASSES = [Area, Carer, Visit, Problem.Metadata]

    def default(self, o):  # pylint: disable=method-hidden
        if o.__class__ in JSONEncoder.UNZIP_CLASSES:
            return o.dict()
        if isinstance(o, datetime.date):
            return o.isoformat()
        return json.JSONEncoder.default(self, o)
