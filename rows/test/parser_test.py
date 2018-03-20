"""Test parsing command line arguments"""

import datetime
import unittest
import unittest.mock

import rows.model.area
from rows.parser import Parser


class TestParser(unittest.TestCase):
    """Test parsing command line arguments"""

    def setUp(self):
        self.default_area = rows.model.area.Area(key=1, code='test_area')
        data_source = unittest.mock.Mock()
        data_source.get_areas.return_value = [self.default_area]
        self.parser = Parser(data_source)

    def test_version(self):
        """should parse the 'version' command"""

        actual_namespace = self.parser.parse_args([Parser.VERSION_COMMAND])
        self.assertIsNotNone(Parser.get_argument(actual_namespace, Parser.COMMAND_PARSER))

    def test_solve(self):
        """should fail if the 'problem' argument is missing"""

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.SOLVE_COMMAND])

    def test_pull(self):
        """should fail if the 'area' argument is missing"""

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND])

    def test_pull_area(self):
        """should parse the 'area' argument and adjust 'from' and 'to' arguments"""

        intermediate_namespace = self.parser.parse_args([Parser.PULL_COMMAND, self.default_area.code])
        actual_namespace = self.parser.parse_database_objects(intermediate_namespace)

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_AREA_ARG), self.default_area)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG), Parser.PULL_FROM_DEFAULT_ARG)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG), Parser.PULL_TO_DEFAULT_ARG)

    def test_pull_area_from(self):
        """should parse the 'from' argument and adjust the 'to' argument"""

        area_code = 'test_area'
        start_from = datetime.date.today()

        intermediate_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                         area_code,
                                                         '='.join([Parser.PULL_FROM_ARG, str(start_from)])])
        actual_namespace = self.parser.parse_database_objects(intermediate_namespace)

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG), start_from)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG),
                         start_from + Parser.PULL_WINDOW_WIDTH_DEFAULT)

    def test_pull_area_to(self):
        """should parse the 'to' argument and adjust the 'from' argument"""

        area_code = 'test_area'
        end_at = datetime.date.today() + datetime.timedelta(days=2)

        intermediate_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                         area_code,
                                                         '='.join([Parser.PULL_TO_ARG, str(end_at)])])
        actual_namespace = self.parser.parse_database_objects(intermediate_namespace)

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG),
                         end_at - Parser.PULL_WINDOW_WIDTH_DEFAULT)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG), end_at)

    def test_pull_area_from_to(self):
        """should parse 'from' and 'to' arguments"""

        start_from = datetime.date.today() + datetime.timedelta(days=1)
        end_at = start_from + datetime.timedelta(days=1)

        intermediate_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                         self.default_area.code,
                                                         '='.join([Parser.PULL_FROM_ARG, str(start_from)]),
                                                         '='.join([Parser.PULL_TO_ARG, str(end_at)])])
        actual_namespace = self.parser.parse_database_objects(intermediate_namespace)

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_AREA_ARG), self.default_area)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG), start_from)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG), end_at)

    def test_pull_invalid_to(self):
        """should fail if the 'to' argument is in incorrect format"""

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND,
                                    'test_area',
                                    Parser.PULL_TO_ARG + '=WRONG_DATE'])

    def test_pull_invalid_from(self):
        """should fail if the 'from' argument is in incorrect format"""

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND,
                                    'test_area',
                                    Parser.PULL_FROM_ARG + '=WRONG_DATE'])

    def test_pull_from_later_to(self):
        """should fail if the 'from' argument is larger than the 'to' argument"""

        area = 'test_area'
        start_from = datetime.date.today()
        end_at = start_from - datetime.timedelta(days=10)

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND,
                                    area,
                                    '='.join([Parser.PULL_FROM_ARG, str(start_from)]),
                                    '='.join([Parser.PULL_TO_ARG, str(end_at)])])

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND,
                                    area,
                                    '='.join([Parser.PULL_TO_ARG, str(end_at)]),
                                    '='.join([Parser.PULL_FROM_ARG, str(start_from)])])


if __name__ == '__main__':
    unittest.main()
