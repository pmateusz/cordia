"""Test parsing command line arguments"""

import datetime
import os
import os.path
import stat
import tempfile
import unittest

from rows.parser import Parser


class TestParser(unittest.TestCase):
    """Test parsing command line arguments"""

    def setUp(self):
        self.parser = Parser()

    def test_version(self):
        """should parse the 'version' command"""

        actual_namespace = self.parser.parse_args([Parser.VERSION_COMMAND])

        self.assertIsNotNone(Parser.get_argument(actual_namespace, Parser.VERSION_COMMAND))

    def test_export(self):
        """should parse the 'export' command"""

        actual_namespace = self.parser.parse_args([Parser.EXPORT_COMMAND])

        self.assertTrue(Parser.is_command(actual_namespace, Parser.EXPORT_COMMAND))

    def test_export_output(self):
        """should parse the 'output' argument"""

        file_name = 'test_file'
        actual_namespace = self.parser.parse_args([Parser.EXPORT_COMMAND,
                                                   '='.join([Parser.EXPORT_OUTPUT_ARGUMENT, file_name])])

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.EXPORT_OUTPUT_ARGUMENT), file_name)

    def test_export_to_existing_file(self):
        """should fail if the output file already exists
        should fail if the output file is a directory"""

        with tempfile.NamedTemporaryFile() as temp_file:
            with self.assertRaises(SystemExit):
                self.parser.parse_args(
                    [Parser.EXPORT_COMMAND, '='.join([Parser.EXPORT_OUTPUT_ARGUMENT, temp_file.name])])

    def test_export_to_existing_dir(self):
        """should fail if the output file already exists
        should fail if the output file is a directory"""

        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaises(SystemExit):
                self.parser.parse_args(
                    [Parser.EXPORT_COMMAND, '='.join([Parser.EXPORT_OUTPUT_ARGUMENT, temp_dir])])

    def test_export_output_env_var(self):
        """should replace the environment variable in the 'output' variable"""

        file_path = os.path.join('$PWD', 'test_file')
        actual_namespace = self.parser.parse_args([Parser.EXPORT_COMMAND,
                                                   '='.join([Parser.EXPORT_OUTPUT_ARGUMENT, file_path])])

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.EXPORT_OUTPUT_ARGUMENT), file_path)

    def test_export_invalid_output(self):
        """should fail if the 'output' argument cannot be written due to safety permissions"""

        with tempfile.TemporaryDirectory() as temp_dir:
            file_path = os.path.join(temp_dir, 'test_file')
            os.chmod(temp_dir, stat.S_IREAD)
            with self.assertRaises(SystemExit):
                self.parser.parse_args(
                    [Parser.EXPORT_COMMAND, '='.join([Parser.EXPORT_OUTPUT_ARGUMENT, file_path])])

    def test_solve(self):
        """should parse the command"""

        actual_namespace = self.parser.parse_args([Parser.SOLVE_COMMAND])

        self.assertTrue(Parser.is_command(actual_namespace, Parser.SOLVE_COMMAND))

    def test_pull(self):
        """should fail if the 'area' argument is missing"""

        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND])

    def test_pull_area(self):
        """should parse the 'area' argument and adjust 'from' and 'to' arguments"""

        area = 'test_area'
        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND, area])

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_AREA_ARG), area)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG), Parser.PULL_FROM_DEFAULT_ARG)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG), Parser.PULL_TO_DEFAULT_ARG)

    def test_pull_area_from(self):
        """should parse the 'from' argument and adjust the 'to' argument"""

        area = 'test_area'
        start_from = datetime.date.today()

        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                   area,
                                                   '='.join([Parser.PULL_FROM_ARG, str(start_from)])])

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG), start_from)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG),
                         start_from + Parser.PULL_WINDOW_WIDTH_DEFAULT)

    def test_pull_area_to(self):
        """should parse the 'to' argument and adjust the 'from' argument"""

        area = 'test_area'
        end_at = datetime.date.today() + datetime.timedelta(days=2)

        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                   area,
                                                   '='.join([Parser.PULL_TO_ARG, str(end_at)])])
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG),
                         end_at - Parser.PULL_WINDOW_WIDTH_DEFAULT)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG), end_at)

    def test_pull_area_from_to(self):
        """should parse 'from' and 'to' arguments"""

        area = 'test_area'
        start_from = datetime.date.today() + datetime.timedelta(days=1)
        end_at = start_from + datetime.timedelta(days=1)

        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                   area,
                                                   '='.join([Parser.PULL_FROM_ARG, str(start_from)]),
                                                   '='.join([Parser.PULL_TO_ARG, str(end_at)])])

        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_AREA_ARG), area)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_FROM_ARG),
                         start_from)
        self.assertEqual(Parser.get_argument(actual_namespace, Parser.PULL_TO_ARG), end_at)

    @unittest.skip("no list of areas is available at the moment")
    def test_pull_invalid_area(self):
        """should fail if the 'area' argument is invalid"""

        self.fail()

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
