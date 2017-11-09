"""Execute the main program."""

# pylint: disable=too-few-public-methods, import-error, no-name-in-module, no-member

import distutils.version
import rows.parser
import rows.console


class Application:
    """Execute the main program according to the input arguments"""

    PROGRAM_NAME = 'rows'
    VERSION = distutils.version.StrictVersion('0.0.1')

    def __init__(self):
        self.__console = rows.console.Console()
        self.__handlers = {}

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
