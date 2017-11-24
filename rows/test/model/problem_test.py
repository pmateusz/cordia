"""Test problem"""

import datetime
import io
import json
import unittest

from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.problem import Problem
from rows.model.visit import Visit


class TestProblem(unittest.TestCase):
    """Test problem"""

    def setUp(self):
        self.example_problem = Problem(metadata=Problem.Metadata(area=Area(id=23),
                                                                 begin=datetime.date.today(),
                                                                 end=datetime.date.today() + datetime.timedelta(
                                                                     days=1)),
                                       carers=[Carer(id='123456')],
                                       visits=[Visit(id='123456')])

    def test_dictionary_serialization(self):
        """Can serialize carer from and to a file stream"""
        actual = Problem(**self.example_problem.dict())

        self.assertEqual(actual, self.example_problem)

    def test_json_serialization(self):
        stream = io.StringIO()

        self.example_problem.to_json(stream)
        stream.seek(0, io.SEEK_SET)
        json_obj = json.load(stream)

        actual = Problem.from_json(json_obj)
        self.assertEqual(actual, self.example_problem)


if __name__ == '__main__':
    unittest.main()
