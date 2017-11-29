"""Execute the main program."""

# pylint: import-error, no-name-in-module, no-member

import distutils.version
import rows.parser
import rows.console
import rows.csv_data_source
import rows.pull_command


class Application:
    """Execute the main program according to the input arguments"""

    PROGRAM_NAME = 'rows_cli'
    VERSION = distutils.version.StrictVersion('0.0.1')

    def __init__(self):
        self.__console = rows.console.Console()
        self.__data_source = rows.csv_data_source.CSVDataSource('~/dev/cordia/data/cordia/home_carer_position.csv',
                                                                '~/dev/cordia/data/cordia/home_carer_shift_pattern.csv',
                                                                '~/dev/cordia/data/cordia/service_user_visit.csv')
        self.__handlers = {rows.parser.Parser.PULL_COMMAND: rows.pull_command.Handler(self)}

    def run(self, args):
        """The default entry point for the application."""

        parser = rows.parser.Parser(program_name=Application.PROGRAM_NAME)
        args = parser.parse_args(args)
        handler_name = getattr(args, rows.parser.Parser.COMMAND_PARSER)
        if handler_name:
            handler = self.__handlers[handler_name]
            return handler(args)
        else:
            if getattr(args, 'version'):
                return self.__handle_version(args)
        return 0

    def __handle_version(self, __namespace):
        message = '{0} version {1}'.format(Application.PROGRAM_NAME, Application.VERSION)
        self.__console.write_line(message)

    @property
    def data_source(self):
        """Returns a property"""

        return self.__data_source
