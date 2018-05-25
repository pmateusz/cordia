"""Execute the main program."""

# pylint: import-error, no-name-in-module, no-member

import logging

import rows.parser
import rows.console
import rows.settings
import rows.location_finder
import rows.csv_data_source
import rows.sql_data_source
import rows.pull_command
import rows.solve_command
import rows.version_command
import rows.solution_command
import rows.version


class Application:
    """Execute the main program according to the input arguments"""

    EXIT_OK = 0
    EXIT_ERROR = 1
    PROGRAM_NAME = 'rows_cli'

    def __init__(self, install_directory, output_file_mode='x'):
        self.__console = rows.console.Console()

        self.__settings = rows.settings.Settings(install_directory)

        self.__user_tag_finder = rows.location_finder.UserLocationFinder(self.__settings)
        self.__location_cache = rows.location_finder.FileSystemCache(self.__settings)
        self.__location_finder = rows.location_finder.MultiModeLocationFinder(self.__location_cache,
                                                                              self.__user_tag_finder,
                                                                              timeout=5.0)

        self.__data_source = rows.sql_data_source.SqlDataSource(self.settings,
                                                                self.console,
                                                                self.__location_finder)
        self.__handlers = {rows.parser.Parser.PULL_COMMAND: rows.pull_command.Handler(self),
                           rows.parser.Parser.SOLVE_COMMAND: rows.solve_command.Handler(self),
                           rows.parser.Parser.VERSION_COMMAND: rows.version_command.Handler(self),
                           rows.parser.Parser.SOLUTION_COMMAND: rows.solution_command.Handler(self)}
        self.__output_file_mode = output_file_mode

    def load(self, args):
        """Initialize application components"""

        parser = self.create_parser()
        intermediate_args = parser.parse_args(args)
        self.__setup_logger(intermediate_args)

        self.__settings.reload()
        self.__data_source.reload()
        self.__location_cache.reload()

        return parser.parse_database_objects(intermediate_args)

    def dispose(self):
        """Release application components"""

        self.__location_cache.save()

    def run(self, args):
        """The default entry point for the application."""

        logging.debug("Running application with '{0}' installation directory", self.settings.install_dir)
        handler_name = getattr(args, rows.parser.Parser.COMMAND_PARSER)
        if handler_name:
            handler = self.__handlers[handler_name]
            return handler(args)
        else:
            self.__console.write_line('No command was passed. Please provide a valid command and try again.')
            return self.EXIT_ERROR

    def create_parser(self):
        return rows.parser.Parser(self.data_source, program_name=Application.PROGRAM_NAME)

    @staticmethod
    def __setup_logger(args):
        verbose_arg = getattr(args, 'verbose')
        if verbose_arg:
            logging.getLogger(__name__).setLevel(logging.DEBUG)

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
