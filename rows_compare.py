import argparse
import collections
import datetime
import functools
import glob
import json
import logging
import operator
import os
import os.path
import subprocess
import sys

import bs4

import pandas

import numpy

import matplotlib
import matplotlib.dates
import matplotlib.pyplot

import rows.settings
import rows.console
import rows.location_finder
import rows.sql_data_source

import rows.model.carer
import rows.model.area
import rows.model.service_user
import rows.model.problem
import rows.model.location
import rows.model.metadata
import rows.model.schedule
import rows.model.visit
import rows.model.past_visit
import rows.model.json


def handle_exception(exc_type, exc_value, exc_traceback):
    """Logs uncaught exceptions"""

    if issubclass(exc_type, KeyboardInterrupt):
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
    else:
        logging.error("Uncaught exception", exc_info=(exc_type, exc_value, exc_traceback))


__COMMAND = 'command'
__PULL_COMMAND = 'pull'
__INFO_COMMAND = 'info'
__COMPARE_DISTANCE_COMMAND = 'compare-distance'
__COMPARE_WORKLOAD_COMMAND = 'compare-workload'
__DEBUG_COMMAND = 'debug'
__AREA_ARG = 'area'
__FROM_ARG = 'from'
__TO_ARG = 'to'
__FILE_ARG = 'file'
__SOLUTION_FILE_ARG = 'solution'
__PROBLEM_FILE_ARG = 'problem'
__OUTPUT_PREFIX_ARG = 'output_prefix'
__OPTIONAL_ARG_PREFIX = '--'
__BASE_SCHEDULE_PATTERN = 'base_schedule_pattern'
__CANDIDATE_SCHEDULE_PATTERN = 'candidate_schedule_pattern'


def configure_parser():
    parser = argparse.ArgumentParser(prog=sys.argv[0],
                                     description='Robust Optimization '
                                                 'for Workforce Scheduling command line utility')

    subparsers = parser.add_subparsers(dest=__COMMAND)

    pull_parser = subparsers.add_parser(__PULL_COMMAND)
    pull_parser.add_argument(__AREA_ARG)
    pull_parser.add_argument(__OPTIONAL_ARG_PREFIX + __FROM_ARG)
    pull_parser.add_argument(__OPTIONAL_ARG_PREFIX + __TO_ARG)
    pull_parser.add_argument(__OPTIONAL_ARG_PREFIX + __OUTPUT_PREFIX_ARG)

    info_parser = subparsers.add_parser(__INFO_COMMAND)
    info_parser.add_argument(__FILE_ARG)

    compare_distance_parser = subparsers.add_parser(__COMPARE_DISTANCE_COMMAND)
    compare_distance_parser.add_argument(__BASE_SCHEDULE_PATTERN)
    compare_distance_parser.add_argument(__CANDIDATE_SCHEDULE_PATTERN)

    compare_workload_parser = subparsers.add_parser(__COMPARE_WORKLOAD_COMMAND)
    compare_workload_parser.add_argument(__PROBLEM_FILE_ARG)
    compare_workload_parser.add_argument(__BASE_SCHEDULE_PATTERN)
    compare_workload_parser.add_argument(__CANDIDATE_SCHEDULE_PATTERN)

    debug_parser = subparsers.add_parser(__DEBUG_COMMAND)
    debug_parser.add_argument(__PROBLEM_FILE_ARG)
    debug_parser.add_argument(__SOLUTION_FILE_ARG)

    return parser


def get_or_raise(obj, prop):
    value = getattr(obj, prop)
    if not value:
        raise ValueError('{0} not set'.format(prop))
    return value


def get_date_time(value):
    date_time = datetime.datetime.strptime(value, '%Y-%m-%d')
    return date_time


def pull(args, settings):
    area_code = get_or_raise(args, __AREA_ARG)
    from_raw_date = get_or_raise(args, __FROM_ARG)
    to_raw_date = get_or_raise(args, __TO_ARG)
    output_prefix = get_or_raise(args, __OUTPUT_PREFIX_ARG)

    console = rows.console.Console()
    user_tag_finder = rows.location_finder.UserLocationFinder(settings)
    location_cache = rows.location_finder.FileSystemCache(settings)
    location_finder = rows.location_finder.MultiModeLocationFinder(location_cache, user_tag_finder, timeout=5.0)
    data_source = rows.sql_data_source.SqlDataSource(settings, console, location_finder)

    from_date_time = get_date_time(from_raw_date)
    to_date_time = get_date_time(to_raw_date)
    current_date_time = from_date_time

    while current_date_time <= to_date_time:
        schedule = data_source.get_past_schedule(rows.model.area.Area(code=area_code), current_date_time.date())
        for visit in schedule.visits:
            visit.visit.address = None

        output_file = '{0}_{1}.json'.format(output_prefix, current_date_time.date().strftime('%Y%m%d'))
        with open(output_file, 'w') as output_stream:
            json.dump(schedule, output_stream, cls=rows.model.json.JSONEncoder)

        current_date_time += datetime.timedelta(days=1)


class RoutingServer:
    class Session:
        ENCODING = 'ascii'
        MESSAGE_TIMEOUT = 1
        EXIT_TIMEOUT = 5

        def __init__(self):
            self.__process = subprocess.Popen(['./build/rows-routing-server', '--maps=./data/scotland-latest.osrm'],
                                              stdin=subprocess.PIPE,
                                              stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE)

        def distance(self, source, destination):
            self.__process.stdin.write(
                json.dumps({'command': 'route',
                            'source': source.as_dict(),
                            'destination': destination.as_dict()}).encode(self.ENCODING))
            self.__process.stdin.write(os.linesep.encode(self.ENCODING))
            self.__process.stdin.flush()
            stdout_msg = self.__process.stdout.readline()
            if stdout_msg:
                message = json.loads(stdout_msg.decode(self.ENCODING))
                return message.get('distance', None)
            return None

        def close(self, exc, value, tb):
            stdout_msg, error_msg = self.__process.communicate('{"command":"shutdown"}'.encode(self.ENCODING),
                                                               timeout=self.MESSAGE_TIMEOUT)
            if error_msg:
                logging.error(error_msg)

            try:
                self.__process.wait(self.EXIT_TIMEOUT)
            except subprocess.TimeoutExpired:
                logging.exception('Failed to shutdown the routing server')
                self.__process.kill()
                self.__process.__exit__(exc, value, tb)

    def __enter__(self):
        self.__session = RoutingServer.Session()
        return self.__session

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.__session.close(exc_type, exc_val, exc_tb)


def get_travel_time(schedule, user_tag_finder):
    routes = schedule.routes()

    total_travel_time = datetime.timedelta()
    with RoutingServer() as session:
        for carer in routes:
            visit_it = iter(routes[carer])

            current_visit = next(visit_it, None)
            current_location = user_tag_finder.find(int(current_visit.visit.service_user))
            while current_visit:
                prev_location = current_location
                current_visit = next(visit_it, None)

                if not current_visit:
                    break

                current_location = user_tag_finder.find(int(current_visit.visit.service_user))
                travel_time_sec = session.distance(prev_location, current_location)
                if travel_time_sec:
                    total_travel_time += datetime.timedelta(seconds=travel_time_sec)
    return total_travel_time


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


def info(args, settings):
    user_tag_finder = rows.location_finder.UserLocationFinder(settings)
    user_tag_finder.reload()
    schedule_file = get_or_raise(args, __FILE_ARG)
    schedule_file_to_use = os.path.realpath(os.path.expandvars(schedule_file))
    schedule = load_schedule(schedule_file_to_use)
    carers = {visit.carer for visit in schedule.visits}
    print(get_travel_time(schedule, user_tag_finder), len(carers), len(schedule.visits))


def load_problem(problem_file):
    with open(problem_file, 'r') as input_stream:
        problem_json = json.load(input_stream)
        return rows.model.problem.Problem.from_json(problem_json)


class TimeDeltaConverter:

    def __init__(self):
        self.__zero = datetime.datetime(2018, 1, 1)
        self.__zero_num = matplotlib.dates.date2num(self.__zero)

    @property
    def zero(self):
        return self.__zero

    @property
    def zero_num(self):
        return self.__zero_num

    def __call__(self, series):
        return [matplotlib.dates.date2num(self.__zero + value) - self.__zero_num for value in series]


def compare_distance(args, settings):
    left_series = [load_schedule(file_path) for file_path in glob.glob(getattr(args, __BASE_SCHEDULE_PATTERN))]
    left_series.sort(key=operator.attrgetter('metadata.begin'))
    right_series = [load_schedule(file_path) for file_path in glob.glob(getattr(args, __CANDIDATE_SCHEDULE_PATTERN))]
    right_series.sort(key=operator.attrgetter('metadata.begin'))

    user_tag_finder = rows.location_finder.UserLocationFinder(settings)
    user_tag_finder.reload()
    left_results = [(schedule.metadata.begin, get_travel_time(schedule, user_tag_finder)) for schedule in left_series]
    right_results = [(schedule.metadata.begin, get_travel_time(schedule, user_tag_finder)) for schedule in right_series]

    dates = list(map(operator.itemgetter(0), left_results))
    dates.extend(map(operator.itemgetter(0), right_results))
    data_frame = pandas.DataFrame(columns=['HumanPlanners', 'ConstraintProgramming'],
                                  index=numpy.arange(min(dates),
                                                     max(dates) + datetime.timedelta(days=1),
                                                     dtype='datetime64[D]'))
    for data, duration in left_results:
        data_frame.HumanPlanners[data] = duration
    for data, duration in right_results:
        data_frame.ConstraintProgramming[data] = duration

    time_delta_convert = TimeDeltaConverter()
    indices = numpy.array(list(map(matplotlib.dates.date2num, data_frame.index)))
    width = 0.35

    figure, axis = matplotlib.pyplot.subplots()
    human_handle = axis.bar(indices,
                            time_delta_convert(data_frame.HumanPlanners), width, bottom=time_delta_convert.zero)
    cp_handle = axis.bar(indices + width,
                         time_delta_convert(data_frame.ConstraintProgramming), width, bottom=time_delta_convert.zero)
    axis.xaxis_date()
    axis.yaxis_date()
    axis.yaxis.set_major_formatter(matplotlib.dates.DateFormatter("%H:%M:%S"))
    axis.legend((human_handle, cp_handle), ('Human Planners', 'Constraint Programming'), loc='upper right')
    matplotlib.pyplot.show()


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
        # TODO: search for the closest time distance...
        service_time = functools.reduce(operator.add,
                                        (visit_durations[(visit.visit.service_user, visit.visit.time)]
                                         for visit in route.visits))
        available_time = functools.reduce(operator.add, (event.duration
                                                         for event in carer_diaries[route.carer.sap_number].events))
        data_set.append([route.carer.sap_number,
                         available_time,
                         service_time,
                         travel_time,
                         float(service_time.total_seconds() + travel_time.total_seconds())
                         / available_time.total_seconds()])
    data_set.sort(key=operator.itemgetter(4))
    return pandas.DataFrame(columns=['Carer', 'Availability', 'Service', 'Travel', 'Usage'], data=data_set)


def compare_workload(args, settings):
    problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
    diary_by_date_by_carer = collections.defaultdict(dict)
    for carer_shift in problem.carers:
        for diary in carer_shift.diaries:
            diary_by_date_by_carer[diary.date][carer_shift.carer.sap_number] = diary
    base_schedules = [load_schedule(file_path)
                      for file_path in glob.glob(getattr(args, __BASE_SCHEDULE_PATTERN))]
    base_schedule_by_date = {schedule.metadata.begin: schedule for schedule in base_schedules}
    candidate_schedules = [load_schedule(file_path)
                           for file_path in glob.glob(getattr(args, __CANDIDATE_SCHEDULE_PATTERN))]
    candidate_schedule_by_date = {schedule.metadata.begin: schedule for schedule in candidate_schedules}
    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()

    dates = set(candidate_schedule_by_date.keys())
    for date in base_schedule_by_date.keys():
        dates.add(date)
    dates = list(dates)
    dates.sort()

    with RoutingServer() as routing_session:
        for date in dates:
            print(date)
            base_schedule = base_schedule_by_date.get(date, None)
            if not base_schedule:
                logging.error('No base schedule is available for %s', date)
                continue

            observed_duration_by_visit = {}
            for past_visit in base_schedule.visits:
                if past_visit.check_in and past_visit.check_out:
                    observed_duration = past_visit.check_out - past_visit.check_in
                    if observed_duration.days < 0:
                        logging.error('Observed duration %s is negative', observed_duration)
                else:
                    logging.warning('Visit %s is not supplied with information on check-in and check-out information',
                                    past_visit.visit.key)
                    observed_duration = past_visit.duration
                observed_duration_by_visit[(past_visit.visit.service_user, past_visit.visit.time)] = observed_duration

            candidate_schedule = candidate_schedule_by_date.get(date, None)
            if not candidate_schedule:
                logging.error('No candidate schedule is available for %s', date)
                continue

            base_schedule_data_frame = get_schedule_data_frame(base_schedule,
                                                               routing_session,
                                                               location_finder,
                                                               diary_by_date_by_carer[date],
                                                               observed_duration_by_visit)

            candidate_schedule_data_frame = get_schedule_data_frame(candidate_schedule,
                                                                    routing_session,
                                                                    location_finder,
                                                                    diary_by_date_by_carer[date],
                                                                    observed_duration_by_visit)


def debug(args, settings):
    problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
    solution_file = get_or_raise(args, __SOLUTION_FILE_ARG)
    schedule = load_schedule(solution_file)

    schedule_date = schedule.metadata.begin
    carer_dairies = {
        carer_shift.carer.sap_number:
            next((diary for diary in carer_shift.diaries if diary.date == schedule_date), None)
        for carer_shift in problem.carers}

    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()
    data_set = []
    with RoutingServer() as session:
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
                distance = session.distance(source_loc, destination_loc)
                if distance is None:
                    logging.error('Distance cannot be estimated between %s and %s', source_loc, destination_loc)
                    continue
                travel_time += datetime.timedelta(seconds=distance)
            service_time = datetime.timedelta()
            for visit in route.visits:
                if visit.check_in and visit.check_out:
                    observed_duration = visit.check_out - visit.check_in
                    if observed_duration.days < 0:
                        logging.error('Observed duration %s is negative', observed_duration)
                    service_time += observed_duration
                else:
                    logging.warning('Visit %s is not supplied with information on check-in and check-out information',
                                    visit.key)
                    service_time += visit.duration

            available_time = functools.reduce(operator.add, (event.duration
                                                             for event in carer_dairies[route.carer.sap_number].events))
            data_set.append([route.carer.sap_number,
                             available_time,
                             service_time,
                             travel_time,
                             float(service_time.total_seconds() + travel_time.total_seconds())
                             / available_time.total_seconds()])
    data_set.sort(key=operator.itemgetter(4))
    data_frame = pandas.DataFrame(columns=['Carer', 'Availability', 'Service', 'Travel', 'Usage'], data=data_set)

    figure, axis = matplotlib.pyplot.subplots()
    indices = numpy.arange(len(data_frame.index))
    time_delta_converter = TimeDeltaConverter()
    width = 0.35

    travel_series = numpy.array(time_delta_converter(data_frame.Travel))
    service_series = numpy.array(time_delta_converter(data_frame.Service))
    idle_overtime_series = list(data_frame.Availability - data_frame.Travel - data_frame.Service)
    idle_series = numpy.array(time_delta_converter(
        map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
    overtime_series = numpy.array(time_delta_converter(
        map(lambda value: datetime.timedelta(
            seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(), idle_overtime_series)))

    service_handle = axis.bar(indices, service_series, width, bottom=time_delta_converter.zero)
    travel_handle = axis.bar(indices, travel_series, width,
                             bottom=service_series + time_delta_converter.zero_num)
    idle_handle = axis.bar(indices, idle_series, width,
                           bottom=service_series + travel_series + time_delta_converter.zero_num)
    overtime_handle = axis.bar(indices, overtime_series, width,
                               bottom=idle_series + service_series + travel_series + time_delta_converter.zero_num)

    axis.yaxis_date()
    axis.yaxis.set_major_formatter(matplotlib.dates.DateFormatter("%H:%M:%S"))
    axis.legend((travel_handle, service_handle, idle_handle, overtime_handle),
                ('Travel', 'Service', 'Idle', 'Overtime'), loc='upper right')
    matplotlib.pyplot.show()
    pass


if __name__ == '__main__':
    sys.excepthook = handle_exception

    __script_file = os.path.realpath(__file__)
    __install_directory = os.path.dirname(__script_file)
    __settings = rows.settings.Settings(__install_directory)
    __settings.reload()

    __parser = configure_parser()
    __args = __parser.parse_args(sys.argv[1:])
    __command = getattr(__args, __COMMAND)
    if __command == __PULL_COMMAND:
        pull(__args, __settings)
    elif __command == __INFO_COMMAND:
        info(__args, __settings)
    elif __command == __COMPARE_DISTANCE_COMMAND:
        compare_distance(__args, __settings)
    elif __command == __COMPARE_WORKLOAD_COMMAND:
        compare_workload(__args, __settings)
    elif __command == __DEBUG_COMMAND:
        debug(__args, __settings)
    else:
        raise ValueError('Unknown command: ' + __command)
