import json
import rows.model.json


class Handler:
    """Implements the history command"""

    def __init__(self, application):
        self.__application = application
        self.__data_source = application.data_source
        self.__console = application.console

    def __call__(self, command):
        area = getattr(command, 'area')
        output_file = getattr(command, 'output')

        visits = self.__data_source.get_historical_visits(area)
        with open(output_file, self.__application.output_file_mode) as file_stream:
            json.dump(visits, file_stream, sort_keys=False, cls=rows.model.json.JSONEncoder)
        return 0
