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
import re
import subprocess
import sys

import bs4
import matplotlib
import matplotlib.dates
import matplotlib.pyplot
import matplotlib.ticker
import matplotlib.cm
import numpy
import pandas

import rows.console
import rows.location_finder
import rows.model.area
import rows.model.carer
import rows.model.json
import rows.model.location
import rows.model.metadata
import rows.model.past_visit
import rows.model.problem
import rows.model.schedule
import rows.model.service_user
import rows.model.visit
import rows.settings
import rows.sql_data_source


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
__COMPARE_TRACE_COMMAND = 'compare-trace'
__COST_FUNCTION_TYPE = 'cost_function'
__DEBUG_COMMAND = 'debug'
__AREA_ARG = 'area'
__FROM_ARG = 'from'
__TO_ARG = 'to'
__FILE_ARG = 'file'
__DATE_ARG = 'date'
__SOLUTION_FILE_ARG = 'solution'
__PROBLEM_FILE_ARG = 'problem'
__OUTPUT_PREFIX_ARG = 'output_prefix'
__OPTIONAL_ARG_PREFIX = '--'
__BASE_SCHEDULE_PATTERN = 'base_schedule_pattern'
__CANDIDATE_SCHEDULE_PATTERN = 'candidate_schedule_pattern'


def get_or_raise(obj, prop):
    value = getattr(obj, prop)
    if not value:
        raise ValueError('{0} not set'.format(prop))
    return value


def get_date_time(value):
    date_time = datetime.datetime.strptime(value, '%Y-%m-%d')
    return date_time


def get_date(value):
    value_to_use = get_date_time(value)
    return value_to_use.date()


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

    compare_trace_parser = subparsers.add_parser(__COMPARE_TRACE_COMMAND)
    compare_trace_parser.add_argument(__FILE_ARG)
    compare_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __COST_FUNCTION_TYPE, required=True)
    compare_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __DATE_ARG, type=get_date)

    return parser


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
                         / available_time.total_seconds()])
    data_set.sort(key=operator.itemgetter(4))
    return pandas.DataFrame(columns=['Carer', 'Availability', 'Service', 'Travel', 'Usage'], data=data_set)


def save_workforce_histogram(data_frame, file_path):
    figure, axis = matplotlib.pyplot.subplots()
    try:
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
                seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(),
                idle_overtime_series)))

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
        matplotlib.pyplot.savefig(file_path)
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def compare_workload(args, settings):
    __PLOT_EXT = '.png'
    problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
    diary_by_date_by_carer = collections.defaultdict(dict)
    for carer_shift in problem.carers:
        for diary in carer_shift.diaries:
            diary_by_date_by_carer[diary.date][carer_shift.carer.sap_number] = diary
    base_schedules = {load_schedule(file_path): file_path
                      for file_path in glob.glob(getattr(args, __BASE_SCHEDULE_PATTERN))}
    base_schedule_by_date = {schedule.metadata.begin: schedule for schedule in base_schedules}
    candidate_schedules = {load_schedule(file_path): file_path
                           for file_path in glob.glob(getattr(args, __CANDIDATE_SCHEDULE_PATTERN))}
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
            base_schedule = base_schedule_by_date.get(date, None)
            if not base_schedule:
                logging.error('No base schedule is available for %s', date)
                continue

            observed_duration_by_visit = VisitDict()
            for past_visit in base_schedule.visits:
                if past_visit.check_in and past_visit.check_out:
                    observed_duration = past_visit.check_out - past_visit.check_in
                    if observed_duration.days < 0:
                        logging.error('Observed duration %s is negative', observed_duration)
                else:
                    logging.warning(
                        'Visit %s is not supplied with information on check-in and check-out information',
                        past_visit.visit.key)
                    observed_duration = past_visit.duration
                observed_duration_by_visit[past_visit.visit] = observed_duration

            candidate_schedule = candidate_schedule_by_date.get(date, None)
            if not candidate_schedule:
                logging.error('No candidate schedule is available for %s', date)
                continue

            base_schedule_file = base_schedules[base_schedule]
            base_schedule_data_frame = get_schedule_data_frame(base_schedule,
                                                               routing_session,
                                                               location_finder,
                                                               diary_by_date_by_carer[date],
                                                               observed_duration_by_visit)

            base_schedule_stem, base_schedule_ext = os.path.splitext(base_schedule_file)
            save_workforce_histogram(base_schedule_data_frame, base_schedule_stem + __PLOT_EXT)

            candidate_schedule_file = candidate_schedules[candidate_schedule]
            candidate_schedule_data_frame = get_schedule_data_frame(candidate_schedule,
                                                                    routing_session,
                                                                    location_finder,
                                                                    diary_by_date_by_carer[date],
                                                                    observed_duration_by_visit)
            candidate_schedule_stem, candidate_schedule_ext = os.path.splitext(candidate_schedule_file)
            save_workforce_histogram(candidate_schedule_data_frame, candidate_schedule_stem + __PLOT_EXT)


def parse_time_delta(text):
    if text:
        time = datetime.datetime.strptime(text, '%H:%M:%S').time()
        return datetime.timedelta(hours=time.hour, minutes=time.minute, seconds=time.second)
    return None


class TraceLog:
    __STAGE_PATTERN = re.compile('^\w+(?P<number>\d+)$')

    class ProgressMessage:
        def __init__(self, **kwargs):
            self.__branches = kwargs.get('branches', None)
            self.__cost = kwargs.get('cost', None)
            self.__dropped_visits = kwargs.get('dropped_visits', None)
            self.__memory_usage = kwargs.get('memory_usage', None)
            self.__solutions = kwargs.get('solutions', None)
            self.__wall_time = parse_time_delta(kwargs.get('wall_time', None))

        @property
        def cost(self):
            return self.__cost

        @property
        def solutions(self):
            return self.__solutions

        @property
        def dropped_visits(self):
            return self.__dropped_visits

    class ProblemMessage:
        def __init__(self, **kwargs):
            self.__carers = kwargs.get('carers', None)
            self.__visits = kwargs.get('visits', None)
            self.__date = kwargs.get('date', None)
            if self.__date:
                self.__date = datetime.datetime.strptime(self.__date, '%Y-%b-%d').date()
            self.__visit_time_windows = parse_time_delta(kwargs.get('visit_time_windows', None))
            self.__break_time_windows = parse_time_delta(kwargs.get('break_time_windows', None))
            self.__shift_adjustment = parse_time_delta(kwargs.get('shift_adjustment', None))
            self.__area = kwargs.get('area', None)

        @property
        def date(self):
            return self.__date

        @property
        def carers(self):
            return self.__carers

        @property
        def visits(self):
            return self.__visits

    def __init__(self, time_point):
        self.__start = time_point
        self.__events = []
        self.__current_stage = None
        self.__problem = TraceLog.ProblemMessage()

    @staticmethod
    def __parse_stage_number(body):
        comment = body.get('comment', None)
        if comment:
            match = TraceLog.__STAGE_PATTERN.match(comment)
            if match:
                return int(match.group('number'))
        return None

    def append(self, time_point, body):
        if 'branches' in body:
            body_to_use = TraceLog.ProgressMessage(**body)
        elif 'type' in body:
            if body['type'] == 'started':
                self.__current_stage = self.__parse_stage_number(body)
            elif body['type'] == 'finished':
                self.__current_stage = None
            body_to_use = body
        elif 'area' in body:
            body_to_use = TraceLog.ProblemMessage(**body)
            self.__problem = body_to_use
        else:
            body_to_use = body
        self.__events.append([time_point - self.__start, self.__current_stage, time_point, body_to_use])

    def has_stages(self):
        for relative_time, stage, absolute_time, event in self.__events:
            if isinstance(event, TraceLog.ProblemMessage) or isinstance(event, TraceLog.ProgressMessage):
                continue
            if 'type' in event and event['type'] == 'started':
                return True
        return False

    @property
    def visits(self):
        return self.__problem.visits

    @property
    def carers(self):
        return self.__problem.carers

    @property
    def date(self):
        return self.__problem.date

    @property
    def events(self):
        return self.__events


def read_traces(trace_file):
    log_line_pattern = re.compile('^\w+\s+(?P<time>\d+:\d+:\d+\.\d+).*?]\s+(?P<body>.*)$')

    trace_logs = []
    has_preambule = False
    with open(trace_file, 'r') as input_stream:
        current_log = None
        for line in input_stream:
            match = log_line_pattern.match(line)
            if match:
                raw_time = match.group('time')
                time = datetime.datetime.strptime(raw_time, '%H:%M:%S.%f')
                try:
                    raw_body = match.group('body')
                    body = json.loads(raw_body)
                    if 'comment' in body and body['comment'] == 'All':
                        if 'type' in body:
                            if body['type'] == 'finished':
                                has_preambule = False
                            elif body['type'] == 'started':
                                has_preambule = True
                                current_log = TraceLog(time)
                                current_log.append(time, body)
                                trace_logs.append(current_log)
                    elif 'area' in body and not has_preambule:
                        current_log = TraceLog(time)
                        current_log.append(time, body)
                        trace_logs.append(current_log)
                    else:
                        current_log.append(time, body)
                except json.decoder.JSONDecodeError:
                    logging.warning('Failed to parse line: %s', line)
            else:
                logging.warning('Failed to match line: %s', line)
    return trace_logs


def traces_to_data_frame(trace_logs):
    columns = ['relative_time', 'cost', 'dropped_visits', 'solutions', 'stage', 'stage_started', 'date', 'carers',
               'visits']

    has_stages = [trace.has_stages() for trace in trace_logs]
    if all(has_stages) != any(has_stages):
        raise ValueError('Some traces have stages while others do not')
    has_stages = all(has_stages)

    data = []
    if has_stages:
        for trace in trace_logs:
            current_carers = None
            current_visits = None
            current_stage_started = None
            current_stage_name = None
            for rel_time, stage, abs_time, event in trace.events:
                if isinstance(event, TraceLog.ProblemMessage):
                    current_carers = event.carers
                    current_visits = event.visits
                elif isinstance(event, TraceLog.ProgressMessage):
                    if not current_stage_name:
                        continue
                    data.append([rel_time,
                                 event.cost, event.dropped_visits, event.solutions,
                                 current_stage_name, current_stage_started,
                                 trace.date, current_carers, current_visits])
                elif 'type' in event:
                    if event['type'] != 'started':
                        current_carers = None
                        current_visits = None
                        current_stage_started = None
                        current_stage_name = None
                    elif event['type'] == 'started':
                        current_stage_started = rel_time
                        current_stage_name = event['comment']
    else:
        for trace in trace_logs:
            current_carers = None
            current_visits = None
            for rel_time, stage, abs_time, event in trace.events:
                if isinstance(event, TraceLog.ProblemMessage):
                    current_carers = event.carers
                    current_visits = event.visits
                elif isinstance(event, TraceLog.ProgressMessage):
                    data.append([rel_time,
                                 event.cost, event.dropped_visits, event.solutions,
                                 None, None,
                                 trace.date, current_carers, current_visits])
    return pandas.DataFrame(data=data, columns=columns)


def compare_trace(args, settings):
    cost_function = get_or_raise(args, __COST_FUNCTION_TYPE)
    trace_file = get_or_raise(args, __FILE_ARG)
    trace_file_base_name = os.path.basename(trace_file)
    trace_file_stem, trace_file_ext = os.path.splitext(trace_file_base_name)

    trace_logs = read_traces(trace_file)
    data_frame = traces_to_data_frame(trace_logs)

    current_date = getattr(args, __DATE_ARG, None)
    dates = data_frame['date'].unique()
    if current_date and current_date not in dates:
        raise ValueError('Date {0} is not present in the data set'.format(current_date))

    def format_timedelta(x, pos=None):
        delta = datetime.timedelta(seconds=x)
        time_point = datetime.datetime(2017, 1, 1) + delta
        return time_point.strftime('%M:%S')

    def add_legend(axis, handles, bbox_to_anchor=(0.5, -0.23), ncol=3):
        first_row = handles[0]

        def legend_no_date(row):
            handle, visits, carers, cost_function = row
            return 'V{0:03} C{1:02} {2}'.format(visits, carers, cost_function)

        def legend_with_date(row):
            handle, multi_visits, visits, multi_carers, carers, cost_function, date = row
            date_time = datetime.datetime.combine(date, datetime.time())
            return 'V{0:02}/{1:03} C{2:02}/{3:02} {4} {5}' \
                .format(multi_visits, visits, multi_carers, carers, cost_function, date_time.strftime('%d-%m'))

        if len(first_row) == 4:
            legend_formatter = legend_no_date
        elif len(first_row) == 7:
            legend_formatter = legend_with_date
        else:
            raise ValueError('Expecting row of either 4 or 5 elements')

        legend = axis.legend(list(map(operator.itemgetter(0), handles)),
                             list(map(legend_formatter, handles)),
                             loc='lower center',
                             ncol=ncol,
                             bbox_to_anchor=bbox_to_anchor,
                             fancybox=None,
                             edgecolor=None,
                             handletextpad=0.1,
                             columnspacing=0.15)
        for handle in legend.legendHandles:
            handle.set_sizes([16.0])

    __SCATTER_POINT_SIZE = 1
    __FILE_FORMAT = 'svg'
    __Y_AXIS_EXTENSION = 1.2

    def scatter_cost(axis, data_frame):
        return axis.scatter(
            [time_delta.total_seconds() for time_delta in data_frame['relative_time']], data_frame['cost'],
            s=__SCATTER_POINT_SIZE)

    def scatter_dropped_visits(axis, data_frame):
        axis.scatter(
            [time_delta.total_seconds() for time_delta in data_frame['relative_time']],
            data_frame['dropped_visits'],
            s=__SCATTER_POINT_SIZE)

    def draw_avline(axis, point, color='lightgrey', linestyle='--'):
        axis.axvline(point, color=color, linestyle=linestyle, linewidth=0.8, alpha=0.8)

    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    try:
        if current_date:
            current_date_frame = data_frame[data_frame['date'] == current_date]
            stages = current_date_frame['stage'].unique()
            if len(stages) > 1:
                handles = []
                for stage in stages:
                    time_delta = current_date_frame[current_date_frame['stage'] == stage]['stage_started'].iloc[0]
                    current_stage_data_frame = current_date_frame[current_date_frame['stage'] == stage]
                    draw_avline(ax1, time_delta.total_seconds())
                    draw_avline(ax2, time_delta.total_seconds())
                    visits = current_stage_data_frame['visits'].iloc[0]
                    carers = current_stage_data_frame['carers'].iloc[0]
                    handle = scatter_cost(ax1, current_date_frame)
                    scatter_dropped_visits(ax2, current_stage_data_frame)
                    handles.append([handle, visits, carers, cost_function])
                add_legend(ax1, handles)

                ax2.set_xlim(left=0)
                ax2.set_ylim(bottom=0)
                ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            else:
                visits = current_date_frame['visits'].iloc[0]
                carers = current_date_frame['carers'].iloc[0]
                handle = ax1.scatter(
                    [time_delta.total_seconds() for time_delta in current_date_frame['relative_time']],
                    current_date_frame['cost'], s=1)
                add_legend(ax1, [[handle, visits, carers, cost_function]])
                scatter_dropped_visits(ax2, current_date_frame)

            ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
            ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

            logging.warning('Extending the x axis of the plot by 2 minutes')
            x_left, x_right = ax1.get_xlim()
            updated_x_right = x_right + 2 * 60

            ax1_y_bottom, ax1_y_top = ax1.get_ylim()
            ax1.set_ylim(bottom=0, top=ax1_y_top * __Y_AXIS_EXTENSION)
            ax1.set_xlim(left=0, right=updated_x_right)

            ax2_y_bottom, ax2_y_top = ax2.get_ylim()
            ax2.set_ylim(bottom=0, top=ax2_y_top * __Y_AXIS_EXTENSION)
            ax2.set_xlim(left=0, right=updated_x_right)
            ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            matplotlib.pyplot.savefig(trace_file_stem + '_' + current_date.isoformat() + '.' + __FILE_FORMAT,
                                      format=__FILE_FORMAT,
                                      dpi=1200)
        else:
            handles = []
            color_number = 0
            color_map = matplotlib.cm.get_cmap('tab20')
            for current_date in dates:
                current_date_frame = data_frame[data_frame['date'] == current_date]
                stages = current_date_frame['stage'].unique()
                if len(stages) > 1:
                    stage_linestyles = [None, 'dotted', 'dashed']
                    for stage, linestyle in zip(stages, stage_linestyles):
                        time_delta = current_date_frame[current_date_frame['stage'] == stage]['stage_started'].iloc[0]
                        draw_avline(ax1, time_delta.total_seconds(),
                                    color=color_map.colors[color_number],
                                    linestyle=linestyle)
                        draw_avline(ax2, time_delta.total_seconds(),
                                    color=color_map.colors[color_number],
                                    linestyle=linestyle)

                    total_carers = current_date_frame['carers'].max()
                    multi_carers = current_date_frame['carers'].min()
                    if multi_carers == total_carers:
                        multi_carers = 0

                    total_visits = current_date_frame['visits'].max()
                    multi_visits = current_date_frame['visits'].min()
                    if multi_visits == total_visits:
                        multi_visits = 0

                    handle = scatter_cost(ax1, current_date_frame)
                    scatter_dropped_visits(ax2, current_date_frame)
                    handles.append([handle,
                                    multi_visits,
                                    total_visits,
                                    multi_carers,
                                    total_carers,
                                    cost_function,
                                    current_date])
                else:
                    visits = current_date_frame['visits'].iloc[0]
                    carers = current_date_frame['carers'].iloc[0]
                    handle = ax1.scatter(
                        [time_delta.total_seconds() for time_delta in current_date_frame['relative_time']],
                        current_date_frame['cost'], s=1)
                    handles.append([handle, visits, carers, cost_function, current_date])
                    scatter_dropped_visits(ax2, current_date_frame)
                color_number += 1

            ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
            ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

            logging.warning('Extending the x axis of the plot by 2 minutes')
            x_left, x_right = ax1.get_xlim()
            updated_x_right = x_right + 2 * 60

            ax1_y_bottom, ax1_y_top = ax1.get_ylim()
            ax1.set_ylim(bottom=0, top=ax1_y_top * __Y_AXIS_EXTENSION)
            ax1.set_xlim(left=0, right=updated_x_right)

            legend = add_legend(ax2, handles, bbox_to_anchor=(0.5, -1.6), ncol=2)

            ax2_y_bottom, ax2_y_top = ax2.get_ylim()
            ax2.set_ylim(bottom=0, top=ax2_y_top * __Y_AXIS_EXTENSION)
            ax2.set_xlim(left=0, right=updated_x_right)
            ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            matplotlib.pyplot.set_cmap(color_map)
            matplotlib.pyplot.tight_layout()
            figure.subplots_adjust(bottom=0.4)
            matplotlib.pyplot.savefig(trace_file_stem + '.' + __FILE_FORMAT,
                                      format=__FILE_FORMAT,
                                      dpi=1200,
                                      bbox_extra_artists=(legend,),
                                      layout='tight')
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


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
                    logging.warning(
                        'Visit %s is not supplied with information on check-in and check-out information',
                        visit.key)
                    service_time += visit.duration

            available_time = functools.reduce(operator.add, (event.duration
                                                             for event in
                                                             carer_dairies[route.carer.sap_number].events))
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
    elif __command == __COMPARE_TRACE_COMMAND:
        compare_trace(__args, __settings)
    elif __command == __DEBUG_COMMAND:
        debug(__args, __settings)
    else:
        raise ValueError('Unknown command: ' + __command)
