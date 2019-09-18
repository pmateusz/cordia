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
import typing
import warnings

import matplotlib
import matplotlib.cm
import matplotlib.dates
import matplotlib.pyplot
import matplotlib.ticker
import numpy
import pandas
import tabulate

import rows.console
import rows.load
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
import rows.plot
import rows.routing_server
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
__SHOW_WORKING_HOURS_COMMAND = 'show-working-hours'
__COMPARE_DISTANCE_COMMAND = 'compare-distance'
__COMPARE_WORKLOAD_COMMAND = 'compare-workload'
__COMPARE_QUALITY_COMMAND = 'compare-quality'
__COMPARE_COST_COMMAND = 'compare-cost'
__CONTRAST_WORKLOAD_COMMAND = 'contrast-workload'
__COMPARE_PREDICTION_ERROR_COMMAND = 'compare-prediction-error'
__COMPARE_BENCHMARK_COMMAND = 'compare-benchmark'
__COMPARE_BENCHMARK_TABLE_COMMAND = 'compare-benchmark-table'
__COMPARE_QUALITY_OPTIMIZER_COMMAND = 'compare-quality-optimizer'
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
__FILE_FORMAT_ARG = 'output_format'

__color_map = matplotlib.pyplot.get_cmap('tab20c')
FOREGROUND_COLOR = __color_map.colors[0]
FOREGROUND_COLOR2 = 'black'


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
    compare_distance_parser.add_argument(__OPTIONAL_ARG_PREFIX + __FILE_FORMAT_ARG, default=rows.plot.FILE_FORMAT)

    compare_workload_parser = subparsers.add_parser(__COMPARE_WORKLOAD_COMMAND)
    compare_workload_parser.add_argument(__PROBLEM_FILE_ARG)
    compare_workload_parser.add_argument(__BASE_SCHEDULE_PATTERN)
    compare_workload_parser.add_argument(__CANDIDATE_SCHEDULE_PATTERN)
    compare_workload_parser.add_argument(__OPTIONAL_ARG_PREFIX + __FILE_FORMAT_ARG, default=rows.plot.FILE_FORMAT)

    debug_parser = subparsers.add_parser(__DEBUG_COMMAND)
    # debug_parser.add_argument(__PROBLEM_FILE_ARG)
    # debug_parser.add_argument(__SOLUTION_FILE_ARG)

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

    compare_quality_parser = subparsers.add_parser(__COMPARE_QUALITY_COMMAND)

    compare_quality_optimizer_parser = subparsers.add_parser(__COMPARE_QUALITY_OPTIMIZER_COMMAND)
    compare_quality_optimizer_parser.add_argument(__FILE_ARG)

    subparsers.add_parser(__COMPARE_COST_COMMAND)

    compare_benchmark_parser = subparsers.add_parser(__COMPARE_BENCHMARK_COMMAND)
    compare_benchmark_parser.add_argument(__FILE_ARG)

    subparsers.add_parser(__COMPARE_BENCHMARK_TABLE_COMMAND)

    return parser


def split_delta(delta: datetime.timedelta) -> typing.Tuple[int, int, int, int]:
    days = int(delta.days)
    hours = int((delta.total_seconds() - 24 * 3600 * days) // 3600)
    minutes = int((delta.total_seconds() - 24 * 3600 * days - 3600 * hours) // 60)
    seconds = int(delta.total_seconds() - 24 * 3600 * days - 3600 * hours - 60 * minutes)

    assert hours < 24
    assert minutes < 60
    assert seconds < 60

    return days, hours, minutes, seconds


def get_time_delta_label(total_travel_time: datetime.timedelta) -> str:
    days, hours, minutes, seconds = split_delta(total_travel_time)

    time = '{0:02d}:{1:02d}:{2:02d}'.format(hours, minutes, seconds)
    if days == 0:
        return time
    elif days == 1:
        return '1 day ' + time
    else:
        return '{0} days '.format(days) + time


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
    schedule = rows.load.load_schedule(schedule_file_to_use)
    carers = {visit.carer for visit in schedule.visits}
    print(get_travel_time(schedule, user_tag_finder), len(carers), len(schedule.visits))


def create_routing_session():
    return rows.routing_server.RoutingServer(server_executable=find_file_or_fail(
        ['~/dev/cordia/build/rows-routing-server', './build/rows-routing-server']),
        maps_file=find_file_or_fail(
            ['~/dev/cordia/data/scotland-latest.osrm', './data/scotland-latest.osrm']))


def compare_distance(args, settings):
    schedule_patterns = getattr(args, __SCHEDULE_PATTERNS)
    labels = getattr(args, __LABELS)
    output_file = getattr(args, __OUTPUT, 'distance')
    output_file_format = getattr(args, __FILE_FORMAT_ARG)

    data_frame_file = 'data_frame_cache.bin'

    if os.path.isfile(data_frame_file):
        data_frame = pandas.read_pickle(data_frame_file)
    else:
        problem = rows.load.load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))

        store = []
        with create_routing_session() as routing_session:
            distance_estimator = DistanceEstimator(settings, routing_session)
            for label, schedule_pattern in zip(labels, schedule_patterns):
                for schedule_path in glob.glob(schedule_pattern):
                    schedule = rows.load.load_schedule(schedule_path)
                    duration_estimator = DurationEstimator.create_expected_visit_duration(schedule)
                    frame = rows.plot.get_schedule_data_frame(schedule, problem, duration_estimator, distance_estimator)
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
    condensed_frame = pandas.pivot(data_frame, columns='Label', values='Travel', index='Date')
    condensed_frame['Improvement'] = condensed_frame['2nd Stage'] - condensed_frame['3rd Stage']
    condensed_frame['RelativeImprovement'] = condensed_frame['Improvement'] / condensed_frame['2nd Stage']

    figure, ax = matplotlib.pyplot.subplots(1, 1, sharex=True)
    try:
        width = 0.20
        dates = data_frame['Date'].unique()
        time_delta_convert = rows.plot.TimeDeltaConverter()
        indices = numpy.arange(1, len(dates) + 1, 1)

        handles = []
        position = 0
        for label in labels:
            data_frame_to_use = data_frame[data_frame['Label'] == label]

            handle = ax.bar(indices + position * width,
                            time_delta_convert(data_frame_to_use['Travel']),
                            width,
                            bottom=time_delta_convert.zero)

            handles.append(handle)
            position += 1

        ax.yaxis_date()

        yaxis_converter = rows.plot.CumulativeHourMinuteConverter()
        ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(yaxis_converter))
        ax.set_ylabel('Total Travel Time [hh:mm:ss]')
        ax.set_yticks([time_delta_convert.zero + datetime.timedelta(seconds=seconds) for seconds in range(0, 30 * 3600, 4 * 3600 + 1)])
        ax.set_xlabel('Day of October 2017')

        translate_labels = {
            '3rd Stage': '3rd Stage',
            'Human Planners': 'Human Planners'
        }
        labels_to_use = [translate_labels[label] if label in translate_labels else label for label in labels]

        rows.plot.add_legend(ax, handles, labels_to_use, ncol=3, loc='lower center', bbox_to_anchor=(0.5, -0.25))  # , bbox_to_anchor=(0.5, -1.1)
        figure.tight_layout()
        figure.subplots_adjust(bottom=0.20)

        rows.plot.save_figure(output_file, output_file_format)
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)

    # figure, (ax1, ax2, ax3) = matplotlib.pyplot.subplots(3, 1, sharex=True)
    # try:
    #     width = 0.20
    #     dates = data_frame['Date'].unique()
    #     time_delta_convert = rows.plot.TimeDeltaConverter()
    #     indices = numpy.arange(1, len(dates) + 1, 1)
    #
    #     handles = []
    #     position = 0
    #     for label in labels:
    #         data_frame_to_use = data_frame[data_frame['Label'] == label]
    #
    #         handle = ax1.bar(indices + position * width,
    #                          time_delta_convert(data_frame_to_use['Travel']),
    #                          width,
    #                          bottom=time_delta_convert.zero)
    #
    #         ax2.bar(indices + position * width,
    #                 time_delta_convert(data_frame_to_use['Idle']),
    #                 width,
    #                 bottom=time_delta_convert.zero)
    #
    #         ax3.bar(indices + position * width,
    #                 time_delta_convert(data_frame_to_use['Overtime']),
    #                 width,
    #                 bottom=time_delta_convert.zero)
    #
    #         handles.append(handle)
    #         position += 1
    #
    #     ax1.yaxis_date()
    #     ax1.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(rows.plot.CumulativeHourMinuteConverter()))
    #     ax1.set_ylabel('Travel Time')
    #
    #     ax2.yaxis_date()
    #     ax2.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(rows.plot.CumulativeHourMinuteConverter()))
    #     ax2.set_ylabel('Idle Time')
    #
    #     ax3.yaxis_date()
    #     ax3.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(rows.plot.CumulativeHourMinuteConverter()))
    #     ax3.set_ylabel('Total Overtime')
    #     ax3.set_xlabel('Day of October 2017')
    #
    #     translate_labels = {
    #         '3rd Stage': 'Optimizer',
    #         'Human Planners': 'Human Planners'
    #     }
    #     labels_to_use = [translate_labels[label] if label in translate_labels else label for label in labels]
    #
    #     rows.plot.add_legend(ax3, handles, labels_to_use, ncol=3, loc='lower center', bbox_to_anchor=(0.5, -1.1))
    #     figure.tight_layout()
    #     figure.subplots_adjust(bottom=0.20)
    #
    #     rows.plot.save_figure(output_file, output_file_format)
    # finally:
    #     matplotlib.pyplot.cla()
    #     matplotlib.pyplot.close(figure)


def calculate_forecast_visit_duration(problem):
    forecast_visit_duration = rows.plot.VisitDict()
    for recurring_visits in problem.visits:
        for local_visit in recurring_visits.visits:
            forecast_visit_duration[local_visit] = local_visit.duration
    return forecast_visit_duration


def compare_workload(args, settings):
    problem = rows.load.load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
    diary_by_date_by_carer = collections.defaultdict(dict)
    for carer_shift in problem.carers:
        for diary in carer_shift.diaries:
            diary_by_date_by_carer[diary.date][carer_shift.carer.sap_number] = diary
    base_schedules = {rows.load.load_schedule(file_path): file_path
                      for file_path in glob.glob(getattr(args, __BASE_SCHEDULE_PATTERN))}
    base_schedule_by_date = {schedule.metadata.begin: schedule for schedule in base_schedules}
    candidate_schedules = {rows.load.load_schedule(file_path): file_path
                           for file_path in glob.glob(getattr(args, __CANDIDATE_SCHEDULE_PATTERN))}
    candidate_schedule_by_date = {schedule.metadata.begin: schedule for schedule in candidate_schedules}
    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()

    output_file_format = getattr(args, __FILE_FORMAT_ARG)

    dates = set(candidate_schedule_by_date.keys())
    for date in base_schedule_by_date.keys():
        dates.add(date)
    dates = list(dates)
    dates.sort()

    with create_routing_session() as routing_session:

        distance_estimator = DistanceEstimator(settings, routing_session)
        for date in dates:
            base_schedule = base_schedule_by_date.get(date, None)
            if not base_schedule:
                logging.error('No base schedule is available for %s', date)
                continue

            duration_estimator = DurationEstimator.create_expected_visit_duration(base_schedule)

            candidate_schedule = candidate_schedule_by_date.get(date, None)
            if not candidate_schedule:
                logging.error('No candidate schedule is available for %s', date)
                continue

            base_schedule_file = base_schedules[base_schedule]
            base_schedule_data_frame = rows.plot.get_schedule_data_frame(base_schedule, problem, duration_estimator, distance_estimator)

            base_schedule_stem, base_schedule_ext = os.path.splitext(os.path.basename(base_schedule_file))
            rows.plot.save_workforce_histogram(base_schedule_data_frame, base_schedule_stem, output_file_format)

            candidate_schedule_file = candidate_schedules[candidate_schedule]
            candidate_schedule_data_frame = rows.plot.get_schedule_data_frame(candidate_schedule, problem, duration_estimator, distance_estimator)
            candidate_schedule_stem, candidate_schedule_ext \
                = os.path.splitext(os.path.basename(candidate_schedule_file))
            rows.plot.save_workforce_histogram(candidate_schedule_data_frame,
                                               candidate_schedule_stem,
                                               output_file_format)
            rows.plot.save_combined_histogram(candidate_schedule_data_frame,
                                              base_schedule_data_frame,
                                              ['2nd Stage', '3rd Stage'],
                                              'contrast_workforce_{0}_combined'.format(date),
                                              output_file_format)


def contrast_workload(args, settings):
    __WIDTH = 0.35
    __FORMAT = 'svg'

    plot_type = getattr(args, __TYPE_ARG, None)
    if plot_type != __ACTIVITY_TYPE and plot_type != __VISITS_TYPE:
        raise ValueError(
            'Unknown plot type: {0}. Use either {1} or {2}.'.format(plot_type, __ACTIVITY_TYPE, __VISITS_TYPE))

    problem_file = get_or_raise(args, __PROBLEM_FILE_ARG)
    problem = rows.load.load_problem(problem_file)
    base_schedule = rows.load.load_schedule(get_or_raise(args, __BASE_FILE_ARG))
    candidate_schedule = rows.load.load_schedule(get_or_raise(args, __CANDIDATE_FILE_ARG))

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
                time_delta_converter = rows.plot.TimeDeltaConverter()
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
    __PENALTY_PATTERN = re.compile('^MissedVisitPenalty:\s+(?P<penalty>\d+)$')
    __CARER_USED_PATTERN = re.compile('^CarerUsedPenalty:\s+(?P<penalty>\d+)$')

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
            self.__missed_visit_penalty = kwargs.get('missed_visit_penalty', None)
            self.__carer_used_penalty = kwargs.get('carer_used_penalty', None)

        @property
        def date(self):
            return self.__date

        @property
        def carers(self):
            return self.__carers

        @property
        def visits(self):
            return self.__visits

        @property
        def visit_time_window(self):
            return self.__visit_time_windows

        @property
        def carer_used_penalty(self):
            return self.__carer_used_penalty

        @carer_used_penalty.setter
        def carer_used_penalty(self, value):
            self.__carer_used_penalty = value

        @property
        def missed_visit_penalty(self):
            return self.__missed_visit_penalty

        @missed_visit_penalty.setter
        def missed_visit_penalty(self, value):
            self.__missed_visit_penalty = value

        @property
        def shift_adjustment(self):
            return self.__shift_adjustment

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
            elif body['type'] == 'unknown':
                if 'comment' in body:
                    if 'MissedVisitPenalty' in body['comment']:
                        match = re.match(self.__PENALTY_PATTERN, body['comment'])
                        assert match is not None
                        missed_visit_penalty = int(match.group('penalty'))
                        self.__problem.missed_visit_penalty = missed_visit_penalty
                    elif 'CarerUsedPenalty' in body['comment']:
                        match = re.match(self.__CARER_USED_PATTERN, body['comment'])
                        assert match is not None
                        carer_used_penalty = int(match.group('penalty'))
                        self.__problem.carer_used_penalty = carer_used_penalty
            body_to_use = body
        elif 'area' in body:
            body_to_use = TraceLog.ProblemMessage(**body)
            if body_to_use.missed_visit_penalty is None and self.__problem.missed_visit_penalty is not None:
                body_to_use.missed_visit_penalty = self.__problem.missed_visit_penalty
            if body_to_use.carer_used_penalty is None and self.__problem.carer_used_penalty is not None:
                body_to_use.carer_used_penalty = self.__problem.carer_used_penalty
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

    def best_cost(self):
        best_cost, _ = self.__best_cost_and_time()
        return best_cost

    def best_cost_time(self):
        _, best_cost_time = self.__best_cost_and_time()
        return best_cost_time

    def computation_time(self):
        computation_time = datetime.timedelta.max
        for relative_time, stage, absolute_time, event in self.__events:
            computation_time = relative_time
        return computation_time

    def __best_cost_and_time(self):
        best_cost = float('inf')
        best_time = datetime.timedelta.max

        for relative_time, stage, absolute_time, event in self.__events:
            if stage != 2 and stage != 3:
                continue
            if not isinstance(event, TraceLog.ProgressMessage):
                continue
            if best_cost > event.cost:
                best_cost = event.cost
                best_time = relative_time

        return best_cost, best_time

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
    def visit_time_window(self):
        return self.__problem.visit_time_window

    @property
    def carer_used_penalty(self):
        return self.__problem.carer_used_penalty

    @property
    def missed_visit_penalty(self):
        return self.__problem.missed_visit_penalty

    @property
    def shift_adjustment(self):
        return self.__problem.shift_adjustment

    @property
    def events(self):
        return self.__events


def read_traces(trace_file):
    log_line_pattern = re.compile('^\w+\s+(?P<time>\d+:\d+:\d+\.\d+).*?]\s+(?P<body>.*)$')
    other_line_pattern = re.compile('^.*?\[\w+\s+(?P<time>\d+:\d+:\d+\.\d+).*?\]\s+(?P<body>.*)$')

    trace_logs = []
    has_preambule = False
    with open(trace_file, 'r') as input_stream:
        current_log = None
        for line in input_stream:
            match = log_line_pattern.match(line)
            if not match:
                match = other_line_pattern.match(line)

            if match:
                raw_time = match.group('time')
                time = datetime.datetime.strptime(raw_time, '%H:%M:%S.%f')
                try:
                    raw_body = match.group('body')
                    body = json.loads(raw_body)
                    if 'comment' in body and (body['comment'] == 'All'
                                              or 'MissedVisitPenalty' in body['comment']
                                              or 'CarerUsedPenalty' in body['comment']):
                        if body['comment'] == 'All':
                            if 'type' in body:
                                if body['type'] == 'finished':
                                    has_preambule = False
                                elif body['type'] == 'started':
                                    has_preambule = True
                                    current_log = TraceLog(time)
                                    current_log.append(time, body)
                                    trace_logs.append(current_log)
                        else:
                            current_log.append(time, body)
                    elif 'area' in body and not has_preambule:
                        current_log = TraceLog(time)
                        current_log.append(time, body)
                        trace_logs.append(current_log)
                    else:
                        current_log.append(time, body)
                except json.decoder.JSONDecodeError:
                    warnings.warn('Failed to parse line: ' + line)
            elif 'GUIDED_LOCAL_SEARCH specified without sane timeout: solve may run forever.' in line:
                pass
            else:
                warnings.warn('Failed to match line: ' + line)
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
                    if 'comment' in event and event['type'] == 'unknown':
                        continue

                    if event['type'] == 'finished':
                        current_carers = None
                        current_visits = None
                        current_stage_started = None
                        current_stage_name = None
                        continue

                    if event['type'] == 'started':
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


def parse_pandas_duration(value):
    raw_hours, raw_minutes, raw_seconds = value.split(':')
    return datetime.timedelta(hours=int(raw_hours), minutes=int(raw_minutes), seconds=int(raw_seconds))


def format_timedelta(x, pos=None):
    if x < 0:
        return None

    x_to_use = x
    if isinstance(x, numpy.int64):
        x_to_use = x.item()

    delta = datetime.timedelta(seconds=x_to_use)
    time_point = datetime.datetime(2017, 1, 1) + delta
    # if time_point.hour == 0:
    #     return time_point.strftime('%M:%S')
    return time_point.strftime('%M:%S')


def format_timedelta_pandas(x, pos=None):
    if x < 0:
        return None

    time_delta = pandas.to_timedelta(x)
    hours = int(time_delta.total_seconds() / matplotlib.dates.SEC_PER_HOUR)
    minutes = int(time_delta.total_seconds() / matplotlib.dates.SEC_PER_MIN) - 60 * hours
    return '{0:02d}:{1:02d}'.format(hours, minutes)


def format_time(x, pos=None):
    if isinstance(x, numpy.int64):
        x = x.item()
    delta = datetime.timedelta(seconds=x)
    time_point = datetime.datetime(2017, 1, 1) + delta
    return time_point.strftime('%H:%M')


__SCATTER_POINT_SIZE = 1

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

    return rows.plot.add_legend(axis,
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
    problem = rows.load.load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))

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
            ax1.set_ylabel('Cost Function [s]')
            # ax1.set_xlim(left=0, right=updated_x_right)

            ax2_y_bottom, ax2_y_top = ax2.get_ylim()
            ax2.set_ylim(bottom=-10, top=ax2_y_top * __Y_AXIS_EXTENSION)
            # ax2.set_xlim(left=0, right=updated_x_right)
            ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            ax2.set_ylabel('Declined Visits')
            ax2.set_xlabel('Computation Time [mm:ss]')
            rows.plot.save_figure(output_file_stem + '_' + current_date.isoformat())
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

            x_ticks = numpy.arange(0, max_relative_time.total_seconds() + 2 * 60, 1 * 60)

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
            ax1.set_ylabel('Cost Function [s]')
            # legend = add_trace_legend(ax2, handles, bbox_to_anchor=(0.5, -1.7), ncol=2)

            ax2_y_bottom, ax2_y_top = ax2.get_ylim()
            ax2.set_ylim(bottom=-10, top=ax2_y_top * __Y_AXIS_EXTENSION)
            ax2.set_xlim(left=0, right=(max_relative_time + datetime.timedelta(minutes=2)).total_seconds())
            ax2.set_ylabel('Declined Visits')
            ax2.set_xlabel('Computation Time [mm:ss]')

            ax2.set_xticks(x_ticks)
            ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
            matplotlib.pyplot.tight_layout()
            # figure.subplots_adjust(bottom=0.4)
            rows.plot.save_figure(output_file_stem)
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
    problem = rows.load.load_problem(problem_file)

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

    x_ticks_positions = range(0, 8 * 60 + 1, 120)

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
        ax1.set_ylabel('Cost Function [s]')
        ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
        ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
        legend1 = ax1.legend([base_handle, candidate_handle], labels)
        for handle in legend1.legendHandles:
            handle._sizes = [25]

        ax2.set_xlim(left=0.0)
        ax2.set_ylim(bottom=0.0)
        ax2.set_ylabel('Declined Visits')
        ax2.set_xlabel('Computation Time [mm:ss]')
        ax2.set_xticks(x_ticks_positions)
        ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
        legend2 = ax2.legend([base_handle, candidate_handle], labels)
        for handle in legend2.legendHandles:
            handle._sizes = [25]
        figure.tight_layout()

        matplotlib.pyplot.tight_layout()
        rows.plot.save_figure(output_file_stem + '_' + current_date.isoformat())
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

        ax1.set_xlim(left=0.0, right=14)
        ax1.set_ylim(bottom=0, top=6 * 10 ** 4)
        ax1.set_ylabel('Cost Function [s]')
        ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
        ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

        x_ticks_positions = range(0, 12 + 1, 2)
        # matplotlib.pyplot.locator_params(axis='x', nbins=6)
        ax2.set_xlim(left=0.0, right=14)
        ax2.set_ylim(bottom=-10.0, top=120)
        ax2.set_ylabel('Declined Visits')
        ax2.set_xlabel('Computation Time [mm:ss]')
        ax2.set_xticks(x_ticks_positions)
        ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))

        matplotlib.pyplot.tight_layout()
        rows.plot.save_figure(output_file_stem + '_first_stage_' + current_date.isoformat())
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)

    # figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    # try:
    #     candidate_current_data_frame = candidate_frame[candidate_frame['date'] == current_date]
    #     scatter_dropped_visits(ax2, candidate_current_data_frame, color=color_map.colors[1])
    #     scatter_cost(ax1, candidate_current_data_frame, color=color_map.colors[1])
    #
    #     stage3_started = \
    #         candidate_current_data_frame[candidate_current_data_frame['stage'] == 'Stage3']['stage_started'].iloc[0]
    #
    #     oscillation_started = datetime.timedelta(seconds=360)
    #     ax1.set_xlim(left=oscillation_started.total_seconds(), right=stage3_started.total_seconds())
    #     # ax1.set_ylim(bottom=0.0, top=400000)
    #     ax1.set_ylabel('Cost Function [s]')
    #     ax1.ticklabel_format(style='sci', axis='y', scilimits=(-2, 2))
    #     ax1.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
    #
    #     ax2.set_xlim(left=oscillation_started.total_seconds(), right=stage3_started.total_seconds())
    #     ax2.set_ylim(bottom=-5.0, top=40)
    #     ax2.set_ylabel('Declined Visits')
    #     ax2.set_xlabel('Computation Time [mm:ss]')
    #     ax2.set_xticks(x_ticks_positions)
    #     ax2.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta))
    #
    #     matplotlib.pyplot.tight_layout()
    #     rows.plot.save_figure(output_file_stem + '_oscillations_' + current_date.isoformat())
    # finally:
    #     matplotlib.pyplot.cla()
    #     matplotlib.pyplot.close(figure)


def compare_prediction_error(args, settings):
    base_schedule = rows.plot.load_schedule(get_or_raise(args, __BASE_FILE_ARG))
    candidate_schedule = rows.plot.load_schedule(get_or_raise(args, __CANDIDATE_FILE_ARG))
    observed_duration_by_visit = rows.plot.calculate_observed_visit_duration(base_schedule)
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


class DistanceEstimator:

    def __init__(self, settings, routing_session):
        self.__routing_session = routing_session
        self.__location_finder = rows.location_finder.UserLocationFinder(settings)
        self.__location_finder.reload()

    def __call__(self, source_visit, destination_visit):
        source_loc = self.__location_finder.find(source_visit.visit.service_user)
        if not source_loc:
            logging.error('Failed to resolve location of %s', source_visit.visit.service_user)
            return datetime.timedelta()
        destination_loc = self.__location_finder.find(destination_visit.visit.service_user)
        if not destination_loc:
            logging.error('Failed to resolve location of %s', destination_visit.visit.service_user)
            return datetime.timedelta()
        distance = self.__routing_session.distance(source_loc, destination_loc)
        if distance is None:
            logging.error('Distance cannot be estimated between %s and %s', source_loc, destination_loc)
            return datetime.timedelta()
        return datetime.timedelta(seconds=distance)


class DurationEstimator:

    def __init__(self, index):
        self.__index = index

    def __call__(self, visit) -> typing.Optional[datetime.timedelta]:
        try:
            return self.__index[visit]
        except KeyError:
            return None

    @staticmethod
    def create_expected_visit_duration(schedule):
        expected_visit_duration = rows.plot.VisitDict()
        for past_visit in schedule.visits:
            expected_visit_duration[past_visit.visit] = past_visit.duration
        return DurationEstimator(expected_visit_duration)


def remove_violated_visits(rough_schedule: rows.model.schedule.Schedule,
                           metadata: TraceLog,
                           problem: rows.model.problem.Problem,
                           duration_estimator: DurationEstimator,
                           distance_estimator: DistanceEstimator) -> rows.model.schedule.Schedule:
    max_delay = metadata.visit_time_window
    min_delay = -metadata.visit_time_window

    dropped_visits = 0
    allowed_visits = []
    for route in rough_schedule.routes():
        carer_diary = problem.get_diary(route.carer, metadata.date)
        if not carer_diary:
            continue

        for visit in route.visits:
            if visit.check_in is not None:
                check_in_delay = visit.check_in - datetime.datetime.combine(metadata.date, visit.time)
                if check_in_delay > max_delay:  # or check_in_delay < min_delay:
                    dropped_visits += 1
                    continue
            allowed_visits.append(visit)

    # schedule does not have visits which exceed time windows
    first_improved_schedule = rows.model.schedule.Schedule(carers=rough_schedule.carers(), visits=allowed_visits)

    allowed_visits = []
    for route in first_improved_schedule.routes():
        edges = route.edges()

        if not edges:
            continue

        diary = problem.get_diary(route.carer, metadata.date)
        assert diary is not None

        # shift adjustment is added twice because it is allowed to extend the time before and after the working hours
        max_shift_end = max(event.end for event in diary.events) + metadata.shift_adjustment + metadata.shift_adjustment

        first_visit = edges[0][0]
        current_time = datetime.datetime.combine(metadata.date, first_visit.time)
        if current_time <= max_shift_end:
            allowed_visits.append(first_visit)

        for prev_visit, next_visit in edges:
            current_time += duration_estimator(prev_visit.visit)
            current_time += distance_estimator(prev_visit, next_visit)
            current_time = max(current_time, datetime.datetime.combine(metadata.date, next_visit.time) + min_delay)
            if current_time <= max_shift_end:
                allowed_visits.append(next_visit)
            else:
                dropped_visits += 1

    # schedule does not contain visits which exceed overtime of the carer
    second_improved_schedule = rows.model.schedule.Schedule(carers=rough_schedule.carers(), visits=allowed_visits)
    return second_improved_schedule


class ScheduleCost:
    CARER_COST = datetime.timedelta(seconds=60 * 60 * 4)

    def __init__(self, travel_time: datetime.timedelta, carers_used: int, visits_missed: int, missed_visit_penalty: int):
        self.__travel_time = travel_time
        self.__carers_used = carers_used
        self.__visits_missed = visits_missed
        self.__missed_visit_penalty = missed_visit_penalty

    @property
    def travel_time(self) -> datetime.timedelta:
        return self.__travel_time

    @property
    def visits_missed(self) -> int:
        return self.__visits_missed

    @property
    def missed_visit_penalty(self) -> int:
        return self.__missed_visit_penalty

    @property
    def carers_used(self) -> int:
        return self.__carers_used

    def total_cost(self):
        return self.__travel_time.total_seconds() \
               + self.CARER_COST.total_seconds() * self.__carers_used \
               + self.__missed_visit_penalty * self.__visits_missed


def get_schedule_cost(schedule: rows.model.schedule.Schedule,
                      metadata: TraceLog,
                      problem: rows.model.problem.Problem,
                      distance_estimator: DistanceEstimator) -> ScheduleCost:
    carer_used_ids = set()
    visit_made_ids = set()

    travel_time = datetime.timedelta()

    for route in schedule.routes():
        if not route.visits:
            continue

        carer_used_ids.add(route.carer.sap_number)
        for visit in route.visits:
            visit_made_ids.add(visit.visit.key)

        for source, destination in route.edges():
            travel_time += distance_estimator(source, destination)

    schedule_date = schedule.date()
    available_visit_ids = {visit.key for visit_group in problem.visits for visit in visit_group.visits if visit.date == schedule_date}
    return ScheduleCost(travel_time, len(carer_used_ids), len(available_visit_ids.difference(visit_made_ids)), metadata.missed_visit_penalty)


def compare_schedule_cost(args, settings):
    ProblemConfig = collections.namedtuple('ProblemConfig', ['ProblemPath', 'HumanSolutionPath', 'SolverSolutionPath'])

    simulation_dir = '/home/pmateusz/dev/cordia/simulations/current_review_simulations'
    solver_log_file = os.path.join(simulation_dir, 'cp_schedules/past/c350past_redv90b90e30m1m1m5.err.log')
    problem_data = [ProblemConfig(os.path.join(simulation_dir, 'problems/C350_past.json'),
                                  os.path.join(simulation_dir, 'planner_schedules/C350_planners_201710{0:02d}.json'.format(day)),
                                  os.path.join(simulation_dir, 'cp_schedules/past/c350past_redv90b90e30m1m1m5_201710{0:02d}.gexf'.format(day)))
                    for day in range(1, 15, 1)]

    solver_traces = read_traces(solver_log_file)
    assert len(solver_traces) == len(problem_data)

    results = []
    printable_results = []

    def get_cost_label(cost_components: ScheduleCost) -> int:
        return int(cost_components.total_cost())

    with create_routing_session() as routing_session:
        distance_estimator = DistanceEstimator(settings, routing_session)

        for solver_trace, problem_data in zip(solver_traces, problem_data):
            problem = rows.load.load_problem(os.path.join(simulation_dir, problem_data.ProblemPath))
            human_schedule = rows.load.load_schedule(os.path.join(simulation_dir, problem_data.HumanSolutionPath))
            solver_schedule = rows.load.load_schedule(os.path.join(simulation_dir, problem_data.SolverSolutionPath))

            assert solver_trace.date == human_schedule.date()
            assert solver_trace.date == solver_schedule.date()

            duration_estimator = DurationEstimator.create_expected_visit_duration(solver_schedule)
            human_schedule_to_use = remove_violated_visits(human_schedule, solver_trace, problem, duration_estimator, distance_estimator)
            solver_schedule_to_use = remove_violated_visits(solver_schedule, solver_trace, problem, duration_estimator, distance_estimator)
            human_cost = get_schedule_cost(human_schedule_to_use, solver_trace, problem, distance_estimator)
            solver_cost = get_schedule_cost(solver_schedule_to_use, solver_trace, problem, distance_estimator)
            results.append({'date': solver_trace.date,
                            'missed_visit_penalty': solver_trace.missed_visit_penalty,
                            'carer_used_penalty': solver_trace.carer_used_penalty,
                            'planner_travel_time': human_cost.travel_time,
                            'planner_carers_used': human_cost.carers_used,
                            'planner_missed_visits': human_cost.visits_missed,
                            'planner_total_cost': human_cost.total_cost(),
                            'solver_carers_used': solver_cost.carers_used,
                            'solver_travel_time': solver_cost.travel_time,
                            'solver_missed_visits': solver_cost.visits_missed,
                            'solver_total_cost': solver_cost.total_cost()})

            printable_results.append(collections.OrderedDict(day=solver_trace.date.day,
                                                             visit_penalty=solver_trace.missed_visit_penalty,
                                                             carer_penalty=solver_trace.carer_used_penalty,
                                                             planner_travel_time=get_time_delta_label(human_cost.travel_time),
                                                             planner_carers_used=human_cost.carers_used,
                                                             planner_missed_visits=human_cost.visits_missed,
                                                             planner_total_cost=get_cost_label(human_cost),
                                                             solver_travel_time=get_time_delta_label(solver_cost.travel_time),
                                                             solver_carers_used=solver_cost.carers_used,
                                                             solver_missed_visits=solver_cost.visits_missed,
                                                             solver_total_cost=get_cost_label(solver_cost)))

    data_frame = pandas.DataFrame(data=results)
    print(tabulate.tabulate(data_frame, tablefmt='psql', headers='keys'))

    printable_data_frame = pandas.DataFrame(data=printable_results)
    print(tabulate.tabulate(printable_data_frame, tablefmt='latex', headers='keys', showindex=False))


def get_consecutive_visit_time_span(schedule: rows.model.schedule.Schedule, start_time_estimator):
    client_visits = collections.defaultdict(list)
    for visit in schedule.visits:
        client_visits[visit.visit.service_user].append(visit)

    for client in client_visits:
        visits = client_visits[client]

        used_keys = set()
        unique_visits = []
        for visit in visits:
            date_time = start_time_estimator(visit)
            if date_time.hour == 0 and date_time.minute == 0:
                continue

            if visit.visit.key not in used_keys:
                used_keys.add(visit.visit.key)
                unique_visits.append(visit)
        unique_visits.sort(key=start_time_estimator)
        client_visits[client] = unique_visits

    client_span = collections.defaultdict(datetime.timedelta)
    for client in client_visits:
        if len(client_visits[client]) < 2:
            continue

        last_visit = client_visits[client][0]
        total_span = datetime.timedelta()
        for next_visit in client_visits[client][1:]:
            total_span += start_time_estimator(next_visit) - start_time_estimator(last_visit)
            last_visit = next_visit
        client_span[client] = total_span
    return client_span


def get_carer_client_frequency(schedule: rows.model.schedule.Schedule):
    client_assigned_carers = collections.defaultdict(collections.Counter)
    for visit in schedule.visits:
        client_assigned_carers[int(visit.visit.service_user)][int(visit.carer.sap_number)] += 1
    return client_assigned_carers


def get_visits(problem: rows.model.problem.Problem, date: datetime.date):
    visits = set()
    for local_visits in problem.visits:
        for visit in local_visits.visits:
            if date != visit.date:
                continue
            visit.service_user = local_visits.service_user
            visits.add(visit)
    return visits


def get_teams(problem: rows.model.problem.Problem, schedule: rows.model.schedule.Schedule):
    multiple_carer_visit_keys = set()
    for visit in get_visits(problem, schedule.date()):
        if visit.carer_count > 1:
            multiple_carer_visit_keys.add(visit.key)

    client_visit_carers = collections.defaultdict(lambda: collections.defaultdict(list))
    for visit in schedule.visits:
        if visit.visit.key not in multiple_carer_visit_keys:
            continue
        client_visit_carers[visit.visit.service_user][visit.visit.key].append(int(visit.carer.sap_number))
    for client in client_visit_carers:
        for visit_key in client_visit_carers[client]:
            client_visit_carers[client][visit_key].sort()
    teams = set()
    for client in client_visit_carers:
        for visit_key in client_visit_carers[client]:
            teams.add(tuple(client_visit_carers[client][visit_key]))
    return teams


def compare_schedule_quality(args, settings):
    ProblemConfig = collections.namedtuple('ProblemConfig', ['ProblemPath', 'HumanSolutionPath', 'SolverSolutionPath'])

    def compare_quality(solver_trace, problem, human_schedule, solver_schedule, duration_estimator, distance_estimator):
        visits = get_visits(problem, solver_trace.date)
        multiple_carer_visit_keys = {visit.key for visit in visits if visit.carer_count > 1}
        clients = list({int(visit.service_user) for visit in visits})

        # number of different carers assigned throughout the day
        human_carer_frequency = get_carer_client_frequency(human_schedule)
        solver_carer_frequency = get_carer_client_frequency(solver_schedule)

        human_schedule_squared = []
        solver_schedule_squared = []
        for client in clients:
            if client in human_carer_frequency:
                human_schedule_squared.append(sum(human_carer_frequency[client][carer] ** 2 for carer in human_carer_frequency[client]))
            else:
                human_schedule_squared.append(0)

            if client in solver_carer_frequency:
                solver_schedule_squared.append(sum(solver_carer_frequency[client][carer] ** 2 for carer in solver_carer_frequency[client]))
            else:
                solver_schedule_squared.append(0)

        human_matching_dominates = 0
        solver_matching_dominates = 0
        for index in range(len(clients)):
            if human_schedule_squared[index] > solver_schedule_squared[index]:
                human_matching_dominates += 1
            elif human_schedule_squared[index] < solver_schedule_squared[index]:
                solver_matching_dominates += 1
        matching_no_diff = len(clients) - human_matching_dominates - solver_matching_dominates
        assert matching_no_diff >= 0

        human_schedule_span = get_consecutive_visit_time_span(human_schedule, lambda visit: visit.check_in)
        solver_schedule_span = get_consecutive_visit_time_span(solver_schedule, lambda visit: datetime.datetime.combine(visit.date, visit.time))

        human_span_dominates = 0
        solver_span_dominates = 0
        for client in clients:
            if human_schedule_span[client] > solver_schedule_span[client]:
                human_span_dominates += 1
            elif human_schedule_span[client] < solver_schedule_span[client]:
                solver_span_dominates += 1
        span_no_diff = len(clients) - human_span_dominates - solver_span_dominates
        assert span_no_diff > 0

        human_teams = get_teams(problem, human_schedule)
        solver_teams = get_teams(problem, solver_schedule)

        human_schedule_frame = rows.plot.get_schedule_data_frame(human_schedule, problem, duration_estimator, distance_estimator)
        solver_schedule_frame = rows.plot.get_schedule_data_frame(solver_schedule, problem, duration_estimator, distance_estimator)

        human_total_overtime = compute_overtime(human_schedule_frame).sum()
        solver_total_overtime = compute_overtime(solver_schedule_frame).sum()

        return {'problem': str(human_schedule.date()),
                'visits': len(visits),
                'clients': len(clients),
                'human_overtime': human_total_overtime,
                'solver_overtime': solver_total_overtime,
                'human_visit_span_dominates': human_span_dominates,
                'solver_visit_span_dominates': solver_span_dominates,
                'visit_span_indifferent': span_no_diff,
                'human_matching_dominates': human_matching_dominates,
                'solver_matching_dominates': solver_matching_dominates,
                'matching_indifferent': matching_no_diff,
                'human_teams': len(human_teams),
                'solver_teams': len(solver_teams)}

    simulation_dir = '/home/pmateusz/dev/cordia/simulations/current_review_simulations'
    solver_log_file = os.path.join(simulation_dir, 'cp_schedules/past/c350past_redv90b90e30m1m1m5.err.log')
    problem_data = [ProblemConfig(os.path.join(simulation_dir, 'problems/C350_past.json'),
                                  os.path.join(simulation_dir, 'planner_schedules/C350_planners_201710{0:02d}.json'.format(day)),
                                  os.path.join(simulation_dir, 'cp_schedules/past/c350past_redv90b90e30m1m1m5_201710{0:02d}.gexf'.format(day)))
                    for day in range(1, 15, 1)]

    solver_traces = read_traces(solver_log_file)
    assert len(solver_traces) == len(problem_data)

    results = []
    with create_routing_session() as routing_session:
        distance_estimator = DistanceEstimator(settings, routing_session)

        for solver_trace, problem_data in zip(solver_traces, problem_data):
            problem = rows.load.load_problem(os.path.join(simulation_dir, problem_data.ProblemPath))
            human_schedule = rows.load.load_schedule(os.path.join(simulation_dir, problem_data.HumanSolutionPath))
            solver_schedule = rows.load.load_schedule(os.path.join(simulation_dir, problem_data.SolverSolutionPath))

            assert solver_trace.date == human_schedule.date()
            assert solver_trace.date == solver_schedule.date()

            duration_estimator = DurationEstimator.create_expected_visit_duration(solver_schedule)
            human_schedule_to_use = remove_violated_visits(human_schedule, solver_trace, problem, duration_estimator, distance_estimator)
            solver_schedule_to_use = remove_violated_visits(solver_schedule, solver_trace, problem, duration_estimator, distance_estimator)
            row = compare_quality(solver_trace, problem, human_schedule_to_use, solver_schedule_to_use, duration_estimator, distance_estimator)
            results.append(row)

    data_frame = pandas.DataFrame(data=results)
    data_frame['human_visit_span_dominates_rel'] = data_frame['human_visit_span_dominates'] / data_frame['clients']
    data_frame['human_visit_span_dominates_rel_label'] = data_frame['human_visit_span_dominates_rel'].apply(lambda v: '{0:.2f}'.format(v * 100.0))
    data_frame['solver_visit_span_dominates_rel'] = data_frame['solver_visit_span_dominates'] / data_frame['clients']
    data_frame['solver_visit_span_dominates_rel_label'] = data_frame['solver_visit_span_dominates_rel'].apply(lambda v: '{0:.2f}'.format(v * 100.0))
    data_frame['visit_span_indifferent_rel'] = data_frame['visit_span_indifferent'] / data_frame['clients']

    data_frame['human_matching_dominates_rel'] = data_frame['human_matching_dominates'] / data_frame['clients']
    data_frame['human_matching_dominates_rel_label'] = data_frame['human_matching_dominates_rel'].apply(lambda v: '{0:.2f}'.format(v * 100.0))
    data_frame['solver_matching_dominates_rel'] = data_frame['solver_matching_dominates'] / data_frame['clients']
    data_frame['solver_matching_dominates_rel_label'] = data_frame['solver_matching_dominates_rel'].apply(lambda v: '{0:.2f}'.format(v * 100.0))
    data_frame['matching_indifferent_rel'] = data_frame['matching_indifferent'] / data_frame['clients']
    data_frame['day'] = data_frame['problem'].apply(lambda label: datetime.datetime.strptime(label, '%Y-%m-%d').date().day)
    data_frame['human_overtime_label'] = data_frame['human_overtime'].apply(get_time_delta_label)
    data_frame['solver_overtime_label'] = data_frame['solver_overtime'].apply(get_time_delta_label)

    print(tabulate.tabulate(data_frame, tablefmt='psql', headers='keys'))
    print(tabulate.tabulate(data_frame[['day', 'human_overtime_label', 'solver_overtime_label',
                                        'human_visit_span_dominates_rel_label', 'solver_visit_span_dominates_rel_label',
                                        'human_matching_dominates_rel_label', 'solver_matching_dominates_rel_label',
                                        'human_teams', 'solver_teams']], tablefmt='latex', showindex=False, headers='keys'))


BenchmarkData = collections.namedtuple('BenchmarkData', ['BestCost', 'BestCostTime', 'BestBound', 'ComputationTime'])


class MipTrace:
    __MIP_HEADER_PATTERN = re.compile('^\s*Expl\s+Unexpl\s+|\s+Obj\s+Depth\s+IntInf\s+|\s+Incumbent\s+BestBd\s+Gap\s+|\s+It/Node\s+Time\s*$')
    __MIP_LINE_PATTERN = re.compile('^(?P<solution_flag>[\w\*]?)\s*'
                                    '(?P<explored_nodes>\d+)\s+'
                                    '(?P<nodes_to_explore>\d+)\s+'
                                    '(?P<node_relaxation>[\w\.]*)\s+'
                                    '(?P<node_depth>\d*)\s+'
                                    '(?P<fractional_variables>\w*)\s+'
                                    '(?P<incumbent>[\d\.\-]*)\s+'
                                    '(?P<lower_bound>[\d\.\-]*)\s+'
                                    '(?P<gap>[\d\.\%\-]*)\s+'
                                    '(?P<simplex_it_per_node>[\d\.\-]*)\s+'
                                    '(?P<elapsed_time>\d+)s$')
    __SUMMARY_PATTERN = re.compile('^Best\sobjective\s(?P<objective>[e\d\.\+]+),\s'
                                   'best\sbound\s(?P<bound>[e\d\.\+]+),\s'
                                   'gap\s(?P<gap>[e\d\.\+]+)\%$')

    class MipProgressMessage:
        def __init__(self, has_solution, best_cost, lower_bound, elapsed_time):
            self.__has_solution = has_solution
            self.__best_cost = best_cost
            self.__lower_bound = lower_bound
            self.__elapsed_time = elapsed_time

        @property
        def has_solution(self):
            return self.__has_solution

        @property
        def best_cost(self):
            return self.__best_cost

        @property
        def lower_bound(self):
            return self.__lower_bound

        @property
        def elapsed_time(self):
            return self.__elapsed_time

    def __init__(self, best_objective: float, best_bound: float, events: typing.List[MipProgressMessage]):
        self.__best_objective = best_objective
        self.__best_bound = best_bound
        self.__events = events

    @staticmethod
    def read_from_file(path) -> 'MipTrace':
        events = []
        best_objective = float('inf')
        best_bound = float('-inf')
        with open(path, 'r') as fp:
            lines = fp.readlines()
            lines_it = iter(lines)
            for line in lines_it:
                if re.match(MipTrace.__MIP_HEADER_PATTERN, line):
                    break
            next(lines_it, None)  # read the empty line
            for line in lines_it:
                line_match = re.match(MipTrace.__MIP_LINE_PATTERN, line)
                if not line_match:
                    break

                raw_solution_flag = line_match.group('solution_flag')
                raw_incumbent = line_match.group('incumbent')
                raw_lower_bound = line_match.group('lower_bound')
                raw_elapsed_time = line_match.group('elapsed_time')

                has_solution = raw_solution_flag == 'H' or raw_solution_flag == '*'
                incumbent = float(raw_incumbent) if raw_incumbent and raw_incumbent != '-' else float('inf')
                lower_bound = float(raw_lower_bound) if raw_lower_bound else float('-inf')
                elapsed_time = datetime.timedelta(seconds=int(raw_elapsed_time)) if raw_elapsed_time else datetime.timedelta()
                events.append(MipTrace.MipProgressMessage(has_solution, incumbent, lower_bound, elapsed_time))
            next(lines_it, None)
            for line in lines_it:
                line_match = re.match(MipTrace.__SUMMARY_PATTERN, line)
                if line_match:
                    raw_objective = line_match.group('objective')
                    if raw_objective:
                        best_objective = float(raw_objective)
                    raw_bound = line_match.group('bound')
                    if raw_bound:
                        best_bound = float(raw_bound)
        return MipTrace(best_objective, best_bound, events)

    def best_cost(self):
        return self.__best_objective

    def best_cost_time(self):
        for event in reversed(self.__events):
            if event.has_solution:
                return event.elapsed_time
        return datetime.timedelta.max

    def best_bound(self):
        return self.__best_bound

    def computation_time(self):
        if self.__events:
            return self.__events[-1].elapsed_time
        return datetime.timedelta.max


def compare_benchmark_table(args, settings):
    ProblemConfig = collections.namedtuple('ProblemConfig', ['ProblemPath', 'Carers', 'Visits', 'Visits2', 'MipSolutionLog',
                                                             'CpTeamSolutionLog',
                                                             'CpWindowsSolutionLog'])
    simulation_dir = '/home/pmateusz/dev/cordia/simulations/current_review_simulations'
    old_simulation_dir = '/home/pmateusz/dev/cordia/simulations/review_simulations_old'

    problem_configs = [ProblemConfig(os.path.join(simulation_dir, 'benchmark/25/problem_201710{0:02d}_v25m0c3.json'.format(day_number)),
                                     3, 25, 0,
                                     os.path.join(simulation_dir, 'benchmark/25/solutions/problem_201710{0:02d}_v25m0c3_mip.log'.format(day_number)),
                                     os.path.join(simulation_dir, 'benchmark/25/solutions/problem_201710{0:02d}_v25m0c3.err.log'.format(day_number)),
                                     os.path.join(simulation_dir, 'benchmark/25/solutions/problem_201710{0:02d}_v25m0c3.err.log'.format(day_number)))
                       for day_number in range(1, 15, 1)]
    problem_configs.extend(
        [ProblemConfig(os.path.join(simulation_dir, 'benchmark/25/problem_201710{0:02d}_v25m5c3.json'.format(day_number)),
                       3, 20, 5,
                       os.path.join(simulation_dir, 'benchmark/25/solutions/problem_201710{0:02d}_v25m5c3_mip.log'.format(day_number)),
                       os.path.join(simulation_dir, 'benchmark/25/solutions/problem_201710{0:02d}_teams_v25m5c3.err.log'.format(day_number)),
                       os.path.join(simulation_dir, 'benchmark/25/solutions/problem_201710{0:02d}_windows_v25m5c3.err.log'.format(day_number)))
         for day_number in range(1, 15, 1)])
    problem_configs.extend(
        [ProblemConfig(os.path.join(simulation_dir, 'benchmark/50/problem_201710{0:02d}_v50m0c5.json'.format(day_number)),
                       5, 50, 0,
                       os.path.join(simulation_dir, 'benchmark/50/solutions/problem_201710{0:02d}_v50m0c5_mip.log'.format(day_number)),
                       os.path.join(simulation_dir, 'benchmark/50/solutions/problem_201710{0:02d}_v50m0c5.err.log'.format(day_number)),
                       os.path.join(simulation_dir, 'benchmark/50/solutions/problem_201710{0:02d}_v50m0c5.err.log'.format(day_number)))
         for day_number in range(1, 15, 1)])
    problem_configs.extend(
        [ProblemConfig(os.path.join(simulation_dir, 'benchmark/50/problem_201710{0:02d}_v50m10c5.json'.format(day_number)),
                       5, 40, 10,
                       os.path.join(simulation_dir, 'benchmark/50/solutions/problem_201710{0:02d}_v50m10c5_mip.log'.format(day_number)),
                       os.path.join(simulation_dir, 'benchmark/50/solutions/problem_201710{0:02d}_teams_v50m10c5.err.log'.format(day_number)),
                       os.path.join(simulation_dir, 'benchmark/50/solutions/problem_201710{0:02d}_windows_v50m10c5.err.log'.format(day_number)))
         for day_number in range(1, 15, 1)])

    logs = []
    for problem_config in problem_configs:
        if not os.path.exists(problem_config.MipSolutionLog):
            warnings.warn('File {0} is missing'.format(problem_config.MipSolutionLog))
            continue

        if not os.path.exists(problem_config.CpTeamSolutionLog):
            warnings.warn('File {0} is missing'.format(problem_config.CpTeamSolutionLog))
            continue

        if not os.path.exists(problem_config.CpWindowsSolutionLog):
            warnings.warn('File {0} is missing'.format(problem_config.CpWindowsSolutionLog))
            continue

        with warnings.catch_warnings():
            warnings.simplefilter('ignore')

            cp_team_logs = read_traces(problem_config.CpTeamSolutionLog)
            if not cp_team_logs:
                warnings.warn('File {0} is empty'.format(problem_config.CpTeamSolutionLog))
                continue
            cp_team_log = cp_team_logs[0]

            cp_window_logs = read_traces(problem_config.CpWindowsSolutionLog)
            if not cp_window_logs:
                warnings.warn('File {0} is empty'.format(problem_config.CpWindowsSolutionLog))
                continue
            cp_window_log = cp_window_logs[0]

            mip_log = MipTrace.read_from_file(problem_config.MipSolutionLog)
            if not mip_log:
                warnings.warn('File {0} is empty'.format(problem_config.MipSolutionLog))
                continue

            logs.append([problem_config, mip_log, cp_team_log, cp_window_log])

    def get_gap(cost, lower_bound):
        return (cost - lower_bound) * 100.0 / lower_bound

    data = []
    for problem_config, mip_log, cp_team_log, cp_window_log in logs:
        data.append(collections.OrderedDict(
            date=cp_team_log.date,
            visits=problem_config.Visits,
            visits_of_two=problem_config.Visits2,
            carers=cp_team_log.carers,
            penalty=cp_team_log.missed_visit_penalty,
            lower_bound=mip_log.best_bound(),
            mip_best_cost=mip_log.best_cost(),
            mip_best_gap=get_gap(mip_log.best_cost(), mip_log.best_bound()),
            mip_best_time=mip_log.best_cost_time(),
            team_best_cost=cp_team_log.best_cost(),
            team_best_gap=get_gap(cp_team_log.best_cost(), mip_log.best_bound()),
            team_best_time=cp_team_log.best_cost_time(),
            windows_best_cost=cp_window_log.best_cost(),
            windows_best_gap=get_gap(cp_window_log.best_cost(), mip_log.best_bound()),
            windows_best_time=cp_window_log.best_cost_time()))

    data_frame = pandas.DataFrame(data=data)
    print(tabulate.tabulate(data_frame, tablefmt='psql', headers='keys'))

    def get_duration_label(time_delta: datetime.timedelta) -> str:
        assert time_delta.days == 0
        hours = int(time_delta.total_seconds() / 3600)
        minutes = int(time_delta.total_seconds() / 60 - hours * 60)
        seconds = int(time_delta.total_seconds() - 3600 * hours - 60 * minutes)
        return '{0:02d}:{1:02d}:{2:02d}'.format(hours, minutes, seconds)

    def get_cost_label(cost: float, lower_bound: float) -> str:
        return str(int(cost))

    def get_gap_label(gap: float) -> str:
        return '{0:.2f}'.format(gap)

    def get_problem_label(problem, date: datetime.date):
        label = '{0:2d} {1}'.format(date.day, problem.Visits)
        if problem.Visits2 == 0:
            return label
        return label + '/' + str(problem.Visits2)

    print_data = []
    for problem_config, mip_log, cp_team_log, cp_window_log in logs:
        print_data.append(collections.OrderedDict(Problem=get_problem_label(problem_config, cp_team_log.date),
                                                  Penalty=cp_team_log.missed_visit_penalty,
                                                  LB=mip_log.best_bound(),
                                                  MIP_COST=get_cost_label(mip_log.best_cost(), mip_log.best_bound()),
                                                  # MIP_GAP=get_gap_label(get_gap(mip_log.best_cost(), mip_log.best_bound())),
                                                  MIP_TIME=get_duration_label(mip_log.best_cost_time()),
                                                  # TEAMS_GAP=get_gap_label(get_gap(cp_team_log.best_cost(), mip_log.best_bound())),
                                                  TEAMS_COST=get_cost_label(cp_team_log.best_cost(), mip_log.best_bound()),
                                                  TEAMS_Time=get_duration_label(cp_team_log.best_cost_time()),
                                                  WINDOWS_COST=get_cost_label(cp_window_log.best_cost(), mip_log.best_bound()),
                                                  # WINDOWS_GAP=get_gap_label(get_gap(cp_window_log.best_cost(), mip_log.best_bound())),
                                                  WINDOWS_TIME=get_duration_label(cp_window_log.best_cost_time())
                                                  ))

    data_frame = pandas.DataFrame(data=print_data)
    print(tabulate.tabulate(data_frame, tablefmt='latex', headers='keys', showindex=False))


def compare_planner_optimizer_quality(args, settings):
    data_file = getattr(args, __FILE_ARG)
    data_frame = pandas.read_csv(data_file)

    figsize = (2.5, 5)
    labels = ['Planners', 'Algorithm']

    data_frame['travel_time'] = data_frame['Travel Time'].apply(parse_pandas_duration)
    data_frame['span'] = data_frame['Span'].apply(parse_pandas_duration)
    data_frame['overtime'] = data_frame['Overtime'].apply(parse_pandas_duration)

    data_frame_planners = data_frame[data_frame['Type'] == 'Planners']
    data_frame_solver = data_frame[data_frame['Type'] == 'Solver']

    overtime_per_carer = [list((data_frame_planners['overtime'] / data_frame_planners['Carers']).values),
                          list((data_frame_solver['overtime'] / data_frame_solver['Carers']).values)]

    def to_matplotlib_minutes(value):
        return value * 60 * 1000000000

    fig, ax = matplotlib.pyplot.subplots(1, 1, figsize=figsize)
    ax.boxplot(overtime_per_carer, flierprops=dict(marker='.'), medianprops=dict(color=FOREGROUND_COLOR))
    ax.set_xticklabels(labels, rotation=45)
    ax.set_ylabel('Overtime per Carer [HH:MM]')
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta_pandas))
    ax.set_yticks([0, to_matplotlib_minutes(10), to_matplotlib_minutes(20), to_matplotlib_minutes(30)])
    fig.tight_layout()
    rows.plot.save_figure('quality_boxplot_overtime')

    travel_time_per_carer = [list((data_frame_planners['travel_time'] / data_frame_planners['Carers']).values),
                             list((data_frame_solver['travel_time'] / data_frame_solver['Carers']).values)]
    fig, ax = matplotlib.pyplot.subplots(1, 1, figsize=figsize)
    ax.boxplot(travel_time_per_carer, flierprops=dict(marker='.'), medianprops=dict(color=FOREGROUND_COLOR))
    ax.set_xticklabels(labels, rotation=45)
    ax.set_ylabel('Travel Time per Carer [HH:MM]')
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta_pandas))
    ax.set_yticks([0, to_matplotlib_minutes(30), to_matplotlib_minutes(60),
                   to_matplotlib_minutes(90), to_matplotlib_minutes(120)])
    fig.tight_layout()
    rows.plot.save_figure('quality_boxplot_travel_time')

    span_per_client = [list((data_frame_planners['span'] / data_frame_planners['Clients']).values),
                       list((data_frame_solver['span'] / data_frame_solver['Clients']).values)]
    fig, ax = matplotlib.pyplot.subplots(1, 1, figsize=figsize)
    ax.boxplot(span_per_client, flierprops=dict(marker='.'), medianprops=dict(color=FOREGROUND_COLOR))
    ax.set_xticklabels(labels, rotation=45)
    ax.set_ylabel('Visit Span per Client [HH:MM]')
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta_pandas))
    ax.set_yticks([0, to_matplotlib_minutes(6 * 60), to_matplotlib_minutes(7 * 60), to_matplotlib_minutes(8 * 60),
                   to_matplotlib_minutes(9 * 60)])
    ax.set_ylim(bottom=6 * 60 * 60 * 1000000000)
    fig.tight_layout()
    rows.plot.save_figure('quality_span')

    teams = [list(data_frame_planners['Teams'].values), list(data_frame_solver['Teams'].values)]
    fig, ax = matplotlib.pyplot.subplots(1, 1, figsize=figsize)
    ax.boxplot(teams, flierprops=dict(marker='.'), medianprops=dict(color=FOREGROUND_COLOR))
    ax.set_xticklabels(labels, rotation=45)
    ax.set_ylabel('Teams of 2 Carers')
    fig.tight_layout()
    rows.plot.save_figure('quality_teams')

    better_matching = [list(data_frame_planners['Better Matching'].values),
                       list(data_frame_solver['Better Matching'].values)]
    fig, ax = matplotlib.pyplot.subplots(1, 1, figsize=figsize)
    ax.boxplot(better_matching, flierprops=dict(marker='.'), medianprops=dict(color=FOREGROUND_COLOR))
    ax.set_xticklabels(labels, rotation=45)
    ax.set_ylabel('Better Client-Carer Matching')
    fig.tight_layout()
    rows.plot.save_figure('quality_matching')


def parse_percent(value):
    value_to_use = value.replace('%', '')
    return float(value_to_use) / 100.0


def parse_duration_seconds(value):
    return datetime.timedelta(seconds=value)


def compare_benchmark(args, settings):
    data_file_path = getattr(args, __FILE_ARG)
    data_frame = pandas.read_csv(data_file_path)

    data_frame['relative_cost_difference'] = data_frame['Relative Cost Difference'].apply(parse_percent)
    data_frame['relative_gap'] = data_frame['Relative Gap'].apply(parse_percent)
    data_frame['time'] = data_frame['Time'].apply(parse_duration_seconds)

    matplotlib.rcParams.update({'font.size': 18})

    labels = ['MS', 'IP']
    low_labels = ['Gap', 'Delta', 'Time']

    cp_frame = data_frame[data_frame['Solver'] == 'CP']
    mip_frame = data_frame[data_frame['Solver'] == 'MIP']

    def get_series(frame, configuration):
        num_visits, num_visits_of_2 = configuration
        filtered_frame = frame[(frame['Visits'] == num_visits) & (frame['Synchronized Visits'] == num_visits_of_2)]
        return [filtered_frame['relative_gap'].values, filtered_frame['relative_cost_difference'].values,
                filtered_frame['time'].values]

    def seconds(value):
        return value * 1000000000

    def minutes(value):
        return 60 * seconds(value)

    def hours(value):
        return 3600 * seconds(value)

    limit_configurations = [[[None, minutes(1) + seconds(15)], [0, minutes(9)]],
                            [[None, minutes(1) + seconds(30)], [0, hours(4) + minutes(30)]],
                            [[0, minutes(3) + seconds(30)], [0, hours(4) + minutes(30)]],
                            [[0, minutes(3) + seconds(30)], [0, hours(4) + minutes(30)]]]

    yticks_configurations = [
        [[0, seconds(15), seconds(30), seconds(45), minutes(1)], [0, minutes(1), minutes(2), minutes(4), minutes(8)]],
        [[0, seconds(15), seconds(30), seconds(45), minutes(1), minutes(1) + seconds(15)],
         [0, hours(1), hours(2), hours(3), hours(4)]],
        [[0, minutes(1), minutes(2), minutes(3)], [0, hours(1), hours(2), hours(3), hours(4)]],
        [[0, minutes(1), minutes(2), minutes(3)], [0, hours(1), hours(2), hours(3), hours(4)]]]

    problem_configurations = [(25, 0), (25, 5), (50, 0), (50, 10)]

    def format_timedelta_pandas(x, pos=None):
        if x < 0:
            return None

        time_delta = pandas.to_timedelta(x)
        hours = int(time_delta.total_seconds() / matplotlib.dates.SEC_PER_HOUR)
        minutes = int(time_delta.total_seconds() / matplotlib.dates.SEC_PER_MIN) - 60 * hours
        seconds = int(time_delta.total_seconds() - 3600 * hours - 60 * minutes)
        return '{0:01d}:{1:02d}:{2:02d}'.format(hours, minutes, seconds)

    def format_percent(x, pox=None):
        return int(x * 100.0)

    for index, problem_config in enumerate(problem_configurations):
        fig, axes = matplotlib.pyplot.subplots(1, 2)

        cp_gap, cp_delta, cp_time = get_series(cp_frame, problem_config)
        mip_gap, mip_delta, mip_time = get_series(mip_frame, problem_config)

        cp_time_limit, mip_time_limit = limit_configurations[index]
        cp_yticks, mip_yticks = yticks_configurations[index]

        cp_ax, mip_ax = axes

        first_color_config = dict(flierprops=dict(marker='.'),
                                  medianprops=dict(color=FOREGROUND_COLOR),
                                  boxprops=dict(color=FOREGROUND_COLOR),
                                  whiskerprops=dict(color=FOREGROUND_COLOR),
                                  capprops=dict(color=FOREGROUND_COLOR))

        second_color_config = dict(flierprops=dict(marker='.'),
                                   medianprops=dict(color=FOREGROUND_COLOR2),
                                   boxprops=dict(color=FOREGROUND_COLOR2),
                                   whiskerprops=dict(color=FOREGROUND_COLOR2),
                                   capprops=dict(color=FOREGROUND_COLOR2))

        cp_ax.boxplot([cp_gap, cp_delta, []], **second_color_config)
        cp_twinx = cp_ax.twinx()
        cp_twinx.boxplot([[], [], cp_time], **first_color_config)
        cp_twinx.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta_pandas))
        cp_ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_percent))
        cp_twinx.tick_params(axis='y', labelcolor=FOREGROUND_COLOR)
        cp_ax.set_xlabel('Multistage')
        cp_ax.set_xticklabels(low_labels, rotation=45)
        cp_ax.set_ylim(bottom=-0.05, top=1)
        cp_ax.set_ylabel('Delta, Gap [%]')
        cp_twinx.set_ylim(bottom=cp_time_limit[0], top=cp_time_limit[1])
        if cp_yticks:
            cp_twinx.set_yticks(cp_yticks)

        mip_ax.boxplot([mip_gap, mip_delta, []], **second_color_config)
        mip_twinx = mip_ax.twinx()
        mip_twinx.boxplot([[], [], mip_time], **first_color_config)
        mip_twinx.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_timedelta_pandas))
        mip_twinx.tick_params(axis='y', labelcolor=FOREGROUND_COLOR)
        mip_ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(format_percent))
        mip_ax.set_xlabel('IP')
        mip_ax.set_xticklabels(low_labels, rotation=45)
        mip_ax.set_ylim(bottom=-0.05, top=1)
        mip_twinx.set_ylabel('Computation Time [H:MM:SS]', color=FOREGROUND_COLOR)
        mip_twinx.set_ylim(bottom=mip_time_limit[0], top=mip_time_limit[1])
        if mip_yticks:
            mip_twinx.set_yticks(mip_yticks)

        fig.tight_layout(w_pad=0.0)
        rows.plot.save_figure('benchmark_boxplot_{0}_{1}'.format(problem_config[0], problem_config[1]))
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(fig)


def old_debug(args, settings):
    problem = rows.plot.load_problem(get_or_raise(args, __PROBLEM_FILE_ARG))
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
    time_delta_converter = rows.plot.TimeDeltaConverter()
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
            rows.plot.save_figure(output_file_base_name + '_' + current_date)
        finally:
            matplotlib.pyplot.cla()
            matplotlib.pyplot.close(figure)


def compute_overtime(frame):
    idle_overtime_series = list(frame.Availability - frame.Travel - frame.Service)
    idle_series = numpy.array(
        list(map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
    overtime_series = numpy.array(list(map(lambda value: datetime.timedelta(
        seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(), idle_overtime_series)))
    return overtime_series


def debug(args, settings):
    pass


if __name__ == '__main__':
    sys.excepthook = handle_exception

    matplotlib.rcParams.update({'font.size': 12, 'pdf.fonttype': 42})
    matplotlib.rc('font', **{'sans-serif': ['Roboto']})

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
    elif __command == __COMPARE_QUALITY_COMMAND:
        compare_schedule_quality(__args, __settings)
    elif __command == __COMPARE_QUALITY_OPTIMIZER_COMMAND:
        compare_planner_optimizer_quality(__args, __settings)
    elif __command == __COMPARE_BENCHMARK_COMMAND:
        compare_benchmark(__args, __settings)
    elif __command == __COMPARE_COST_COMMAND:
        compare_schedule_cost(__args, __settings)
    elif __command == __COMPARE_BENCHMARK_TABLE_COMMAND:
        compare_benchmark_table(__args, __settings)
    elif __command == __DEBUG_COMMAND:
        debug(__args, __settings)
    else:
        raise ValueError('Unknown command: ' + __command)
