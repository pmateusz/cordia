"""Implements the pull command"""

import collections
import itertools
import json
import logging

import rows.version

from rows.model.diary import Diary
from rows.model.json import JSONEncoder
from rows.model.problem import Problem


class Handler:
    """Implements the pull command"""

    def __init__(self, application):
        self.__data_source = application.data_source
        self.__location_finder = application.location_finder

    def __call__(self, command):
        area = getattr(command, 'area')
        begin = getattr(command, 'from').value
        end = getattr(command, 'to').value

        # TODO: allow other file names
        # TODO: fail if file already exists
        problem = self.__create_problem(area, begin, end)
        try:
            with open('problem.json', 'w') as file_stream:
                json.dump(problem, file_stream, indent=2, sort_keys=False, cls=JSONEncoder)
            return 0
        except RuntimeError as ex:
            logging.error('Failed to save problem instance due to error: %s', ex)

    def __create_problem(self, area, begin, end):
        visits_by_address = collections.OrderedDict()
        locations_by_address = {}
        for visit in self.__data_source.get_visits_for_area(area, begin, end):
            if visit.address in visits_by_address:
                visits_by_address[visit.address].append(visit)
            else:
                visits_by_address[visit.address] = [visit]

            if visit.address not in locations_by_address:
                location = self.__location_finder.find(visit.address)
                if location is None:
                    logging.error("Failed to find location of the address '%s'", location)
                locations_by_address[visit.address] = location

        visits = [Problem.LocationVisits(location=location,
                                         address=address,
                                         visits=visits_by_address[address])
                  for address, location in locations_by_address.items()]

        carers = []
        for carer in self.__data_source.get_carers_for_area(area, begin, end):
            absolute_events = self.__data_source.get_interval_for_carer(carer, begin, end)
            diaries = [Diary(**{Diary.DATE: date, Diary.EVENTS: list(events), Diary.SCHEDULE_PATTERN_KEY: None})
                       for date, events in itertools.groupby(absolute_events, key=lambda event: event.begin.date())]
            carers.append(Problem.CarerShift(**{Problem.CarerShift.CARER: carer, Problem.CarerShift.DIARIES: diaries}))

        return Problem(metadata=Problem.Metadata(area=area, begin=begin, end=end, version=rows.version.VERSION),
                       carers=carers,
                       visits=visits)
