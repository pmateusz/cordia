"""Implements the pull command"""

import collections
import itertools

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

        visits = self.__data_source.get_visits_for_area(area, begin, end)
        visits_by_user = collections.OrderedDict()
        address_locations = {}
        for visit in visits:
            if visit.service_user in visits_by_user:
                visits_by_user[visit.service_user].append(visit)
            else:
                visits_by_user[visit.service_user] = [visit]

            if visit.address not in address_locations:
                address_locations[visit.address] = self.__location_finder.find(visit.address)
                assert address_locations[visit.address]

        carers = self.__data_source.get_carers_for_area(area, begin, end)
        carer_events = {}
        for carer in carers:
            events = self.__data_source.get_interval_for_carer(carer, begin, end)
            carer_events[carer] = [(key, list(group)) for key, group in
                                   itertools.groupby(events, key=lambda event: event.begin.date())]
            print(carer)

        metadata = Problem.Metadata(area=area, begin=begin, end=end)
        problem = Problem(metadata=metadata)

        print(problem)
