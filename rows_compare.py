#!/usr/bin/env python3

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
import rows.routing_server
import rows.plot


def handle_exception(exc_type, exc_value, exc_traceback):
    """Logs uncaught exceptions"""

    if issubclass(exc_type, KeyboardInterrupt):
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
    else:
        logging.error("Uncaught exception", exc_info=(exc_type, exc_value, exc_traceback))


__COMMAND = 'command'
__PULL_COMMAND = 'pull'
__INFO_COMMAND = 'info'
__SHOW_WORKING_HOURS_COMMAND = 'show-working-hours'
__COMPARE_DISTANCE_COMMAND = 'compare-distance'
__COMPARE_WORKLOAD_COMMAND = 'compare-workload'
__CONTRAST_WORKLOAD_COMMAND = 'contrast-workload'
__COMPARE_PREDICTION_ERROR_COMMAND = 'compare-prediction-error'
__TYPE_ARG = 'type'
__ACTIVITY_TYPE = 'activity'
__VISITS_TYPE = 'visits'
__COMPARE_TRACE_COMMAND = 'compare-trace'
__CONTRAST_TRACE_COMMAND = 'contrast-trace'
__COST_FUNCTION_TYPE = 'cost_function'
__DEBUG_COMMAND = 'debug'
__AREA_ARG = 'area'
__FROM_ARG = 'from'
__TO_ARG = 'to'
__FILE_ARG = 'file'
__DATE_ARG = 'date'
__BASE_FILE_ARG = 'base-file'
__CANDIDATE_FILE_ARG = 'candidate-file'
__SOLUTION_FILE_ARG = 'solution'
__PROBLEM_FILE_ARG = 'problem'
__OUTPUT_PREFIX_ARG = 'output_prefix'
__OPTIONAL_ARG_PREFIX = '--'
__BASE_SCHEDULE_PATTERN = 'base_schedule_pattern'
__CANDIDATE_SCHEDULE_PATTERN = 'candidate_schedule_pattern'
__SCHEDULE_PATTERNS = 'schedule_patterns'
__LABELS = 'labels'
__OUTPUT = 'output'
__ARROWS = 'arrows'


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


def find_file_or_fail(file_paths):
    for raw_path in file_paths:
        path = os.path.expanduser(raw_path)
        path = os.path.expandvars(path)
        if os.path.isfile(path):
            return path
    if file_paths:
        raise ValueError('Failed to find ' + file_paths[0])
    raise ValueError()


def add_legend(axis, handles, labels, ncol, bbox_to_anchor):
    legend = axis.legend(handles,
                         labels,
                         loc='lower center',
                         ncol=ncol,
                         bbox_to_anchor=bbox_to_anchor,
                         fancybox=None,
                         edgecolor=None,
                         handletextpad=0.1,
                         columnspacing=0.15)
    return legend


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
    compare_distance_parser.add_argument(__OPTIONAL_ARG_PREFIX + __PROBLEM_FILE_ARG, required=True)
    compare_distance_parser.add_argument(__OPTIONAL_ARG_PREFIX + __SCHEDULE_PATTERNS, nargs='+', required=True)
    compare_distance_parser.add_argument(__OPTIONAL_ARG_PREFIX + __LABELS, nargs='+', required=True)
    compare_distance_parser.add_argument(__OPTIONAL_ARG_PREFIX + __OUTPUT)

    compare_workload_parser = subparsers.add_parser(__COMPARE_WORKLOAD_COMMAND)
    compare_workload_parser.add_argument(__PROBLEM_FILE_ARG)
    compare_workload_parser.add_argument(__BASE_SCHEDULE_PATTERN)
    compare_workload_parser.add_argument(__CANDIDATE_SCHEDULE_PATTERN)

    debug_parser = subparsers.add_parser(__DEBUG_COMMAND)
    debug_parser.add_argument(__PROBLEM_FILE_ARG)
    debug_parser.add_argument(__SOLUTION_FILE_ARG)

    compare_trace_parser = subparsers.add_parser(__COMPARE_TRACE_COMMAND)
    compare_trace_parser.add_argument(__PROBLEM_FILE_ARG)
    compare_trace_parser.add_argument(__FILE_ARG)
    compare_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __COST_FUNCTION_TYPE, required=True)
    compare_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __DATE_ARG, type=get_date)
    compare_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __OUTPUT)
    compare_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __ARROWS, type=bool, default=False)

    contrast_workload_parser = subparsers.add_parser(__CONTRAST_WORKLOAD_COMMAND)
    contrast_workload_parser.add_argument(__PROBLEM_FILE_ARG)
    contrast_workload_parser.add_argument(__BASE_FILE_ARG)
    contrast_workload_parser.add_argument(__CANDIDATE_FILE_ARG)
    contrast_workload_parser.add_argument(__OPTIONAL_ARG_PREFIX + __TYPE_ARG)

    compare_prediction_error_parser = subparsers.add_parser(__COMPARE_PREDICTION_ERROR_COMMAND)
    compare_prediction_error_parser.add_argument(__BASE_FILE_ARG)
    compare_prediction_error_parser.add_argument(__CANDIDATE_FILE_ARG)

    contrast_trace_parser = subparsers.add_parser(__CONTRAST_TRACE_COMMAND)
    contrast_trace_parser.add_argument(__PROBLEM_FILE_ARG)
    contrast_trace_parser.add_argument(__BASE_FILE_ARG)
    contrast_trace_parser.add_argument(__CANDIDATE_FILE_ARG)
    contrast_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __DATE_ARG, type=get_date, required=True)
    contrast_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __COST_FUNCTION_TYPE, required=True)
    contrast_trace_parser.add_argument(__OPTIONAL_ARG_PREFIX + __OUTPUT)

    show_working_hours_parser = subparsers.add_parser(__SHOW_WORKING_HOURS_COMMAND)
    show_working_hours_parser.add_argument(__FILE_ARG)
    show_working_hours_parser.add_argument(__OPTIONAL_ARG_PREFIX + __OUTPUT)

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


def get_travel_time(schedule, user_tag_finder):
    routes = schedule.routes()
    total_travel_time = datetime.timedelta()
    with create_routing_session() as session:
        for route in routes:
            visit_it = iter(route.visits)

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


def info(args, settings):
    user_tag_finder = rows.location_finder.UserLocationFinder(settings)
    user_tag_finder.reload()
    schedule_file = get_or_raise(args, __FILE_ARG)
    schedule_file_to_use = os.path.realpath(os.path.expandvars(schedule_file))
    schedule = rows.plot.load_schedule(schedule_file_to_use)
    carers = {visit.carer for visit in schedule.visits}
    print(get_travel_time(schedule, user_tag_finder), len(carers), len(schedule.visits))


def load_problem(problem_file):
    with open(problem_file, 'r') as input_stream:
        problem_json = json.load(input_stream)
        return rows.model.problem.Problem.from_json(problem_json)


class CumulativeHourMinuteConverter:
    def __init__(self):
        self.__REFERENCE = matplotlib.dates.date2num(datetime.datetime(2018, 1, 1))

    def __call__(self, x, pos=None):
        time_delta = matplotlib.dates.num2timedelta(x - self.__REFERENCE)
        hours = int(time_delta.total_seconds() // matplotlib.dates.SEC_PER_HOUR)
        minutes = int((time_delta.total_seconds() - hours * matplotlib.dates.SEC_PER_HOUR) \
                      // matplotlib.dates.SEC_PER_MIN)
        return '{0:02d}:{1:02d}:00'.format(hours, minutes)


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

    def convert(self, value):
        return matplotlib.dates.date2num(self.__zero + value) - self.__zero_num

    def __call__(self, series):
        return [self.convert(value) for value in series]


def create_routing_session():
    return rows.routing_server.RoutingServer(server_executable=find_file_or_fail(
        ['~/dev/cordia/build/rows-routing-server', './build/rows-routing-server']),
        maps_file=find_file_or_fail(
            ['~/dev/cordia/data/scotland-latest.osrm', './data/scotland-latest.osrm']))


def compare_distance(args, settings):
    schedule_patterns = getattr(args, __SCHEDULE_PATTERNS)
    labels = getattr(args, __LABELS)
    output_file = getattr(args, __OUTPUT, 'distance')

    data_frame_file = 'data_frame_cache.bin'

    if os.path.isfile(data_frame_file):
        data_frame = pandas.read_pickle(data_frame_file)
    else:
        user_tag_finder = rows.location_finder.UserLocationFinder(settings)
        user_tag_finder.reload()

        location_finder = rows.location_finder.UserLocationFinder(settings)
        location_finder.reload()

        problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
        observed_duration_by_visit = calculate_forecast_visit_duration(problem)

        diary_by_date_by_carer = collections.defaultdict(dict)
        for carer_shift in problem.carers:
            for diary in carer_shift.diaries:
                diary_by_date_by_carer[diary.date][carer_shift.carer.sap_number] = diary

        store = []
        with create_routing_session() as routing_session:
            for label, schedule_pattern in zip(labels, schedule_patterns):
                for schedule_path in glob.glob(schedule_pattern):
                    schedule = rows.plot.load_schedule(schedule_path)
                    frame = rows.plot.get_schedule_data_frame(schedule,
                                                              routing_session,
                                                              location_finder,
                                                              diary_by_date_by_carer[schedule.metadata.begin],
                                                              observed_duration_by_visit)
                    visits = frame['Visits'].sum()
                    carers = len(frame.where(frame['Visits'] > 0))
                    idle_time = frame['Availability'] - frame['Travel'] - frame['Service']
                    idle_time[idle_time < pandas.Timedelta(0)] = pandas.Timedelta(0)
                    overtime = frame['Travel'] + frame['Service'] - frame['Availability']
                    overtime[overtime < pandas.Timedelta(0)] = pandas.Timedelta(0)
                    store.append({
                        'Label': label,
                        'Date': schedule.metadata.begin,
                        'Availability': frame['Availability'].sum(),
                        'Travel': frame['Travel'].sum(),
                        'Service': frame['Service'].sum(),
                        'Idle': idle_time.sum(),
                        'Overtime': overtime.sum(),
                        'Carers': carers,
                        'Visits': visits
                    })

        data_frame = pandas.DataFrame(store)
        data_frame.sort_values(by=['Date'], inplace=True)
        data_frame.to_pickle(data_frame_file)

    data_frame.to_csv('table.csv')
    figure, (ax1, ax2, ax3) = matplotlib.pyplot.subplots(3, 1, sharex=True)
    try:
        width = 0.20
        dates = data_frame['Date'].unique()
        time_delta_convert = TimeDeltaConverter()
        indices = numpy.arange(1, len(dates) + 1, 1)

        handles = []
        position = 0
        for label in labels:
            data_frame_to_use = data_frame[data_frame['Label'] == label]

            handle = ax1.bar(indices + position * width,
                             time_delta_convert(data_frame_to_use['Travel']),
                             width,
                             bottom=time_delta_convert.zero)

            ax2.bar(indices + position * width,
                    time_delta_convert(data_frame_to_use['Idle']),
                    width,
                    bottom=time_delta_convert.zero)

            ax3.bar(indices + position * width,
                    time_delta_convert(data_frame_to_use['Overtime']),
                    width,
                    bottom=time_delta_convert.zero)

            handles.append(handle)
            position += 1

        ax1.yaxis_date()
        ax1.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
        ax1.set_ylabel('Travel Time')

        ax2.yaxis_date()
        ax2.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
        ax2.set_ylabel('Idle Time')

        ax3.yaxis_date()
        ax3.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
        ax3.set_ylabel('Overtime')
        ax3.set_xlabel('Day of October 2017')

        legend = add_legend(ax3, handles, labels, ncol=3, bbox_to_anchor=(0.5, -0.68))

        matplotlib.pyplot.tight_layout()

        figure.subplots_adjust(bottom=0.15)

        matplotlib.pyplot.savefig(output_file + '.' + __FILE_FORMAT,
                                  format=__FILE_FORMAT,
                                  transparent=True,
                                  bbox_extra_artists=(legend,),
                                  dpi=300)
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def save_workforce_histogram(data_frame, file_path):
    __width = 0.35
    figure, axis = matplotlib.pyplot.subplots()
    try:
        indices = numpy.arange(len(data_frame.index))
        time_delta_converter = TimeDeltaConverter()

        travel_series = numpy.array(time_delta_converter(data_frame.Travel))
        service_series = numpy.array(time_delta_converter(data_frame.Service))
        idle_overtime_series = list(data_frame.Availability - data_frame.Travel - data_frame.Service)
        idle_series = numpy.array(time_delta_converter(
            map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
        overtime_series = numpy.array(time_delta_converter(
            map(lambda value: datetime.timedelta(
                seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(),
                idle_overtime_series)))

        service_handle = axis.bar(indices, service_series, __width, bottom=time_delta_converter.zero)
        travel_handle = axis.bar(indices, travel_series, __width,
                                 bottom=service_series + time_delta_converter.zero_num)
        idle_handle = axis.bar(indices, idle_series, __width,
                               bottom=service_series + travel_series + time_delta_converter.zero_num)
        overtime_handle = axis.bar(indices, overtime_series, __width,
                                   bottom=idle_series + service_series + travel_series + time_delta_converter.zero_num)

        axis.yaxis_date()
        axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
        axis.set_xlabel('Carer')
        axis.set_ylabel('Time Delta')
        legend = add_legend(axis,
                            [travel_handle, service_handle, idle_handle, overtime_handle],
                            ['Travel Time', 'Service Time', 'Idle Time', 'Overtime'],
                            ncol=4,
                            bbox_to_anchor=(0.5, -0.2))

        matplotlib.pyplot.tight_layout()

        figure.subplots_adjust(bottom=0.15)

        matplotlib.pyplot.savefig(file_path,
                                  format=__FILE_FORMAT,
                                  transparent=True,
                                  dpi=300,
                                  bbox_extra_artists=(legend,))
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def save_combined_histogram(top_data_frame, bottom_data_frame, labels, file_path):
    __width = 0.35

    y_label1, y_label2 = labels
    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    max_y = max((top_data_frame['Service'] + top_data_frame['Travel']).max(),
                (bottom_data_frame['Service'] + bottom_data_frame['Travel']).max())

    try:
        time_delta_converter = TimeDeltaConverter()

        # put the same scale limits
        for axis, data_frame, y_label in [(ax1, top_data_frame, y_label1), (ax2, bottom_data_frame, y_label2)]:
            travel_series = numpy.array(time_delta_converter(data_frame.Travel))
            service_series = numpy.array(time_delta_converter(data_frame.Service))
            idle_overtime_series = list(data_frame.Availability - data_frame.Travel - data_frame.Service)
            idle_series = numpy.array(time_delta_converter(
                map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
            overtime_series = numpy.array(time_delta_converter(
                map(lambda value: datetime.timedelta(
                    seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(),
                    idle_overtime_series)))

            indices = numpy.arange(len(data_frame.index))
            service_handle = axis.bar(indices, service_series, __width, bottom=time_delta_converter.zero)
            travel_handle = axis.bar(indices, travel_series, __width,
                                     bottom=service_series + time_delta_converter.zero_num)
            idle_handle = axis.bar(indices, idle_series, __width,
                                   bottom=service_series + travel_series + time_delta_converter.zero_num)
            overtime_handle = axis.bar(indices, overtime_series, __width,
                                       bottom=idle_series + service_series + travel_series + time_delta_converter.zero_num)

            axis.yaxis_date()
            axis.set_ylim(top=time_delta_converter.convert(max_y) + time_delta_converter.zero_num)
            axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
            axis.set_ylabel('Time Delta')
            axis.text(1.02, 0.5, y_label,
                      horizontalalignment='left',
                      verticalalignment='center',
                      rotation=90,
                      clip_on=False,
                      transform=axis.transAxes)

        ax2.set_xlabel('Carer')
        legend = add_legend(axis,
                            [travel_handle, service_handle, idle_handle, overtime_handle],
                            ['Travel Time', 'Service Time', 'Idle Time', 'Overtime'],
                            ncol=4,
                            bbox_to_anchor=(0.5, -0.4))

        matplotlib.pyplot.tight_layout()

        figure.subplots_adjust(bottom=0.15, right=0.96, hspace=0.05)

        matplotlib.pyplot.savefig(file_path,
                                  format=__FILE_FORMAT,
                                  transparent=True,
                                  dpi=300,
                                  bbox_extra_artists=(legend,))
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def calculate_forecast_visit_duration(problem):
    forecast_visit_duration = rows.plot.VisitDict()
    for recurring_visits in problem.visits:
        for local_visit in recurring_visits.visits:
            forecast_visit_duration[local_visit] = local_visit.duration
    return forecast_visit_duration


def calculate_observed_visit_duration(schedule):
    observed_duration_by_visit = rows.plot.VisitDict()
    for past_visit in schedule.visits:
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
    return observed_duration_by_visit


def calculate_expected_visit_duration(schedule):
    expected_visit_duration = rows.plot.VisitDict()
    for past_visit in schedule.visits:
        expected_visit_duration[past_visit.visit] = past_visit.duration
    return expected_visit_duration


def compare_workload(args, settings):
    problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
    diary_by_date_by_carer = collections.defaultdict(dict)
    for carer_shift in problem.carers:
        for diary in carer_shift.diaries:
            diary_by_date_by_carer[diary.date][carer_shift.carer.sap_number] = diary
    base_schedules = {rows.plot.load_schedule(file_path): file_path
                      for file_path in glob.glob(getattr(args, __BASE_SCHEDULE_PATTERN))}
    base_schedule_by_date = {schedule.metadata.begin: schedule for schedule in base_schedules}
    candidate_schedules = {rows.plot.load_schedule(file_path): file_path
                           for file_path in glob.glob(getattr(args, __CANDIDATE_SCHEDULE_PATTERN))}
    candidate_schedule_by_date = {schedule.metadata.begin: schedule for schedule in candidate_schedules}
    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()

    dates = set(candidate_schedule_by_date.keys())
    for date in base_schedule_by_date.keys():
        dates.add(date)
    dates = list(dates)
    dates.sort()

    with create_routing_session() as routing_session:
        for date in dates:
            base_schedule = base_schedule_by_date.get(date, None)
            if not base_schedule:
                logging.error('No base schedule is available for %s', date)
                continue

            observed_duration_by_visit = calculate_observed_visit_duration(base_schedule)

            candidate_schedule = candidate_schedule_by_date.get(date, None)
            if not candidate_schedule:
                logging.error('No candidate schedule is available for %s', date)
                continue

            base_schedule_file = base_schedules[base_schedule]
            base_schedule_data_frame = rows.plot.get_schedule_data_frame(base_schedule,
                                                                         routing_session,
                                                                         location_finder,
                                                                         diary_by_date_by_carer[date],
                                                                         observed_duration_by_visit)

            base_schedule_stem, base_schedule_ext = os.path.splitext(os.path.basename(base_schedule_file))
            save_workforce_histogram(base_schedule_data_frame, base_schedule_stem + '.' + __FILE_FORMAT)

            candidate_schedule_file = candidate_schedules[candidate_schedule]
            candidate_schedule_data_frame = rows.plot.get_schedule_data_frame(candidate_schedule,
                                                                              routing_session,
                                                                              location_finder,
                                                                              diary_by_date_by_carer[date],
                                                                              observed_duration_by_visit)
            candidate_schedule_stem, candidate_schedule_ext = os.path.splitext(
                os.path.basename(candidate_schedule_file))
            save_workforce_histogram(candidate_schedule_data_frame, candidate_schedule_stem + '.' + __FILE_FORMAT)
            save_combined_histogram(candidate_schedule_data_frame,
                                    base_schedule_data_frame,
                                    ['2nd Stage', '3rd Stage'],
                                    'contrast_workforce_{0}_combined.{1}'.format(date, __FILE_FORMAT))


def contrast_workload(args, settings):
    __WIDTH = 0.35
    __FORMAT = 'svg'

    plot_type = getattr(args, __TYPE_ARG, None)
    if plot_type != __ACTIVITY_TYPE and plot_type != __VISITS_TYPE:
        raise ValueError(
            'Unknown plot type: {0}. Use either {1} or {2}.'.format(plot_type, __ACTIVITY_TYPE, __VISITS_TYPE))

    problem_file = get_or_raise(args, __PROBLEM_FILE_ARG)
    problem = load_problem(problem_file)
    base_schedule = rows.plot.load_schedule(get_or_raise(args, __BASE_FILE_ARG))
    candidate_schedule = rows.plot.load_schedule(get_or_raise(args, __CANDIDATE_FILE_ARG))

    if base_schedule.metadata.begin != candidate_schedule.metadata.begin:
        raise ValueError('Schedules begin at a different date: {0} vs {1}'
                         .format(base_schedule.metadata.begin, candidate_schedule.metadata.begin))

    if base_schedule.metadata.end != candidate_schedule.metadata.end:
        raise ValueError('Schedules end at a different date: {0} vs {1}'
                         .format(base_schedule.metadata.end, candidate_schedule.metadata.end))

    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()

    diary_by_date_by_carer = collections.defaultdict(dict)
    for carer_shift in problem.carers:
        for diary in carer_shift.diaries:
            diary_by_date_by_carer[diary.date][carer_shift.carer.sap_number] = diary

    date = base_schedule.metadata.begin
    problem_file_base = os.path.basename(problem_file)
    problem_file_name, problem_file_ext = os.path.splitext(problem_file_base)

    with create_routing_session() as routing_session:
        observed_duration_by_visit = calculate_expected_visit_duration(candidate_schedule)
        base_schedule_frame = rows.plot.get_schedule_data_frame(base_schedule,
                                                                routing_session,
                                                                location_finder,
                                                                diary_by_date_by_carer[date],
                                                                observed_duration_by_visit)
        candidate_schedule_frame = rows.plot.get_schedule_data_frame(candidate_schedule,
                                                                     routing_session,
                                                                     location_finder,
                                                                     diary_by_date_by_carer[date],
                                                                     observed_duration_by_visit)

    color_map = matplotlib.cm.get_cmap('tab20')
    matplotlib.pyplot.set_cmap(color_map)
    figure, axis = matplotlib.pyplot.subplots()
    matplotlib.pyplot.tight_layout()
    try:
        contrast_frame = pandas.DataFrame.merge(base_schedule_frame,
                                                candidate_schedule_frame,
                                                on='Carer',
                                                how='left',
                                                suffixes=['_Base', '_Candidate'])
        contrast_frame['Visits_Candidate'] = contrast_frame['Visits_Candidate'].fillna(0)
        contrast_frame['Availability_Candidate'] \
            = contrast_frame['Availability_Candidate'].mask(pandas.isnull, contrast_frame['Availability_Base'])
        contrast_frame['Travel_Candidate'] \
            = contrast_frame['Travel_Candidate'].mask(pandas.isnull, datetime.timedelta())
        contrast_frame['Service_Candidate'] \
            = contrast_frame['Service_Candidate'].mask(pandas.isnull, datetime.timedelta())
        contrast_frame = contrast_frame.sort_values(
            by=['Availability_Candidate', 'Service_Candidate', 'Travel_Candidate'],
            ascending=False)
        if plot_type == __VISITS_TYPE:
            indices = numpy.arange(len(contrast_frame.index))
            base_handle = axis.bar(indices, contrast_frame['Visits_Base'], __WIDTH)
            candidate_handle = axis.bar(indices + __WIDTH, contrast_frame['Visits_Candidate'], __WIDTH)
            axis.legend((base_handle, candidate_handle),
                        ('Human Planners', 'Constraint Programming'), loc='best')
            output_file = problem_file_name + '_contrast_visits_' + date.isoformat() + '.' + __FORMAT
        elif plot_type == __ACTIVITY_TYPE:
            indices = numpy.arange(len(base_schedule_frame.index))

            def plot_activity_stacked_histogram(availability, travel, service, axis, width=0.35, initial_width=0.0,
                                                color_offset=0):
                time_delta_converter = TimeDeltaConverter()
                travel_series = numpy.array(time_delta_converter(travel))
                service_series = numpy.array(time_delta_converter(service))
                idle_overtime_series = list(availability - travel - service)
                idle_series = numpy.array(time_delta_converter(
                    map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
                overtime_series = numpy.array(time_delta_converter(
                    map(lambda value: datetime.timedelta(
                        seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(),
                        idle_overtime_series)))
                service_handle = axis.bar(indices + initial_width, service_series,
                                          width,
                                          bottom=time_delta_converter.zero,
                                          color=color_map.colors[0 + color_offset])
                travel_handle = axis.bar(indices + initial_width,
                                         travel_series,
                                         width,
                                         bottom=service_series + time_delta_converter.zero_num,
                                         color=color_map.colors[2 + color_offset])
                idle_handle = axis.bar(indices + initial_width,
                                       idle_series,
                                       width,
                                       bottom=service_series + travel_series + time_delta_converter.zero_num,
                                       color=color_map.colors[4 + color_offset])
                overtime_handle = axis.bar(indices + initial_width,
                                           overtime_series,
                                           width,
                                           bottom=idle_series + service_series + travel_series + time_delta_converter.zero_num,
                                           color=color_map.colors[6 + color_offset])
                return service_handle, travel_handle, idle_handle, overtime_handle

            travel_candidate_handle, service_candidate_handle, idle_candidate_handle, overtime_candidate_handle \
                = plot_activity_stacked_histogram(contrast_frame.Availability_Candidate,
                                                  contrast_frame.Travel_Candidate,
                                                  contrast_frame.Service_Candidate,
                                                  axis,
                                                  __WIDTH)

            travel_base_handle, service_base_handle, idle_base_handle, overtime_base_handle \
                = plot_activity_stacked_histogram(contrast_frame.Availability_Base,
                                                  contrast_frame.Travel_Base,
                                                  contrast_frame.Service_Base,
                                                  axis,
                                                  __WIDTH,
                                                  __WIDTH,
                                                  1)

            axis.yaxis_date()
            axis.yaxis.set_major_formatter(matplotlib.dates.DateFormatter("%H:%M:%S"))
            axis.legend(
                (travel_candidate_handle, service_candidate_handle, idle_candidate_handle, overtime_candidate_handle,
                 travel_base_handle, service_base_handle, idle_base_handle, overtime_base_handle),
                ('', '', '', '', 'Service', 'Travel', 'Idle', 'Overtime'), loc='best', ncol=2, columnspacing=0)

            output_file = problem_file_name + '_contrast_activity_' + date.isoformat() + '.' + __FORMAT
            bottom, top = axis.get_ylim()
            axis.set_ylim(bottom, top + 0.025)
        else:
            raise ValueError('Unknown plot type {0}'.format(plot_type))

        matplotlib.pyplot.subplots_adjust(left=0.125)
        matplotlib.pyplot.savefig(output_file, format=__FORMAT, dpi=300)
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


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


# compare formulations on one plot

def format_timedelta(x, pos=None):
    delta = datetime.timedelta(seconds=x)
    time_point = datetime.datetime(2017, 1, 1) + delta
    # if time_point.hour == 0:
    #     return time_point.strftime('%M:%S')
    return time_point.strftime('%H:%M:%S')


def format_time(x, pos=None):
    if isinstance(x, numpy.int64):
        x = x.item()
    delta = datetime.timedelta(seconds=x)
    time_point = datetime.datetime(2017, 1, 1) + delta
    return time_point.strftime('%H:%M')


__SCATTER_POINT_SIZE = 1
__FILE_FORMAT = 'png'
__Y_AXIS_EXTENSION = 1.2


def add_trace_legend(axis, handles, bbox_to_anchor=(0.5, -0.23), ncol=3):
    first_row = handles[0]

    def legend_single_stage(row):
        handle, multi_visits, visits, carers, cost_function, date = row
        date_time = datetime.datetime.combine(date, datetime.time())
        return 'V{0:02}/{1:03} C{2:02} {3} {4}'.format(multi_visits,
                                                       visits,
                                                       carers,
                                                       cost_function,
                                                       date_time.strftime('%d-%m'))

    def legend_multi_stage(row):
        handle, multi_visits, visits, multi_carers, carers, cost_function, date = row
        date_time = datetime.datetime.combine(date, datetime.time())
        return 'V{0:02}/{1:03} C{2:02}/{3:02} {4} {5}' \
            .format(multi_visits, visits, multi_carers, carers, cost_function, date_time.strftime('%d-%m'))

    if len(first_row) == 6:
        legend_formatter = legend_single_stage
    elif len(first_row) == 7:
        legend_formatter = legend_multi_stage
    else:
        raise ValueError('Expecting row of either 6 or 7 elements')

    return add_legend(axis,
                      list(map(operator.itemgetter(0), handles)),
                      list(map(legend_formatter, handles)),
                      ncol,
                      bbox_to_anchor)


def scatter_cost(axis, data_frame, color):
    return axis.scatter(
        [time_delta.total_seconds() for time_delta in data_frame['relative_time']], data_frame['cost'],
        s=__SCATTER_POINT_SIZE,
        c=color)


def scatter_dropped_visits(axis, data_frame, color):
    axis.scatter(
        [time_delta.total_seconds() for time_delta in data_frame['relative_time']],
        data_frame['dropped_visits'],
        s=__SCATTER_POINT_SIZE,
        c=color)


def draw_avline(axis, point, color='lightgrey', linestyle='--'):
    axis.axvline(point, color=color, linestyle=linestyle, linewidth=0.8, alpha=0.8)


def get_problem_stats(problem, date):
    problem_visits = [visit for carer_visits in problem.visits
                      for visit in carer_visits.visits if visit.date == date]
    return len(problem_visits), len([visit for visit in problem_visits if visit.carer_count > 1])


def compare_trace(args, settings):
    problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))

    cost_function = get_or_raise(args, __COST_FUNCTION_TYPE)
    trace_file = get_or_raise(args, __FILE_ARG)
    trace_file_base_name = os.path.basename(trace_file)
    trace_file_stem, trace_file_ext = os.path.splitext(trace_file_base_name)
    output_file_stem = getattr(args, __OUTPUT, trace_file_stem)

    add_arrows = getattr(args, __ARROWS, False)

    trace_logs = read_traces(trace_file)
    data_frame = traces_to_data_frame(trace_logs)

    current_date = getattr(args, __DATE_ARG, None)
    dates = data_frame['date'].unique()
    if current_date and current_date not in dates:
        raise ValueError('Date {0} is not present in the data set'.format(current_date))

    color_numbers = [0, 2, 4, 6, 8, 10, 12, 1, 3, 5, 7, 9, 11, 13]
    color_number_it = iter(color_numbers)
    color_map = matplotlib.cm.get_cmap('tab20')
    matplotlib.pyplot.set_cmap(color_map)
    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    max_relative_time = datetime.timedelta()
    try:
        if current_date:
            total_problem_visits, total_multiple_carer_visits = get_problem_stats(problem, current_date)
            current_date_frame = data_frame[data_frame['date'] == current_date]
            stages = current_date_frame['stage'].unique()
            if len(stages) > 1:
                handles = []
                for stage in stages:
                    time_delta = current_date_frame[current_date_frame['stage'] == stage]['stage_started'].iloc[0]
                    current_stage_data_frame = current_date_frame[current_date_frame['stage'] == stage]
                    draw_avline(ax1, time_delta.total_seconds())
                    draw_avline(ax2, time_delta.total_seconds())
                    total_stage_visits = current_stage_data_frame['visits'].iloc[0]
                    carers = current_stage_data_frame['carers'].iloc[0]
                    handle = scatter_cost(ax1, current_date_frame)
                    scatter_dropped_visits(ax2, current_stage_data_frame)
                    handles.append([handle, total_stage_visits, carers, cost_function])
                add_trace_legend(ax1, handles)

                ax2.set_xlim(left=0)
                ax2.set_ylim(bottom=-10)
                ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            else:
                total_visits = current_date_frame['visits'].iloc[0]
                if total_visits != (total_problem_visits + total_multiple_carer_visits):
                    raise ValueError('Number of visits in problem and solution does not match: {0} vs {1}'
                                     .format(total_visits, (total_problem_visits + total_multiple_carer_visits)))

                carers = current_date_frame['carers'].iloc[0]
                handle = ax1.scatter(
                    [time_delta.total_seconds() for time_delta in current_date_frame['relative_time']],
                    current_date_frame['cost'], s=1)
                add_trace_legend(ax1,
                                 [[handle, total_multiple_carer_visits, total_problem_visits, carers, cost_function]])
                scatter_dropped_visits(ax2, current_date_frame)

            ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
            ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

            # logging.warning('Extending the x axis of the plot by 2 minutes')
            x_left, x_right = ax1.get_xlim()
            updated_x_right = x_right + 2 * 60

            ax1_y_bottom, ax1_y_top = ax1.get_ylim()
            ax1.set_ylim(bottom=0, top=ax1_y_top * __Y_AXIS_EXTENSION)
            ax1.set_ylabel('Cost Function')
            # ax1.set_xlim(left=0, right=updated_x_right)

            ax2_y_bottom, ax2_y_top = ax2.get_ylim()
            ax2.set_ylim(bottom=-10, top=ax2_y_top * __Y_AXIS_EXTENSION)
            # ax2.set_xlim(left=0, right=updated_x_right)
            ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            ax2.set_ylabel('Declined Visits')
            ax2.set_xlabel('Computation Time')
            matplotlib.pyplot.savefig(output_file_stem + '_' + current_date.isoformat() + '.' + __FILE_FORMAT,
                                      format=__FILE_FORMAT,
                                      transparent=True,
                                      dpi=300)
        else:
            handles = []
            for current_date in dates:
                current_color = color_map.colors[next(color_number_it)]

                current_date_frame = data_frame[data_frame['date'] == current_date]
                max_relative_time = max(current_date_frame['relative_time'].max(), max_relative_time)
                total_problem_visits, total_multiple_carer_visits = get_problem_stats(problem, current_date)
                stages = current_date_frame['stage'].unique()
                if len(stages) > 1:
                    stage_linestyles = [None, 'dotted', 'dashed']
                    for stage, linestyle in zip(stages, stage_linestyles):
                        time_delta = current_date_frame[current_date_frame['stage'] == stage]['stage_started'].iloc[0]
                        draw_avline(ax1, time_delta.total_seconds(),
                                    color=current_color,
                                    linestyle=linestyle)
                        draw_avline(ax2, time_delta.total_seconds(),
                                    color=current_color,
                                    linestyle=linestyle)

                    total_carers = current_date_frame['carers'].max()
                    multi_carers = current_date_frame['carers'].min()
                    if multi_carers == total_carers:
                        multi_carers = 0

                    total_visits = current_date_frame['visits'].max()
                    multi_visits = current_date_frame['visits'].min()
                    if multi_visits == total_visits:
                        multi_visits = 0

                    handle = scatter_cost(ax1, current_date_frame, current_color)
                    scatter_dropped_visits(ax2, current_date_frame, current_color)
                    handles.append([handle,
                                    multi_visits,
                                    total_visits,
                                    multi_carers,
                                    total_carers,
                                    cost_function,
                                    current_date])
                else:
                    total_visits = current_date_frame['visits'].iloc[0]
                    if total_visits != (total_problem_visits + total_multiple_carer_visits):
                        raise ValueError('Number of visits in problem and solution does not match: {0} vs {1}'
                                         .format(total_visits, (total_problem_visits + total_multiple_carer_visits)))
                    carers = current_date_frame['carers'].iloc[0]
                    handle = scatter_cost(ax1, current_date_frame, current_color)
                    handles.append(
                        [handle,
                         total_multiple_carer_visits,
                         total_problem_visits,
                         carers,
                         cost_function,
                         current_date])
                    scatter_dropped_visits(ax2, current_date_frame, current_color)

            ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
            ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

            # logging.warning('Extending the x axis of the plot by 2 minutes')
            x_left, x_right = ax1.get_xlim()
            # updated_x_right = x_right + 2 * 60

            if add_arrows:
                ax1.arrow(950, 200000, 40, -110000, head_width=10, head_length=20000, fc='k', ec='k')
                ax2.arrow(950, 60, 40, -40, head_width=10, head_length=10, fc='k', ec='k')

            ax1_y_bottom, ax1_y_top = ax1.get_ylim()
            ax1.set_ylim(bottom=0, top=ax1_y_top * __Y_AXIS_EXTENSION)
            ax1.set_xlim(left=0, right=(max_relative_time + datetime.timedelta(minutes=2)).total_seconds())
            ax1.set_ylabel('Cost Function')
            # legend = add_trace_legend(ax2, handles, bbox_to_anchor=(0.5, -1.7), ncol=2)

            ax2_y_bottom, ax2_y_top = ax2.get_ylim()
            ax2.set_ylim(bottom=-10, top=ax2_y_top * __Y_AXIS_EXTENSION)
            ax2.set_xlim(left=0, right=(max_relative_time + datetime.timedelta(minutes=2)).total_seconds())
            ax2.set_ylabel('Declined Visits')
            ax2.set_xlabel('Computation Time')

            ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            matplotlib.pyplot.tight_layout()
            # figure.subplots_adjust(bottom=0.4)
            matplotlib.pyplot.savefig(output_file_stem + '.' + __FILE_FORMAT,
                                      format=__FILE_FORMAT,
                                      dpi=300,
                                      transparent=True,
                                      # bbox_extra_artists=(legend,),
                                      layout='tight')
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def get_schedule_stats(data_frame):
    def get_stage_stats(stage):
        if stage and (isinstance(stage, str) or (isinstance(stage, float) and not numpy.isnan(stage))):
            stage_frame = data_frame[data_frame['stage'] == stage]
        else:
            stage_frame = data_frame[data_frame['stage'].isnull()]

        min_carers, max_carers = stage_frame['carers'].min(), stage_frame['carers'].max()
        if min_carers != max_carers:
            raise ValueError(
                'Numbers of carer differs within stage in range [{0}, {1}]'.format(min_carers, max_carers))

        min_visits, max_visits = stage_frame['visits'].min(), stage_frame['visits'].max()
        if min_visits != max_visits:
            raise ValueError(
                'Numbers of carer differs within stage in range [{0}, {1}]'.format(min_visits, max_visits))

        return min_carers, min_visits

    stages = data_frame['stage'].unique()
    if len(stages) > 1:
        data = []
        for stage in stages:
            carers, visits = get_stage_stats(stage)
            data.append([stage, carers, visits])
        return data
    else:
        stage_to_use = None
        if len(stages) == 1:
            stage_to_use = stages[0]
        carers, visits = get_stage_stats(stage_to_use)
        return [[None, carers, visits]]


def contrast_trace(args, settings):
    problem_file = get_or_raise(args, __PROBLEM_FILE_ARG)
    problem = load_problem(problem_file)

    problem_file_base = os.path.basename(problem_file)
    problem_file_name, problem_file_ext = os.path.splitext(problem_file_base)

    output_file_stem = getattr(args, __OUTPUT, problem_file_name + '_contrast_traces')

    cost_function = get_or_raise(args, __COST_FUNCTION_TYPE)
    base_trace_file = get_or_raise(args, __BASE_FILE_ARG)
    candidate_trace_file = get_or_raise(args, __CANDIDATE_FILE_ARG)

    base_frame = traces_to_data_frame(read_traces(base_trace_file))
    candidate_frame = traces_to_data_frame(read_traces(candidate_trace_file))

    current_date = get_or_raise(args, __DATE_ARG)
    if current_date not in base_frame['date'].unique():
        raise ValueError('Date {0} is not present in the base data set'.format(current_date))

    if current_date not in candidate_frame['date'].unique():
        raise ValueError('Date {0} is not present in the candidate data set'.format(current_date))

    color_map = matplotlib.cm.get_cmap('Set1')
    matplotlib.pyplot.set_cmap(color_map)
    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    try:
        def plot(data_frame, color):
            stages = data_frame['stage'].unique()
            if len(stages) > 1:
                for stage, linestyle in zip(stages, [None, 'dotted', 'dashed']):
                    time_delta = data_frame[data_frame['stage'] == stage]['stage_started'].iloc[0]
                    draw_avline(ax1, time_delta.total_seconds(), linestyle=linestyle)
                    draw_avline(ax2, time_delta.total_seconds(), linestyle=linestyle)
            scatter_dropped_visits(ax2, data_frame, color=color)
            return scatter_cost(ax1, data_frame, color=color)

        base_current_data_frame = base_frame[base_frame['date'] == current_date]
        base_handle = plot(base_current_data_frame, color_map.colors[0])
        base_stats = get_schedule_stats(base_current_data_frame)
        candidate_current_data_frame = candidate_frame[candidate_frame['date'] == current_date]
        candidate_handle = plot(candidate_current_data_frame, color_map.colors[1])
        candidate_stats = get_schedule_stats(candidate_current_data_frame)

        labels = []
        for stages in [base_stats, candidate_stats]:
            if len(stages) == 1:
                labels.append('Direct')
            elif len(stages) > 1:
                labels.append('Multistage')
            else:
                raise ValueError()

        ax1.set_xlim(left=0.0)
        ax1.set_ylim(bottom=0.0)
        ax1.set_ylabel('Cost Function')
        ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
        ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
        legend1 = ax1.legend([base_handle, candidate_handle], labels)
        for handle in legend1.legendHandles:
            handle._sizes = [25]

        ax2.set_xlim(left=0.0)
        ax2.set_ylim(bottom=0.0)
        ax2.set_ylabel('Declined Visits')
        ax2.set_xlabel('Computation Time')
        ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
        legend2 = ax2.legend([base_handle, candidate_handle], labels)
        for handle in legend2.legendHandles:
            handle._sizes = [25]

        matplotlib.pyplot.tight_layout()
        matplotlib.pyplot.savefig(
            output_file_stem + '_' + current_date.isoformat() + '.' + __FILE_FORMAT,
            format=__FILE_FORMAT,
            dpi=300,
            transparent=True,
            layout='tight')
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)

    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    try:
        candidate_current_data_frame = candidate_frame[candidate_frame['date'] == current_date]
        scatter_dropped_visits(ax2, candidate_current_data_frame, color=color_map.colors[1])
        scatter_cost(ax1, candidate_current_data_frame, color=color_map.colors[1])

        stage2_started = \
            candidate_current_data_frame[candidate_current_data_frame['stage'] == 'Stage2']['stage_started'].iloc[0]

        ax1.set_xlim(left=0.0, right=stage2_started.total_seconds())
        ax1.set_ylim(bottom=0, top=22000)
        ax1.set_ylabel('Cost Function')
        ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
        ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

        matplotlib.pyplot.locator_params(axis='x', nbins=6)
        ax2.set_xlim(left=0.0, right=stage2_started.total_seconds())
        ax2.set_ylim(bottom=-10.0, top=120)
        ax2.set_ylabel('Declined Visits')
        ax2.set_xlabel('Computation Time')
        ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

        matplotlib.pyplot.tight_layout()
        matplotlib.pyplot.savefig(
            output_file_stem + '_first_stage_' + current_date.isoformat() + '.' + __FILE_FORMAT,
            format=__FILE_FORMAT,
            dpi=300,
            transparent=True,
            layout='tight')
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)

    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    try:
        candidate_current_data_frame = candidate_frame[candidate_frame['date'] == current_date]
        scatter_dropped_visits(ax2, candidate_current_data_frame, color=color_map.colors[1])
        scatter_cost(ax1, candidate_current_data_frame, color=color_map.colors[1])

        stage3_started = \
            candidate_current_data_frame[candidate_current_data_frame['stage'] == 'Stage3']['stage_started'].iloc[0]

        ax1.set_xlim(left=860.0, right=stage3_started.total_seconds())
        ax1.set_ylim(bottom=0.0, top=400000)
        ax1.set_ylabel('Cost Function')
        ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
        ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

        ax2.set_xlim(left=860.0, right=stage3_started.total_seconds())
        ax2.set_ylim(bottom=-5.0, top=40)
        ax2.set_ylabel('Declined Visits')
        ax2.set_xlabel('Computation Time')
        ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

        matplotlib.pyplot.tight_layout()
        matplotlib.pyplot.savefig(
            output_file_stem + '_oscillations_' + current_date.isoformat() + '.' + __FILE_FORMAT,
            format=__FILE_FORMAT,
            dpi=300,
            transparent=True,
            layout='tight')
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def compare_prediction_error(args, settings):
    base_schedule = rows.plot.load_schedule(get_or_raise(args, __BASE_FILE_ARG))
    candidate_schedule = rows.plot.load_schedule(get_or_raise(args, __CANDIDATE_FILE_ARG))
    observed_duration_by_visit = calculate_observed_visit_duration(base_schedule)
    expected_duration_by_visit = calculate_expected_visit_duration(candidate_schedule)

    data = []
    for visit in base_schedule.visits:
        observed_duration = observed_duration_by_visit[visit.visit]
        expected_duration = expected_duration_by_visit[visit.visit]
        data.append([visit.key, observed_duration.total_seconds(), expected_duration.total_seconds()])

    frame = pandas.DataFrame(columns=['Visit', 'ObservedDuration', 'ExpectedDuration'], data=data)
    frame['Error'] = (frame.ObservedDuration - frame.ExpectedDuration) / frame.ObservedDuration
    figure, axis = matplotlib.pyplot.subplots()
    try:
        axis.plot(frame['Error'], label='(Observed - Expected)/Observed)')
        axis.legend()
        axis.set_ylim(-20, 2)
        axis.grid()
        matplotlib.pyplot.show()
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def debug(args, settings):
    problem = load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
    solution_file = get_or_raise(args, __SOLUTION_FILE_ARG)
    schedule = rows.plot.load_schedule(solution_file)

    schedule_date = schedule.metadata.begin
    carer_dairies = {
        carer_shift.carer.sap_number:
            next((diary for diary in carer_shift.diaries if diary.date == schedule_date), None)
        for carer_shift in problem.carers}

    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()
    data_set = []
    with create_routing_session() as session:
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


def show_working_hours(args, settings):
    __WIDTH = 0.25
    color_map = matplotlib.cm.get_cmap('tab20')
    matplotlib.pyplot.set_cmap(color_map)

    shift_file = get_or_raise(args, __FILE_ARG)
    shift_file_base_name, shift_file_ext = os.path.splitext(os.path.basename(shift_file))
    output_file_base_name = getattr(args, __OUTPUT, shift_file_base_name)

    __EVENT_TYPE_OFFSET = {'assumed': 2, 'contract': 1, 'work': 0}
    __EVENT_TYPE_COLOR = {'assumed': color_map.colors[0], 'contract': color_map.colors[4], 'work': color_map.colors[2]}

    handles = {}
    frame = pandas.read_csv(shift_file)
    dates = frame['day'].unique()
    for current_date in dates:
        frame_to_use = frame[frame['day'] == current_date]
        carers = frame_to_use['carer'].unique()
        figure, axis = matplotlib.pyplot.subplots()
        try:
            current_date_to_use = datetime.datetime.strptime(current_date, '%Y-%m-%d')
            carer_index = 0
            for carer in carers:
                carer_frame = frame_to_use[frame_to_use['carer'] == carer]
                axis.bar(carer_index + 0.25, 24 * 3600, 0.75, bottom=0, color='grey', alpha=0.3)
                for index, row in carer_frame.iterrows():
                    event_begin = datetime.datetime.strptime(row['begin'], '%Y-%m-%d %H:%M:%S')
                    event_end = datetime.datetime.strptime(row['end'], '%Y-%m-%d %H:%M:%S')
                    handle = axis.bar(carer_index + __EVENT_TYPE_OFFSET[row['event type']] * __WIDTH,
                                      (event_end - event_begin).total_seconds(),
                                      __WIDTH,
                                      bottom=(event_begin - current_date_to_use).total_seconds(),
                                      color=__EVENT_TYPE_COLOR[row['event type']])
                    handles[row['event type']] = handle
                carer_index += 1
            axis.legend([handles['work'], handles['contract'], handles['assumed']],
                        ['Worked', 'Available', 'Forecast'], loc='upper right')
            axis.grid(linestyle='dashed')
            axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_time))
            axis.yaxis.set_ticks(numpy.arange(0, 24 * 3600, 2 * 3600))
            axis.set_ylim(6 * 3600, 24 * 60 * 60)
            matplotlib.pyplot.savefig(output_file_base_name + '_' + current_date + '.' + __FILE_FORMAT,
                                      dpi=1200,
                                      format=__FILE_FORMAT,
                                      layout='tight')
        finally:
            matplotlib.pyplot.cla()
            matplotlib.pyplot.close(figure)


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
    elif __command == __CONTRAST_WORKLOAD_COMMAND:
        contrast_workload(__args, __settings)
    elif __command == __COMPARE_TRACE_COMMAND:
        compare_trace(__args, __settings)
    elif __command == __CONTRAST_TRACE_COMMAND:
        contrast_trace(__args, __settings)
    elif __command == __COMPARE_PREDICTION_ERROR_COMMAND:
        compare_prediction_error(__args, __settings)
    elif __command == __SHOW_WORKING_HOURS_COMMAND:
        show_working_hours(__args, __settings)
    elif __command == __DEBUG_COMMAND:
        debug(__args, __settings)
    else:
        raise ValueError('Unknown command: ' + __command)
