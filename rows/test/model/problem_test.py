"""Test problem"""

import datetime
import io
import json
import unittest

import distutils.version

from rows.model.address import Address
from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.event import AbsoluteEvent
from rows.model.json import JSONEncoder
from rows.model.location import Location
from rows.model.metadata import Metadata
from rows.model.problem import Problem
from rows.model.service_user import ServiceUser


class TestProblem(unittest.TestCase):
    """Test problem"""

    def setUp(self):
        visit_address = Address(house_number='75',
                                route='Montrose Street',
                                city='Glasgow',
                                post_code='G1 1XJ')
        begin_date = datetime.date(2017, 2, 1)
        end_date = begin_date + datetime.timedelta(days=1)

        self.example_problem = Problem(**{
            Problem.METADATA: Metadata(area=Area(key=23),
                                       begin=begin_date,
                                       end=end_date,
                                       version=distutils.version.StrictVersion('1.0.0')),
            Problem.CARERS: [
                Problem.CarerShift(carer=Carer(sap_number='123456',
                                               position='Senior Carer',
                                               address=Address(house_number='Flat 6, Room C, 6',
                                                               route='Blackfriars',
                                                               city='Glasgow',
                                                               post_code='G1 1QW')),
                                   diaries=[Diary(
                                       date=begin_date,
                                       shift_pattern_key='123',
                                       events=[AbsoluteEvent(
                                           begin=datetime.datetime(begin_date.year,
                                                                   begin_date.month,
                                                                   begin_date.day,
                                                                   hour=8),
                                           end=datetime.datetime(begin_date.year,
                                                                 begin_date.month,
                                                                 begin_date.day,
                                                                 hour=17,
                                                                 minute=30))])])
            ],
            Problem.VISITS: [
                Problem.LocalVisits(service_user='1234567',
                                    visits=[
                                        Problem.LocalVisit(
                                            date=begin_date,
                                            time=datetime.time(13, 0, 0),
                                            duration=datetime.timedelta(minutes=30))
                                    ])
            ],
            Problem.SERVICE_USERS: [
                ServiceUser(key='1234657',
                            address=visit_address,
                            location=Location(latitude=55.8619711, longitude=-4.2452754))
            ]
        })

    def test_dictionary_serialization(self):
        """Can serialize problem from a dictionary"""
        actual = Problem(**self.example_problem.as_dict())

        self.assertEqual(self.example_problem, actual)

    def test_json_serialization(self):
        """Can serialize problem to stream and deserialize from stream"""
        stream = io.StringIO()

        json.dump(self.example_problem, stream, cls=JSONEncoder)

        stream.seek(0, io.SEEK_SET)
        json_obj = json.load(stream)

        actual = Problem.from_json(json_obj)
        self.assertEqual(self.example_problem, actual)


if __name__ == '__main__':
    unittest.main()
