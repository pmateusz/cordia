"""Implements the pull command"""


class Handler:
    """Implements the pull command"""

    def __init__(self, application):
        self.__data_source = application.data_source

    def __call__(self, command):
        print(command)
