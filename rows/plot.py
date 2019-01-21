import datetime
import json
import os
import logging
import operator
import functools

import bs4
import pandas

import rows.model.schedule


def load_schedule_from_json(file_path):
    with open(file_path, 'r') as input_stream:
        schedule_json = json.load(input_stream)
        return rows.model.schedule.Schedule.from_json(schedule_json)


def load_schedule_from_gexf(file_path):
    with open(file_path, 'r') as input_stream:
        soup = bs4.BeautifulSoup(input_stream, 'html5lib')
        return rows.model.schedule.Schedule.from_gexf(soup)


def load_schedule(file_path):
    file_name, file_ext = os.path.splitext(file_path)
    if file_ext == '.json':
        return load_schedule_from_json(file_path)
    elif file_ext == '.gexf':
        return load_schedule_from_gexf(file_path)
    else:
        raise ValueError('Unrecognized extension ' + file_ext)


def get_schedule_data_frame(schedule, routing_session, location_finder, carer_diaries, visit_durations):
    data_set = []
    for route in schedule.routes():
        travel_time = datetime.timedelta()
        for source, destination in route.edges():
            source_loc = location_finder.find(source.visit.service_user)
            if not source_loc:
                logging.error('Failed to resolve location of %s', source.visit.service_user)
                continue
            destination_loc = location_finder.find(destination.visit.service_user)
            if not destination_loc:
                logging.error('Failed to resolve location of %s', destination.visit.service_user)
                continue
            distance = routing_session.distance(source_loc, destination_loc)
            if distance is None:
                logging.error('Distance cannot be estimated between %s and %s', source_loc, destination_loc)
                continue
            travel_time += datetime.timedelta(seconds=distance)
        service_time = functools.reduce(operator.add, (visit_durations[visit.visit] for visit in route.visits))
        available_time = functools.reduce(operator.add, (event.duration
                                                         for event in carer_diaries[route.carer.sap_number].events))
        data_set.append([route.carer.sap_number,
                         available_time,
                         service_time,
                         travel_time,
                         float(service_time.total_seconds() + travel_time.total_seconds())
                         / available_time.total_seconds(),
                         len(route.visits)])
    data_set.sort(key=operator.itemgetter(4))
    return pandas.DataFrame(columns=['Carer', 'Availability', 'Service', 'Travel', 'Usage', 'Visits'], data=data_set)


class VisitDict:
    def __init__(self):
        self.__dict = {}

    def __getitem__(self, item):
        if item in self.__dict:
            return self.__dict.get(item)
        if item.key:
            same_id_items = [visit for visit in self.__dict if visit.key == item.key]
            if same_id_items:
                if len(same_id_items) > 1:
                    logging.warning('More than one visit with id %s', item.key)
                return self.__dict[same_id_items[0]]

        visits_with_time_offset = [(visit, abs(self.__time_diff(visit.time, item.time).total_seconds()))
                                   for visit in self.__dict if visit.service_user == item.service_user]
        if visits_with_time_offset:
            visit, time_offset = min(visits_with_time_offset, key=operator.itemgetter(1))
            if time_offset > 2 * 3600:
                logging.warning('Suspiciously high time offset %s while finding a key of the visit %s',
                                time_offset,
                                visit)
            return self.__dict[visit]

        raise KeyError(item)

    def __setitem__(self, key, value):
        self.__dict[key] = value

    @staticmethod
    def __time_diff(left_time, right_time):
        __REF_DATE = datetime.date(2018, 1, 1)
        return datetime.datetime.combine(__REF_DATE, left_time) - datetime.datetime.combine(__REF_DATE, right_time)
