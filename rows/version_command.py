"""Implements the version command"""


class Handler:
    """Implements the version command"""

    ENCODING = 'utf-8'

    def __init__(self, application):
        self.__application = application
        self.__console = application.console

    def __call__(self, command):
        message = '{0} version {1}'.format(self.__application.PROGRAM_NAME, self.__application.VERSION)
        self.__console.write_line(message)
        return 0
