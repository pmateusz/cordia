"""Execute the main program."""

# pylint: import-error, no-name-in-module, no-member

import rows.parser
import rows.console
import rows.settings
import rows.location_finder
import rows.csv_data_source
import rows.sql_data_source
import rows.pull_command
import rows.solve_command
import rows.version


# TODO define settings file with location of file system components, database server, database name
class Application:
    """Execute the main program according to the input arguments"""

    EXIT_OK = 0
    PROGRAM_NAME = 'rows_cli'

    def __init__(self, output_file_mode='x'):
        self.__console = rows.console.Console()
        self.__settings = rows.settings.Settings()
        self.__location_cache = rows.location_finder.FileSystemCache(self.__settings)
        self.__location_finder = rows.location_finder.RobustLocationFinder(self.__location_cache, timeout=5.0)
        self.__data_source = rows.sql_data_source.SqlDataSource(self.settings,
                                                                self.console,
                                                                self.__location_finder)
        self.__handlers = {rows.parser.Parser.PULL_COMMAND: rows.pull_command.Handler(self),
                           rows.parser.Parser.SOLVE_COMMAND: rows.solve_command.Handler(self)}
        self.__output_file_mode = output_file_mode

    def load(self):
        """Initialize application components"""

        self.__settings.reload()
        self.__data_source.reload()
        self.__location_cache.reload()

    def dispose(self):
        """Release application components"""

        self.__location_cache.save()

    def run(self, args):
        """The default entry point for the application."""

        parser = self.create_parser()
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
        message = '{0} version {1}'.format(Application.PROGRAM_NAME, rows.version.VERSION)
        self.__console.write_line(message)

    def create_parser(self):
        return rows.parser.Parser(self.data_source, program_name=Application.PROGRAM_NAME)

    @property
    def output_file_mode(self):
        """Returns mode for handling output files"""

        return self.__output_file_mode

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

    @property
    def settings(self):
        """Returns a property"""

        return self.__settings
