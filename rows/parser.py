"""Parse and validate command line arguments."""

import argparse
import datetime
import dateutil.parser
import unittest


class ValueHolder:
    def __init__(self, value, is_default=True):
        self.__value = value
        self.__is_default = is_default

    @property
    def value(self):
        return self.__value

    @property
    def is_default(self):
        return self.__is_default

    @value.setter
    def value(self, value):
        self.__value = value
        self.__is_default = False

    def __eq__(self, other):
        if isinstance(other, datetime.date):
            return self.__value == other

        if isinstance(other, ValueHolder):
            return self.__value == other.__value and self.__is_default == other.__is_default

        return False

    def __str__(self):
        return str(self.__value)

    def __repr__(self):
        return (self.__value, self.__is_default).__repr__()


class Parser:
    """Parse and validate command line arguments."""
    COMMAND_PARSER = 'COMMAND NAME'

    VERSION_COMMAND = '--version'

    EXPORT_COMMAND = 'export'

    SOLVE_COMMAND = 'solve'

    PULL_COMMAND = 'pull'
    PULL_AREA_ARG = 'area'
    PULL_FROM_ARG = '--from'
    PULL_FROM_DEFAULT_ARG = ValueHolder(datetime.date.today() + datetime.timedelta(days=1))
    PULL_TO_ARG = '--to'
    PULL_TO_DEFAULT_ARG = ValueHolder(PULL_FROM_DEFAULT_ARG.value + datetime.timedelta(days=14))

    class PullFromArgAction(argparse.Action):
        def __call__(self, parser, namespace, values, option_string=None):
            from_value = getattr(namespace, 'from')
            to_value = getattr(namespace, 'to')
            if not to_value.is_default and to_value.value < values:
                raise argparse.ArgumentError(self,
                                             "value '{0}' cannot be larger than '{1}'".format(
                                                 values, to_value.value))
            from_value.value = values

    class PullToArgAction(argparse.Action):
        def __call__(self, parser, namespace, values, option_string=None):
            from_value = getattr(namespace, 'from')
            to_value = getattr(namespace, 'to')
            if not from_value.is_default and values < from_value.value:
                raise argparse.ArgumentError(self,
                                             "value '{0}' cannot be smaller than '{1}'".format(
                                                 values, from_value.value))
            to_value.value = values

    def __init__(self, program_name=None):
        """Register parsers for supported commands"""

        self.__parser = argparse.ArgumentParser(prog=program_name,
                                                description='Robust Optimization '
                                                            'for Workforce Scheduling command line utility')

        self.__parser.add_argument('-v',
                                   Parser.VERSION_COMMAND,
                                   help='show the version of this program and exit',
                                   action='store_true')

        subparsers = self.__parser.add_subparsers(dest=Parser.COMMAND_PARSER)

        pull_parser = subparsers.add_parser(name=Parser.PULL_COMMAND,
                                            help='pull an instance of the scheduling problem from an external source')
        pull_parser.add_argument(Parser.PULL_AREA_ARG,
                                 help='an administration, operations and management area'
                                      ' where the requested visits are assigned to')
        pull_parser.add_argument('-f',
                                 Parser.PULL_FROM_ARG,
                                 help='limit considered visits to these that are requested'
                                      ' after the specified date and time',
                                 default=Parser.PULL_FROM_DEFAULT_ARG,
                                 type=Parser.__parse_date,
                                 action=Parser.PullFromArgAction)
        pull_parser.add_argument('-t',
                                 '--to',
                                 help='limit considered visits to these that are requested'
                                      ' until the specified date and time',
                                 default=Parser.PULL_TO_DEFAULT_ARG,
                                 type=Parser.__parse_date,
                                 action=Parser.PullToArgAction)

        subparsers.add_parser(name='solve',
                              help='solve an instance of the scheduling problem')

        export_parser = subparsers.add_parser(name=Parser.EXPORT_COMMAND,
                                              help='export a schedule in the CSV format')
        export_parser.add_argument('-o',
                                   '--output',
                                   help='an output file where the schedule should be saved')

        self.__set_from = False
        self.__set_to = False

    def parse_args(self, args=None):
        """Parse command line arguments"""

        # TODO: execute post parse actions - such as adapting from and to times
        return self.__parser.parse_args(args)

    @staticmethod
    def __parse_date(text_value):
        try:
            date_time = dateutil.parser.parse(text_value)
            return date_time.date()
        except TypeError:
            msg = "Value '{0}' contains unrecognized characters".format(text_value)
            raise argparse.ArgumentTypeError(msg)
        except ValueError:
            msg = "Value '{0}' was not recognized as a date. " \
                  "Please use a valid format, such as {0}".format(text_value, datetime.date.today())
            raise argparse.ArgumentTypeError(msg)


class TestParser(unittest.TestCase):
    def setUp(self):
        self.parser = Parser()

    @staticmethod
    def __get_value_or_default(namespace, attribute, default=None):
        attribute_to_use = attribute.lstrip('-')
        return getattr(namespace, attribute_to_use, default)

    def test_parse_version(self):
        actual_namespace = self.parser.parse_args([Parser.VERSION_COMMAND])

        self.assertIsNotNone(TestParser.__get_value_or_default(actual_namespace, Parser.VERSION_COMMAND))

    def test_parse_export(self):
        actual_namespace = self.parser.parse_args([Parser.EXPORT_COMMAND])

        self.assertEqual(getattr(actual_namespace, Parser.COMMAND_PARSER), Parser.EXPORT_COMMAND)

    @unittest.skip
    def test_parse_export_with_output(self):
        self.fail()

    @unittest.skip
    def test_parse_export_with_output_as_existing_file(self):
        self.fail()

    @unittest.skip
    def test_parse_export_with_output_containing_environment_variable(self):
        self.fail()

    @unittest.skip
    def test_parse_export_with_invalid_output(self):
        self.fail()

    def test_parse_solve(self):
        actual_namespace = self.parser.parse_args([Parser.SOLVE_COMMAND])

        self.assertEqual(getattr(actual_namespace, Parser.COMMAND_PARSER), Parser.SOLVE_COMMAND)

    def test_parse_pull(self):
        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND])

    def test_parse_pull_with_area(self):
        area = 'test_area'
        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND, area])

        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_AREA_ARG), area)
        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_FROM_ARG),
                         Parser.PULL_FROM_DEFAULT_ARG)
        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_TO_ARG),
                         Parser.PULL_TO_DEFAULT_ARG)

    def test_parse_pull_with_area_and_from(self):
        area = 'test_area'
        start_from = datetime.date.today()

        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                   area,
                                                   '='.join([Parser.PULL_FROM_ARG, str(start_from)])])

        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_FROM_ARG), start_from)

    def test_parse_pull_with_area_and_to(self):
        area = 'test_area'
        end_at = datetime.date.today() + datetime.timedelta(days=2)

        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                   area,
                                                   '='.join([Parser.PULL_TO_ARG, str(end_at)])])

        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_TO_ARG), end_at)

    def test_parse_pull_with_area_from_and_to_(self):
        area = 'test_area'
        start_from = datetime.date.today() + datetime.timedelta(days=1)
        end_at = start_from + datetime.timedelta(days=1)

        actual_namespace = self.parser.parse_args([Parser.PULL_COMMAND,
                                                   area,
                                                   '='.join([Parser.PULL_FROM_ARG, str(start_from)]),
                                                   '='.join([Parser.PULL_TO_ARG, str(end_at)])])

        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_AREA_ARG), area)
        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_FROM_ARG), start_from)
        self.assertEqual(TestParser.__get_value_or_default(actual_namespace, Parser.PULL_TO_ARG), end_at)

    @unittest.skip("no list of areas is available at the moment")
    def test_parse_pull_with_invalid_area(self):
        self.fail()

    def test_parse_pull_with_invalid_to(self):
        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND,
                                    'test_area',
                                    Parser.PULL_TO_ARG + '=WRONG_DATE'])

    def test_parse_pull_with_invalid_from(self):
        with self.assertRaises(SystemExit):
            self.parser.parse_args([Parser.PULL_COMMAND,
                                    'test_area',
                                    Parser.PULL_FROM_ARG + '=WRONG_DATE'])

    def test_parse_pull_with_from_later_than_to(self):
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
