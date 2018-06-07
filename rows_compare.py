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
__COMPARE_COMMAND = 'compare'
__DEBUG_COMMAND = 'debug'
__AREA_ARG = 'area'
__FROM_ARG = 'from'
__TO_ARG = 'to'
__FILE_ARG = 'file'
__SOLUTION_FILE_ARG = 'solution'
__PROBLEM_FILE_ARG = 'problem'
__OUTPUT_PREFIX_ARG = 'output_prefix'
__OPTIONAL_ARG_PREFIX = '--'
__LEFT_SCHEDULE_PATTERN_ARG = 'left_schedule_pattern'
__RIGHT_SCHEDULE_PATTERN_ARG = 'right_schedule_pattern'


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

    compare_parser = subparsers.add_parser(__COMPARE_COMMAND)
    compare_parser.add_argument(__LEFT_SCHEDULE_PATTERN_ARG)
    compare_parser.add_argument(__RIGHT_SCHEDULE_PATTERN_ARG)

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


def compare(args, settings):
    left_series = [load_schedule(file_path) for file_path in glob.glob(getattr(args, __LEFT_SCHEDULE_PATTERN_ARG))]
    left_series.sort(key=operator.attrgetter('metadata.begin'))
    right_series = [load_schedule(file_path) for file_path in glob.glob(getattr(args, __RIGHT_SCHEDULE_PATTERN_ARG))]
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

    indices = numpy.array(list(map(matplotlib.dates.date2num, data_frame.index)))
    width = 0.35
    zero = datetime.datetime(2018, 1, 1)
    zero_num = matplotlib.dates.date2num(zero)
    figure, axis = matplotlib.pyplot.subplots()
    human_handle = axis.bar(indices,
                            [matplotlib.dates.date2num(zero + duration) - zero_num
                             for duration in data_frame.HumanPlanners], width, bottom=zero)
    cp_handle = axis.bar(indices + width,
                         [matplotlib.dates.date2num(zero + duration) - zero_num for duration in
                          data_frame.ConstraintProgramming], width, bottom=zero)
    axis.xaxis_date()
    axis.yaxis_date()
    axis.yaxis.set_major_formatter(matplotlib.dates.DateFormatter("%d,%H:%M:%S"))
    axis.legend((human_handle, cp_handle), ('Human Planners', 'Constraint Programming'), loc='upper right')
    matplotlib.pyplot.show()


def debug(args, settings):
    problem_file = get_or_raise(args, __PROBLEM_FILE_ARG)
    solution_file = get_or_raise(args, __SOLUTION_FILE_ARG)
    schedule = load_schedule(solution_file)
    problem = load_problem(problem_file)

    schedule_date = schedule.metadata.begin
    carer_dairies = [
        (carer_shift.carer, next((diary for diary in carer_shift.diaries if diary.date == schedule_date), None))
        for carer_shift in problem.carers]
    carer_availability = {}
    for carer, diary in carer_dairies:
        if not diary:
            continue
        carer_availability[carer] = functools.reduce(operator.add, (event.duration for event in diary.events))
    routes = schedule.routes()

    # list carers
    # get carers available time


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
    elif __command == __COMPARE_COMMAND:
        compare(__args, __settings)
    elif __command == __DEBUG_COMMAND:
        debug(__args, __settings)
    else:
        raise ValueError('Unknown command: ' + __command)
