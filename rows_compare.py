# TODO calculate travel distance for a schedule in json
# TODO calculate travel distance for a schedule in gexf

import argparse
import collections
import datetime
import json
import logging
import os
import operator
import sys

import rows.settings
import rows.console
import rows.location_finder
import rows.sql_data_source

import rows.model.area
import rows.model.metadata
import rows.model.schedule
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
__AREA_ARG = 'area'
__FROM_ARG = 'from'
__TO_ARG = 'to'
__FILE_ARG = 'file'
__OUTPUT_PREFIX_ARG = 'output_prefix'
__OPTIONAL_ARG_PREFIX = '--'


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

    info_parser = subparsers.add_parser('info')
    info_parser.add_argument(__FILE_ARG)

    return parser


def get_or_raise(obj, prop):
    value = getattr(obj, prop)
    if not value:
        raise ValueError('{0} not set'.format(prop))
    return value


def get_date_time(value):
    date_time = datetime.datetime.strptime(value, '%Y-%m-%d')
    return date_time


def pull(args, install_directory):
    area_code = get_or_raise(args, __AREA_ARG)
    from_raw_date = get_or_raise(args, __FROM_ARG)
    to_raw_date = get_or_raise(args, __TO_ARG)
    output_prefix = get_or_raise(args, __OUTPUT_PREFIX_ARG)

    console = rows.console.Console()
    settings = rows.settings.Settings(install_directory)
    settings.reload()

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


def info(args):
    # calculate distance

    schedule_file = get_or_raise(args, __FILE_ARG)
    with open(schedule_file, 'r') as input_stream:
        schedule_dict = json.load(input_stream)
        metadata = rows.model.metadata.Metadata.from_json(schedule_dict['metadata'])
        visits = [rows.model.past_visit.PastVisit.from_json(raw_visit) for raw_visit in schedule_dict['visits']]
        schedule = rows.model.schedule.Schedule(metadata=metadata, visits=visits)

    routes = collections.defaultdict(list)
    for past_visit in schedule.visits:
        routes[past_visit.carer].append(past_visit)
    for carer in routes:
        routes[carer].sort(key=operator.attrgetter('time'))

    for carer in routes:
        print(carer.sap_number, carer.mobility)
        for visit in routes[carer]:
            print('\t', visit.time, visit.visit.service_user)


if __name__ == '__main__':
    sys.excepthook = handle_exception

    __script_file = os.path.realpath(__file__)
    __install_dir = os.path.dirname(__script_file)
    __parser = configure_parser()
    __args = __parser.parse_args(sys.argv[1:])
    __command = getattr(__args, __COMMAND)
    if __command == __PULL_COMMAND:
        pull(__args, __install_dir)
    elif __command == __INFO_COMMAND:
        info(__args)
    else:
        raise ValueError('Unknown command: ' + __command)
