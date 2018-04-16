"""Pulls a real solution for the schedule"""

import logging
import json

from rows.model.json import JSONEncoder


class Handler:
    """Pulls a real solution for the schedule"""

    def __init__(self, application):
        self.__application = application
        self.__data_source = application.data_source
        self.__console = application.console

    def __call__(self, command):
        area = getattr(command, 'area')
        schedule_date = getattr(command, 'schedule_date')
        output_file = getattr(command, 'output')

        schedule = self.__data_source.get_past_schedule(area, schedule_date)
        try:
            if not schedule.visits:
                logging.warning('Schedule does not contain any visits')

            with open(output_file, self.__application.output_file_mode) as file_stream:
                json.dump(schedule, file_stream, indent=2, sort_keys=False, cls=JSONEncoder)
            return 0
        except FileExistsError:
            error_msg = "The file '{0}' already exists. Please try again with a different name.".format(output_file)
            self.__console.write_line(error_msg)
            return 1
        except RuntimeError as ex:
            logging.error('Failed to save problem instance due to error: %s', ex)
            return 1
