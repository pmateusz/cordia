"""Details an instance of the Home Care Scheduling Problem"""
import distutils.version

import dateutil.parser

import rows.model.object
from rows.model.address import Address

from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.location import Location
from rows.model.visit import Visit


class Problem(rows.model.object.DataObject):
    """Details an instance of the Home Care Scheduling Problem"""

    METADATA = 'metadata'
    DATA = 'data'
    CARERS = 'carers'
    VISITS = 'visits'

    class Metadata(rows.model.object.DataObject):
        """Home Care Scheduling Problem metadata"""

        AREA = 'area'
        BEGIN = 'begin'
        END = 'end'
        VERSION = 'version'

        def __init__(self, **kwargs):
            super(Problem.Metadata, self).__init__()

            self.__version = kwargs.get(Problem.Metadata.VERSION, None)
            self.__area = kwargs.get(Problem.Metadata.AREA, None)
            self.__begin = kwargs.get(Problem.Metadata.BEGIN, None)
            self.__end = kwargs.get(Problem.Metadata.END, None)

        def as_dict(self):
            """Create object from dictionary"""

            bundle = super(Problem.Metadata, self).as_dict()

            if self.__version:
                bundle[Problem.Metadata.VERSION] = self.__version

            if self.__area:
                bundle[Problem.Metadata.AREA] = self.__area

            if self.__begin:
                bundle[Problem.Metadata.BEGIN] = self.__begin

            if self.__end:
                bundle[Problem.Metadata.END] = self.__end

            return bundle

        @staticmethod
        def from_json(json_obj):
            """Deserialize object from dictionary"""

            version = distutils.version.StrictVersion()
            version_obj = json_obj.get(Problem.Metadata.VERSION, None)
            if version_obj:
                version.parse(version_obj)

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

            return Problem.Metadata(area=area, begin=begin, end=end, version=version)

        @property
        def version(self):
            """Get a property"""

            return self.__version

        @property
        def area(self):
            """Get a property"""

            return self.__area

        @property
        def begin(self):
            """Get a property"""

            return self.__begin

        @property
        def end(self):
            """Get a property"""

            return self.__end

    class CarerShift(rows.model.object.DataObject):
        """Carers shift details"""

        CARER = 'carer'
        DIARIES = 'diaries'

        def __init__(self, **kwargs):
            super(Problem.CarerShift, self).__init__()

            self.__carer = kwargs.get(Problem.CarerShift.CARER, None)
            self.__diaries = kwargs.get(Problem.CarerShift.DIARIES, None)

        def __eq__(self, other):
            return isinstance(other, Problem.CarerShift) \
                   and self.carer == other.carer \
                   and self.diaries == other.diaries

        def as_dict(self):
            bundle = super(Problem.CarerShift, self).as_dict()
            bundle[Problem.CarerShift.CARER] = self.__carer
            bundle[Problem.CarerShift.DIARIES] = self.__diaries
            return bundle

        @property
        def carer(self):
            """Return a property"""

            return self.__carer

        @property
        def diaries(self):
            """Return a property"""

            return self.__diaries

        @staticmethod
        def from_json(json):
            """Create object from dictionary"""

            carer_json = json.get(Problem.CarerShift.CARER, None)
            carer = Carer.from_json(carer_json) if carer_json else None

            json_diaries = json.get(Problem.CarerShift.DIARIES, None)
            diaries = [Diary.from_json(json_diary) for json_diary in json_diaries] if json_diaries else []

            return Problem.CarerShift(**{Problem.CarerShift.CARER: carer, Problem.CarerShift.DIARIES: diaries})

    class LocationVisits(rows.model.object.DataObject):
        """Groups visits to be formed at the same location"""

        LOCATION = 'location'
        ADDRESS = 'address'
        VISITS = 'visits'

        def __init__(self, **kwargs):
            self.__location = kwargs.get(Problem.LocationVisits.LOCATION, None)
            self.__address = kwargs.get(Problem.LocationVisits.ADDRESS, None)
            self.__visits = kwargs.get(Problem.LocationVisits.VISITS, [])

        def as_dict(self):
            bundle = super(Problem.LocationVisits, self).as_dict()
            bundle[Problem.LocationVisits.LOCATION] = self.__location
            bundle[Problem.LocationVisits.ADDRESS] = self.__address
            bundle[Problem.LocationVisits.VISITS] = self.__visits
            return bundle

        @property
        def location(self):
            """Return a property"""

            return self.__location

        @property
        def address(self):
            """Return a property"""

            return self.__address

        @property
        def visits(self):
            """Return a property"""

            return self.__visits

        @staticmethod
        def from_json(visits_json):
            """Create object from dictionary"""

            location_json = visits_json.get(Problem.LocationVisits.LOCATION)
            location = Location.from_json(location_json) if location_json else None

            address_json = visits_json.get(Problem.LocationVisits.ADDRESS)
            address = Address.from_json(address_json) if address_json else None

            visits_json = visits_json.get(Problem.LocationVisits.VISITS)
            visits = [Visit.from_json(visit_json) for visit_json in visits_json] if visits_json else []

            return Problem.LocationVisits(location=location, address=address, visits=visits)

    def __init__(self, **kwargs):
        super(Problem, self).__init__()

        self.__metadata = kwargs.get(Problem.METADATA, None)
        self.__carers = kwargs.get(Problem.CARERS, None)
        self.__visits = kwargs.get(Problem.VISITS, None)

    def as_dict(self):
        bundle = super(Problem, self).as_dict()

        if self.__metadata:
            bundle[Problem.METADATA] = self.__metadata

        if self.__carers:
            bundle[Problem.CARERS] = self.__carers

        if self.__visits:
            bundle[Problem.VISITS] = self.__visits

        return bundle

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        metadata_json = json_obj.get(Problem.METADATA, None)
        metadata = Problem.Metadata.from_json(metadata_json) if metadata_json else None

        carers_json = json_obj.get(Problem.CARERS, None)
        carers = [Problem.CarerShift.from_json(carer_json) for carer_json in carers_json] if carers_json else []

        visits_json = json_obj.get(Problem.VISITS, None)
        visits = [Problem.LocationVisits.from_json(visit_json) for visit_json in visits_json] if visits_json else []

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
