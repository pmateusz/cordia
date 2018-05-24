"""Test pull command handler"""

import argparse
import datetime
import json
import os
import unittest
import tempfile

from rows.sql_data_source import SqlDataSource
from rows.application import Application
from rows.model.problem import Problem
from rows.parser import ValueHolder
from rows.pull_command import Handler


@unittest.skipIf(os.getenv('TRAVIS', 'false') == 'true',
                 'Resource files required to run the test are not available in CI')
class HandlerTestCase(unittest.TestCase):
    """Test pull command handler"""

    def setUp(self):
        """Initialise application"""

        self.example_begin_date = datetime.datetime(2017, 2, 1).date()
        self.example_end_date = datetime.datetime(2017, 2, 7).date()

        install_dir = os.path.expanduser('~/dev/cordia')
        self.application = Application(install_dir, output_file_mode='w')

        args = []
        self.application.load(args)
        self.example_area = self.application.create_parser().parse_area('C050')

    def tearDown(self):
        """Tear down application"""

        self.application.dispose()

    def test_pull_with_forecast(self):
        """Test pull method with forecast"""
        with self.application.data_source as local_connection:
            estimator = SqlDataSource.ForecastEstimator()
            estimator.reload(self.application.console,
                             local_connection,
                             self.example_area,
                             datetime.datetime(2017, 10, 1),
                             datetime.datetime(2017, 10, 14))

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
