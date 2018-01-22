"""Encode model objects in JSON format"""

import json
import datetime

import distutils.version

from rows.model.address import Address
from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.event import AbsoluteEvent
from rows.model.location import Location
from rows.model.metadata import Metadata
from rows.model.past_visit import PastVisit
from rows.model.problem import Problem
from rows.model.schedule import Schedule
from rows.model.service_user import ServiceUser
from rows.model.visit import Visit


class JSONEncoder(json.JSONEncoder):
    """Encode model objects in JSON format"""

    UNZIP_CLASSES = [AbsoluteEvent,
                     Address,
                     Area,
                     Carer,
                     Diary,
                     Location,
                     Metadata,
                     PastVisit,
                     Problem,
                     Problem.CarerShift,
                     Problem.LocalVisit,
                     Problem.LocalVisits,
                     Schedule,
                     ServiceUser,
                     Visit]

    def default(self, o):  # pylint: disable=method-hidden
        if o.__class__ in JSONEncoder.UNZIP_CLASSES:
            return o.as_dict()
        if isinstance(o, datetime.date):
            return o.isoformat()
        if isinstance(o, datetime.time):
            return o.isoformat()
        if isinstance(o, datetime.timedelta):
            return str(int(o.total_seconds()))
        if isinstance(o, distutils.version.StrictVersion):
            return str(o)
        return json.JSONEncoder.default(self, o)
