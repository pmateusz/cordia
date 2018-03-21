"""Parse and validate command line arguments."""

import argparse
import datetime
import os
import dateutil.parser
import copy

import rows.util
import rows.sql_data_source


class ValueHolder:
    """Container for a single parameter that has a default value."""

    def __init__(self, value, is_default=True):
        self.__value = value
        self.__is_default = is_default

    @property
    def value(self):
        """Read value property"""

        return self.__value

    @property
    def is_default(self):
        """Read is_default property"""

        return self.__is_default

    @value.setter
    def value(self, value):
        self.__value = value
        self.__is_default = False

    def __eq__(self, other):
        if isinstance(other, ValueHolder):
            return self.__value == other.value and self.__is_default == other.is_default

        return self.__value == other

    def __str__(self):
        return str(self.__value)

    def __repr__(self):
        return (self.__value, self.__is_default).__repr__()


class Parser:
    """Parse and validate command line arguments."""
    COMMAND_PARSER = 'COMMAND NAME'

    VERSION_COMMAND = 'version'

    VERBOSE_ARGUMENT = '--verbose'

    SOLVE_COMMAND = 'solve'
    SOLVE_PROBLEM_ARG = 'problem'
    SOLVE_START_ARG = '--start'
    SOLVE_SOLUTIONS_LIMIT_ARG = '--solutions-limit'
    SOLVE_TIME_LIMIT_ARG = '--time-limit'
    SOLVE_SCHEDULE_DATE = '--schedule-date'

    PULL_COMMAND = 'pull'
    PULL_AREA_ARG = 'area'
    PULL_FROM_ARG = '--from'
    PULL_FROM_DEFAULT_ARG = datetime.date.today() + datetime.timedelta(days=1)
    PULL_WINDOW_WIDTH_DEFAULT = datetime.timedelta(days=14)
    PULL_TO_ARG = '--to'
    PULL_TO_DEFAULT_ARG = PULL_FROM_DEFAULT_ARG + PULL_WINDOW_WIDTH_DEFAULT
    PULL_OUTPUT_ARG = '--output'
    PULL_DURATION_ESTIMATOR_ARG = '--duration-estimator'

    class PullFromArgAction(argparse.Action):
        """Validate the 'from' argument"""

        def __call__(self, parser, namespace, values, option_string=None):
            from_value = Parser.get_argument(namespace, Parser.PULL_FROM_ARG)
            to_value = Parser.get_argument(namespace, Parser.PULL_TO_ARG)
            if not to_value.is_default and to_value.value < values:
                raise argparse.ArgumentError(self,
                                             "value '{0}' cannot be larger than '{1}'".format(
                                                 values, to_value.value))
            from_value.value = values

    class PullToArgAction(argparse.Action):
        """Validate the 'to' argument"""

        def __call__(self, parser, namespace, values, option_string=None):
            from_value = Parser.get_argument(namespace, Parser.PULL_FROM_ARG)
            to_value = Parser.get_argument(namespace, Parser.PULL_TO_ARG)
            if not from_value.is_default and values < from_value.value:
                raise argparse.ArgumentError(self,
                                             "value '{0}' cannot be smaller than '{1}'".format(
                                                 values, from_value.value))
            to_value.value = values

    def __init__(self, data_source, program_name=None):
        """Register parsers for supported commands"""

        self.__data_source = data_source
        self.__parser = argparse.ArgumentParser(prog=program_name,
                                                description='Robust Optimization '
                                                            'for Workforce Scheduling command line utility')

        def add_verbose_option(parser):
            parser.add_argument('-v',
                                Parser.VERBOSE_ARGUMENT,
                                help='run application with the verbose logging',
                                action='store_true')

        add_verbose_option(self.__parser)

        subparsers = self.__parser.add_subparsers(dest=Parser.COMMAND_PARSER)

        version_parser = subparsers.add_parser(Parser.VERSION_COMMAND, help='show the version of this program and exit')
        add_verbose_option(version_parser)

        pull_parser = subparsers.add_parser(name=Parser.PULL_COMMAND,
                                            help='pull an instance of the scheduling problem from an external source')

        pull_parser.add_argument(Parser.PULL_AREA_ARG,
                                 help='an administration, operations and management area'
                                      ' where the requested visits are assigned to')
        pull_parser.add_argument('-f',
                                 Parser.PULL_FROM_ARG,
                                 help='limit considered visits to these that are requested'
                                      ' after the specified date and time',
                                 default=ValueHolder(Parser.PULL_FROM_DEFAULT_ARG),
                                 type=Parser.__parse_date,
                                 action=Parser.PullFromArgAction)
        pull_parser.add_argument('-t',
                                 Parser.PULL_TO_ARG,
                                 help='limit considered visits to these that are requested'
                                      ' until the specified date and time',
                                 default=ValueHolder(Parser.PULL_TO_DEFAULT_ARG),
                                 type=Parser.__parse_date,
                                 action=Parser.PullToArgAction)
        pull_parser.add_argument('-o',
                                 Parser.PULL_OUTPUT_ARG,
                                 help='Save output to the specified file',
                                 type=str,
                                 default='problem.json')
        pull_parser.add_argument(Parser.PULL_DURATION_ESTIMATOR_ARG,
                                 help='estimate duration based on historical records',
                                 type=Parser.__parse_duration_estimator,
                                 default=None)
        add_verbose_option(pull_parser)

        solve_parser = subparsers.add_parser(name='solve', help='solve an instance of the scheduling problem')

        solve_parser.add_argument(Parser.SOLVE_PROBLEM_ARG,
                                  help='the problem to solve',
                                  type=str)
        solve_parser.add_argument(Parser.SOLVE_START_ARG,
                                  help='the initial solution to start from',
                                  type=str,
                                  default=None)
        solve_parser.add_argument(Parser.SOLVE_SOLUTIONS_LIMIT_ARG,
                                  help='set limit on the total number of valid solutions with decreasing cost',
                                  type=int,
                                  default=None)
        solve_parser.add_argument(Parser.SOLVE_TIME_LIMIT_ARG,
                                  help='set limit on the wall time',
                                  type=str,
                                  default=None)
        solve_parser.add_argument(Parser.SOLVE_SCHEDULE_DATE,
                                  help='set day to compute schedule for',
                                  type=Parser.__parse_date,
                                  default=None)
        add_verbose_option(solve_parser)

    def parse_args(self, args=None):
        """Parse command line arguments"""

        namespace = self.__parser.parse_args(args)
        if Parser.is_command(namespace, Parser.PULL_COMMAND):
            from_holder = Parser.get_argument(namespace, Parser.PULL_FROM_ARG)
            to_holder = Parser.get_argument(namespace, Parser.PULL_TO_ARG)
            if from_holder and to_holder and (from_holder.is_default ^ to_holder.is_default):
                if from_holder.is_default:
                    Parser.__set_argument(namespace, Parser.PULL_FROM_ARG,
                                          ValueHolder(to_holder.value - Parser.PULL_WINDOW_WIDTH_DEFAULT, False))
                elif to_holder.is_default:
                    Parser.__set_argument(namespace, Parser.PULL_TO_ARG,
                                          ValueHolder(from_holder.value + Parser.PULL_WINDOW_WIDTH_DEFAULT, False))
        return namespace

    def parse_database_objects(self, args):
        """Parse objects that require access to the database"""

        args_to_use = copy.copy(args)
        area_name = self.get_argument(args_to_use, self.PULL_AREA_ARG)
        if area_name:
            area = self.parse_area(area_name)
            self.__set_argument(args_to_use, self.PULL_AREA_ARG, area)

        return args_to_use

    @staticmethod
    def get_argument(namespace, argument):
        """Get the 'argument' property from the 'namespace' object"""

        argument_to_use = argument.lstrip('-')
        return getattr(namespace, argument_to_use, None)

    @staticmethod
    def __set_argument(namespace, argument, value):
        argument_to_use = argument.lstrip('-')
        setattr(namespace, argument_to_use, value)

    @staticmethod
    def is_command(namespace, command):
        """Check if the 'namespace' object contains arguments for the command of a name 'command'"""

        actual_command = getattr(namespace, Parser.COMMAND_PARSER)
        return actual_command == command

    def parse_area(self, area_name):
        areas = self.__data_source.get_areas()
        area_name_to_use = self.normalize(area_name)
        area_to_use = next((area for area in areas if self.normalize(area.code) == area_name_to_use), None)
        if area_to_use:
            return area_to_use
        used_codes = set()
        error_msg = 'Failed to find an area with the specified code. Please use one of the following codes instead:'
        for area in areas:
            if area.code in used_codes:
                continue
            used_codes.add(area.code)
            error_msg += os.linesep + '\t' + area.code
        raise RuntimeError(error_msg)

    @staticmethod
    def __parse_file_path(text_value):
        if os.path.exists(text_value):
            msg = "file '{0}' already exists. Please try another path.".format(text_value)
            raise argparse.ArgumentTypeError(msg)
        path_to_use = os.path.expandvars(text_value)
        directory = os.path.dirname(path_to_use) or os.getcwd()
        if not os.access(directory, os.W_OK):
            msg = "program does not have write permissions to directory {0}. Please try another path.".format(directory)
            raise argparse.ArgumentTypeError(msg)
        return text_value

    @staticmethod
    def __parse_duration_estimator(text_value):
        if not text_value:
            return None
        value_to_use = text_value.strip().lower()
        if value_to_use == rows.sql_data_source.SqlDataSource.GlobalPercentileEstimator.NAME:
            return value_to_use
        elif value_to_use == rows.sql_data_source.SqlDataSource.GlobalTaskConfidenceIntervalEstimator.NAME:
            return value_to_use
        elif value_to_use == rows.sql_data_source.SqlDataSource.PlannedDurationEstimator.NAME:
            return value_to_use
        msg = "Name '{0}' does not match any duration estimator." \
              " Please use a valid name, for example: {1}, {2} or {3}".format(text_value,
                                                                              rows.sql_data_source.SqlDataSource.GlobalPercentileEstimator.NAME,
                                                                              rows.sql_data_source.SqlDataSource.GlobalTaskConfidenceIntervalEstimator.NAME,
                                                                              rows.sql_data_source.SqlDataSource.PlannedDurationEstimator.NAME)
        raise argparse.ArgumentTypeError(msg)

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
                  "Please use a valid format, for example {1}.".format(text_value, datetime.date.today())
            raise argparse.ArgumentTypeError(msg)

    @staticmethod
    def normalize(text_value):
        return text_value
