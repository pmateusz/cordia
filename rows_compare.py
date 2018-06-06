# TODO create histogram

import argparse
import collections
import datetime
import json
import logging
import operator
import os
import os.path
import subprocess
import sys

import bs4

import rows.settings
import rows.console
import rows.location_finder
import rows.sql_data_source

import rows.model.carer
import rows.model.area
import rows.model.service_user
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
    routes = collections.defaultdict(list)
    for past_visit in schedule.visits:
        routes[past_visit.carer].append(past_visit)
    for carer in routes:
        routes[carer].sort(key=operator.attrgetter('time'))

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
        schedule_dict = json.load(input_stream)
        metadata = rows.model.metadata.Metadata.from_json(schedule_dict['metadata'])
        visits = [rows.model.past_visit.PastVisit.from_json(raw_visit) for raw_visit in schedule_dict['visits']]
        return rows.model.schedule.Schedule(metadata=metadata, visits=visits)


def load_schedule_from_gexf(file_path):
    with open(file_path, 'r') as input_stream:
        soup = bs4.BeautifulSoup(input_stream, "html5lib")
        attributes = {}
        for node in soup.find_all('attribute'):
            attributes[node['title']] = node['id']

        type_id = attributes['type']
        sap_number_id = attributes['sap_number']
        id_id = attributes['id']
        user_id = attributes['user']
        start_time_id = attributes['start_time']
        duration_id = attributes['duration']
        longitude_id = attributes['longitude']
        latitude_id = attributes['latitude']

        carers_by_id = {}
        users_by_id = {}
        visits_by_id = {}
        for node in soup.find_all('node'):
            attributes = node.find('attvalues')
            type_attr = attributes.find('attvalue', attrs={'for': type_id})
            if type_attr['value'] == 'carer':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                sap_number_attr = attributes.find('attvalue', attrs={'for': sap_number_id})
                carers_by_id[node['id']] = rows.model.carer.Carer(key=id_number_attr['value'],
                                                                  sap_number=sap_number_attr['value'])
            elif type_attr['value'] == 'user':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                longitude_attr = attributes.find('attvalue', attrs={'for': longitude_id})
                latitude_attr = attributes.find('attvalue', attrs={'for': latitude_id})
                user = rows.model.service_user.ServiceUser(key=id_number_attr['value'],
                                                           location=rows.model.location.Location(
                                                               latitude=latitude_attr['value'],
                                                               longitude=longitude_attr['value']))
                users_by_id[node['id']] = user
            elif type_attr['value'] == 'visit':
                user_attr = attributes.find('attvalue', attrs={'for': user_id})
                start_time_attr = attributes.find('attvalue', attrs={'for': start_time_id})
                duration_attr = attributes.find('attvalue', attrs={'for': duration_id})
                start_time = datetime.datetime.strptime(start_time_attr['value'], '%Y-%b-%d %H:%M:%S')
                duration = datetime.datetime.strptime(duration_attr['value'], '%H:%M:%S').time()
                visits_by_id[node['id']] = rows.model.visit.Visit(date=start_time.date(),
                                                                  time=start_time.time(),
                                                                  duration=datetime.timedelta(hours=duration.hour,
                                                                                              minutes=duration.minute,
                                                                                              seconds=duration.second),
                                                                  service_user=int(user_attr['value']))

        routes = collections.defaultdict(list)
        # iterate over edges
        for edge in soup.find_all('edge'):
            source_id = edge['source']
            target_id = edge['target']
            if source_id in carers_by_id and target_id in visits_by_id:
                routes[carers_by_id[source_id]].append(visits_by_id[target_id])

        past_visits = []
        for carer in routes:
            for visit in routes[carer]:
                past_visits.append(rows.model.past_visit.PastVisit(visit=visit,
                                                                   date=visit.date,
                                                                   time=visit.time,
                                                                   duration=visit.duration,
                                                                   carer=carer))
        past_visits.sort(key=operator.attrgetter('time'))
        return rows.model.schedule.Schedule(metadata=rows.model.metadata.Metadata(
            begin=min(past_visits, key=operator.attrgetter('date')),
            end=max(past_visits, key=operator.attrgetter('date'))),
            visits=past_visits)


def info(args, settings):
    # calculate distance

    user_tag_finder = rows.location_finder.UserLocationFinder(settings)
    user_tag_finder.reload()

    schedule_file = get_or_raise(args, __FILE_ARG)
    schedule_file_to_use = os.path.realpath(os.path.expandvars(schedule_file))
    file_name, file_ext = os.path.splitext(schedule_file_to_use)
    if file_ext == '.json':
        schedule = load_schedule_from_json(schedule_file_to_use)
    elif file_ext == '.gexf':
        schedule = load_schedule_from_gexf(schedule_file_to_use)
    else:
        raise ValueError('Not recognized file format: ' + file_ext)
    print(get_travel_time(schedule, user_tag_finder))


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
    else:
        raise ValueError('Unknown command: ' + __command)
