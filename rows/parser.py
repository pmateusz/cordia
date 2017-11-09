"""Parse and validate command line arguments."""

import argparse
import datetime
import dateutil.parser


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


class Parser:  # pylint: disable=too-few-public-methods
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
        """Validate the 'from' argument"""

        def __call__(self, parser, namespace, values, option_string=None):
            from_value = getattr(namespace, 'from')
            to_value = getattr(namespace, 'to')
            if not to_value.is_default and to_value.value < values:
                raise argparse.ArgumentError(self,
                                             "value '{0}' cannot be larger than '{1}'".format(
                                                 values, to_value.value))
            from_value.value = values

    class PullToArgAction(argparse.Action):
        """Validate the 'to' argument"""

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
                  "Please use a valid format, for example {1}.".format(text_value, datetime.date.today())
            raise argparse.ArgumentTypeError(msg)
