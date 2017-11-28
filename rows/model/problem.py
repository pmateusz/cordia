"""Details an instance of the Home Care Scheduling Problem"""

import datetime
import json

import dateutil.parser

import rows.model.plain_object

from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.visit import Visit


class Problem(rows.model.plain_object.PlainOldDataObject):
    """Details an instance of the Home Care Scheduling Problem"""

    METADATA = 'metadata'
    CARERS = 'carers'
    VISITS = 'visits'

    class Metadata(rows.model.plain_object.PlainOldDataObject):
        """Home Care Scheduling Problem metadata"""

        AREA = 'area'
        BEGIN = 'begin'
        END = 'end'

        def __init__(self, **kwargs):
            super(Problem.Metadata, self).__init__()

            self.__area = kwargs.get(Problem.Metadata.AREA, None)
            self.__begin = kwargs.get(Problem.Metadata.BEGIN, None)
            self.__end = kwargs.get(Problem.Metadata.END, None)

        def as_dict(self):
            bundle = super(Problem.Metadata, self).as_dict()

            if self.__area:
                bundle[Problem.Metadata.AREA] = self.__area.as_dict()

            if self.__begin:
                bundle[Problem.Metadata.BEGIN] = self.__begin

            if self.__end:
                bundle[Problem.Metadata.END] = self.__end

            return bundle

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

        @property
        def area(self):
            """Return a property"""

            return self.__area

        @property
        def begin(self):
            """Return a property"""

            return self.__begin

        @property
        def end(self):
            """Return a property"""

            return self.__end

    def __init__(self, **kwargs):
        super(Problem, self).__init__()

        self.__metadata = kwargs.get(Problem.METADATA, None)
        self.__carers = kwargs.get(Problem.CARERS, None)
        self.__visits = kwargs.get(Problem.VISITS, None)

    def to_json(self, stream):
        """Serialize object to a stream"""

        json.dump(self.as_dict(), stream, cls=JSONEncoder)

    def as_dict(self):
        bundle = super(Problem, self).as_dict()

        if self.__metadata:
            bundle[Problem.METADATA] = self.__metadata

        if self.__carers:
            bundle[Problem.CARERS] = list(self.__carers)

        if self.__visits:
            bundle[Problem.VISITS] = list(self.__visits)

        return bundle

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        metadata_json = json_obj.get(Problem.METADATA, None)
        metadata = Problem.Metadata.from_json(metadata_json) if metadata_json else None

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
            return o.as_dict()
        if isinstance(o, datetime.date):
            return o.isoformat()
        return json.JSONEncoder.default(self, o)
