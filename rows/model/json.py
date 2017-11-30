import json
import datetime

from rows.model.address import Address
from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.location import Location
from rows.model.problem import Problem
from rows.model.visit import Visit


class JSONEncoder(json.JSONEncoder):
    """Encodes the Problem class in JSON format"""

    UNZIP_CLASSES = [Area, Carer, Visit, Problem.Metadata, Location, Address]

    def default(self, o):  # pylint: disable=method-hidden
        if o.__class__ in JSONEncoder.UNZIP_CLASSES:
            return o.as_dict()
        if isinstance(o, datetime.date):
            return o.isoformat()
        return json.JSONEncoder.default(self, o)
