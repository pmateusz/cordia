"""Implements the pull command"""

import collections
import itertools
import json
import logging

import rows.version

from rows.model.diary import Diary
from rows.model.json import JSONEncoder
from rows.model.metadata import Metadata
from rows.model.problem import Problem
from rows.model.service_user import ServiceUser


class Handler:
    """Implements the pull command"""

    def __init__(self, application):
        self.__application = application
        self.__data_source = application.data_source
        self.__location_finder = application.location_finder
        self.__console = application.console

    def __call__(self, command):
        area = getattr(command, 'area')
        begin = getattr(command, 'from').value
        end = getattr(command, 'to').value
        output_file = getattr(command, 'output')

        problem = self.__create_problem(area, begin, end)
        try:
            with open(output_file, self.__application.output_file_mode) as file_stream:
                json.dump(problem, file_stream, indent=2, sort_keys=False, cls=JSONEncoder)
            return 0
        except FileExistsError:
            error_msg = "The file '{0}' already exists. Please try again with a different name.".format(output_file)
            self.__console.write_line(error_msg)
            return 1
        except RuntimeError as ex:
            logging.error('Failed to save problem instance due to error: %s', ex)
            return 1

    def __create_problem(self, area, begin, end):  # pylint: disable=too-many-locals
        visits_by_service_user = collections.OrderedDict()
        address_by_service_user = {}
        location_by_service_user = {}
        for visit in self.__data_source.get_visits_for_area(area, begin, end):
            local_visit = Problem.LocalVisit(date=visit.date,
                                             time=visit.time,
                                             duration=visit.duration,
                                             carer_count=visit.carer_count)
            if visit.service_user in visits_by_service_user:
                visits_by_service_user[visit.service_user].append(local_visit)
            else:
                visits_by_service_user[visit.service_user] = [local_visit]
                location = self.__location_finder.find(visit.address)
                if location is None:
                    logging.error("Failed to find location of the address '%s'", location)
                location_by_service_user[visit.service_user] = location
                address_by_service_user[visit.service_user] = visit.address

        visits = [Problem.LocalVisits(service_user=service_user, visits=visits)
                  for service_user, visits in visits_by_service_user.items()]

        carers = []
        for carer in self.__data_source.get_carers_for_area(area, begin, end):
            absolute_events = self.__data_source.get_interval_for_carer(carer, begin, end)
            diaries = [Diary(**{Diary.DATE: date, Diary.EVENTS: list(events), Diary.SCHEDULE_PATTERN_KEY: None})
                       for date, events in itertools.groupby(absolute_events, key=lambda event: event.begin.date())]
            carers.append(Problem.CarerShift(**{Problem.CarerShift.CARER: carer, Problem.CarerShift.DIARIES: diaries}))

        service_users = []
        for service_user_id in visits_by_service_user.keys():
            carers_frequency = collections.Counter()
            all_visits = 0
            for visit in self.__data_source.get_past_visits_for_service_user(service_user_id):
                carers_frequency[visit.carer.sap_number] += 1
                all_visits += 1

            carer_pref_to_use = [(carer, float(count) / all_visits) for carer, count in carers_frequency.most_common(8)]
            carer_pref_to_use.sort(key=lambda element: element[1], reverse=True)

            service_users.append(ServiceUser(key=service_user_id,
                                             address=address_by_service_user[service_user_id],
                                             location=location_by_service_user[service_user_id],
                                             carer_preference=carer_pref_to_use))

        return Problem(metadata=Metadata(area=area, begin=begin, end=end, version=rows.version.VERSION),
                       carers=carers,
                       visits=visits,
                       service_users=service_users)
