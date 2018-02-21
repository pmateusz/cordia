"""Test pull command handler"""

import argparse
import datetime
import json
import os
import unittest
import tempfile

from rows.application import Application
from rows.model.area import Area
from rows.model.problem import Problem
from rows.parser import ValueHolder
from rows.pull_command import Handler


@unittest.skipIf(os.getenv('TRAVIS', 'false') == 'true',
                 'Resource files required to run the test are not available in CI')
class HandlerTestCase(unittest.TestCase):
    """Test pull command handler"""

    def setUp(self):
        """Initialise application"""

        self.example_area = Area(key='test_key')
        self.example_begin_date = datetime.datetime(2017, 2, 1).date()
        self.example_end_date = datetime.datetime(2017, 2, 7).date()
        self.application = Application(output_file_mode='w')
        self.application.load()

    def tearDown(self):
        """Tear down application"""

        self.application.dispose()

    def test_pull_example(self):
        """Test example pull method call"""

        # given
        handler = Handler(self.application)

        # when
        with tempfile.NamedTemporaryFile(mode=self.application.output_file_mode) as file:
            args = argparse.Namespace(**{'from': ValueHolder(self.example_begin_date),
                                         'to': ValueHolder(self.example_end_date),
                                         'area': self.example_area,
                                         'output': file.name})
            handler(args)

            # then
            with open(file.name) as local_file:
                json_obj = json.load(local_file)
                problem = Problem.from_json(json_obj)
                self.assertIsNotNone(problem)
                print(problem)
