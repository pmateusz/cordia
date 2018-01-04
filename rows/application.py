"""Execute the main program."""

# pylint: import-error, no-name-in-module, no-member

import os

import rows.parser
import rows.console
import rows.location_finder
import rows.csv_data_source
import rows.pull_command
import rows.version


class Application:
    """Execute the main program according to the input arguments"""

    EXIT_OK = 0
    PROGRAM_NAME = 'rows_cli'

    def __init__(self):
        self.__console = rows.console.Console()
        self.__data_source = rows.csv_data_source.CSVDataSource(
            Application.__real_path('~/dev/cordia/data/cordia/home_carer_position.csv'),
            Application.__real_path('~/dev/cordia/data/cordia/home_carer_shift_pattern.csv'),
            Application.__real_path('~/dev/cordia/data/cordia/service_user_visit.csv'),
            Application.__real_path('~/dev/cordia/data/cordia/past_visits.csv'))

        self.__location_cache = rows.location_finder.FileSystemCache(
            Application.__real_path('~/dev/cordia/data/cordia/location_cache.json'))
        self.__location_finder = rows.location_finder.RobustLocationFinder(self.__location_cache, timeout=5.0)
        self.__handlers = {rows.parser.Parser.PULL_COMMAND: rows.pull_command.Handler(self)}

    def load(self):
        """Initialize application components"""

        self.__data_source.reload()
        self.__location_cache.reload()

    def dispose(self):
        """Release application components"""

        self.__location_cache.save()

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

    @staticmethod
    def __real_path(file_path):
        return os.path.realpath(os.path.expandvars(os.path.expanduser(file_path)))

    def __handle_version(self, __namespace):
        message = '{0} version {1}'.format(Application.PROGRAM_NAME, rows.version.VERSION)
        self.__console.write_line(message)

    @property
    def data_source(self):
        """Returns a property"""

        return self.__data_source

    @property
    def location_finder(self):
        """Returns a property"""

        return self.__location_finder

    @property
    def console(self):
        """Returns a property"""

        return self.__console
