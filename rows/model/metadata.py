"""Header of the data model"""
import distutils.version

import dateutil.parser

import rows.version
import rows.model.object
from rows.model.area import Area


class Metadata(rows.model.object.DataObject):
    """Header of the data model"""

    AREA = 'area'
    BEGIN = 'begin'
    END = 'end'
    VERSION = 'version'

    def __init__(self, **kwargs):
        super(Metadata, self).__init__()

        self.__version = kwargs.get(Metadata.VERSION, rows.version.VERSION)
        self.__area = kwargs.get(Metadata.AREA, None)
        self.__begin = kwargs.get(Metadata.BEGIN, None)
        self.__end = kwargs.get(Metadata.END, None)

    def as_dict(self):
        """Create object from dictionary"""

        bundle = super(Metadata, self).as_dict()

        if self.__version:
            bundle[Metadata.VERSION] = self.__version

        if self.__area:
            bundle[Metadata.AREA] = self.__area

        if self.__begin:
            bundle[Metadata.BEGIN] = self.__begin

        if self.__end:
            bundle[Metadata.END] = self.__end

        return bundle

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        version = distutils.version.StrictVersion()
        version_obj = json_obj.get(Metadata.VERSION, None)
        if version_obj:
            version.parse(version_obj)

        area = None
        area_obj = json_obj.get(Metadata.AREA, None)
        if area_obj:
            area = Area.from_json(area_obj)

        begin = None
        begin_obj = json_obj.get(Metadata.BEGIN, None)
        if begin_obj:
            begin = dateutil.parser.parse(begin_obj)
            if begin:
                begin = begin.date()

        end = None
        end_obj = json_obj.get(Metadata.END, None)
        if end_obj:
            end = dateutil.parser.parse(end_obj)
            if end:
                end = end.date()

        return Metadata(area=area, begin=begin, end=end, version=version)

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
