import logging
import pathlib
import datetime
import statistics
import collections
import scipy.stats
import math
import itertools

import pyodbc

import itertools

from rows.util.file_system import real_path

from rows.model.address import Address
from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.event import AbsoluteEvent
from rows.model.problem import Problem
from rows.model.service_user import ServiceUser


class SqlDataSource:
    LIST_AREAS_QUERY = """SELECT aom.aom_id, aom.area_code
FROM [StrathClyde].[dbo].[ListAom] aom
ORDER BY aom.area_code"""

    LIST_VISITS_QUERY = """SELECT MIN(visits.visit_id) as visit_id, visits.service_user_id, visits.visit_date,
visits.requested_visit_time, visits.requested_visit_duration
FROM dbo.ListVisitsWithinWindow visits
WHERE visits.visit_date BETWEEN '{0}' AND '{1}' AND visits.aom_code = {2}
GROUP BY visits.service_user_id, visits.visit_date, visits.requested_visit_duration,
visits.display_address, visits.requested_visit_time"""

    LIST_SERVICE_USER_QUERY = """SELECT visits.service_user_id, visits.display_address
    FROM dbo.ListVisitsWithinWindow visits
    WHERE visits.visit_date BETWEEN '{0}' AND '{1}' AND visits.aom_code = {2}
    GROUP BY visits.service_user_id, visits.display_address"""

    LIST_MULTIPLE_CARER_VISITS_QUERY = """SELECT visits.VisitID as visit_id, COUNT(visits.VisitID) as carer_count
FROM dbo.ListCarerVisits visits
WHERE visits.PlannedStartDateTime BETWEEN '{0}' AND '{1}'
GROUP BY visits.PlannedStartDateTime, visits.VisitID
HAVING COUNT(visits.VisitID) > 1"""

    CARER_FREQUENCY_QUERY = """SELECT user_visit.service_user_id as 'user', carer_visit.PlannedCarerID as 'carer',
COUNT(user_visit.visit_id) AS visits_count,
MIN(total_visits.total_visits) AS total_visits,
ROUND(CONVERT(float, COUNT(user_visit.visit_id)) / MIN(total_visits.total_visits), 4) as care_continuity
FROM dbo.ListCarerVisits carer_visit
INNER JOIN (SELECT DISTINCT service_user_id, visit_id 
FROM dbo.ListVisitsWithinWindow
WHERE aom_code = {0}) user_visit 
ON carer_visit.VisitID = user_visit.visit_id
INNER JOIN (SELECT service_user_id as service_user_id, COUNT(visit_id) as total_visits
FROM (SELECT DISTINCT service_user_id, visit_id 
FROM dbo.ListVisitsWithinWindow
WHERE aom_code = {0}) local_visit
GROUP BY service_user_id) total_visits
ON total_visits.service_user_id = user_visit.service_user_id
GROUP BY user_visit.service_user_id, carer_visit.PlannedCarerID, total_visits.service_user_id
ORDER BY user_visit.service_user_id, care_continuity DESC"""

    CARER_WORKING_HOURS = """SELECT CarerId, StartDateTime, EndDateTime
FROM dbo.ListCarerIntervals
WHERE AomId={2} AND StartDateTime BETWEEN '{0}' AND '{1}'
"""

    GLOBAL_VISIT_DURATION = """SELECT visit.visit_id,
    STRING_AGG(task_id, ';') WITHIN GROUP (ORDER BY visit.task_id) as 'tasks',
    MAX(visit.planned_duration) as 'planned_duration',
    MAX(visit.real_duration) as 'real_duration'
FROM (
  SELECT visit.visit_id as visit_id,
          visit.task_id as task_id,
          MAX(visit.planned_duration) as planned_duration,
          MAX(visit.real_duration) as real_duration
  FROM
  (
      SELECT visit_id as 'visit_id',
            CONVERT(int, task_no) as 'task_id',
            requested_visit_duration * 60 as 'planned_duration',
            -- date may not be valid (is a day before), so it is removed
            -- time may span across multiple days, so need to split the difference into 2 parts
            -- filter out cases where checking and checkout are the same
            carer_visits.CheckInDateTime as 'checkin',
            carer_visits.CheckOutDateTime as 'checkout',
            (SELECT MIN(duration) FROM (
            VALUES
            (DATEDIFF(second, CONVERT(time, carer_visits.CheckInDateTime), CONVERT(time, carer_visits.CheckOutDateTime))),
            (DATEDIFF(second, CONVERT(time, carer_visits.CheckOutDateTime), CONVERT(time, carer_visits.CheckInDateTime))),
            (DATEDIFF(second, CONVERT(time, carer_visits.CheckInDateTime), CONVERT(time, '23:59:59')) + DATEDIFF(second, CONVERT(time, '00:00:00'), CONVERT(time, carer_visits.CheckOutDateTime)) + 1),
            (DATEDIFF(second, CONVERT(time, carer_visits.CheckOutDateTime), CONVERT(time, '23:59:59')) + DATEDIFF(second, CONVERT(time, '00:00:00'), CONVERT(time, carer_visits.CheckInDateTime)) + 1)
            ) as temp(duration)
            WHERE duration >= 0) AS 'real_duration'
        FROM dbo.ListVisitsWithinWindow task_visit
        INNER JOIN dbo.ListCarerVisits carer_visits
        ON carer_visits.VisitID = task_visit.visit_id
        WHERE carer_visits.CheckInDateTime != carer_visits.CheckOutDateTime
    ) visit
    GROUP BY visit.visit_id, visit.planned_duration, visit.task_id
) visit
GROUP BY visit.visit_id
ORDER BY tasks, visit_id
"""

    def __init__(self, settings, location_finder):
        self.__settings = settings
        self.__location_finder = location_finder
        self.__connection_string = None
        self.__connection = None

    def get_areas(self):
        cursor = self.__get_connection().cursor()
        cursor.execute(SqlDataSource.LIST_AREAS_QUERY)
        return [Area(key=row[0], code=row[1]) for row in cursor.fetchall()]

    def get_visits(self, area, begin_date, end_date):
        carer_counts = {}
        for row in self.__get_connection().cursor() \
                .execute(SqlDataSource.LIST_MULTIPLE_CARER_VISITS_QUERY.format(begin_date, end_date)).fetchall():
            visit_id, carer_count = row
            carer_counts[visit_id] = carer_count

        visits_by_service_user = {}
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.LIST_VISITS_QUERY.format(begin_date, end_date, area.key)).fetchall():
            visit_key, service_user_id, visit_date, visit_time, visit_duration = row

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            local_visit = Problem.LocalVisit(key=visit_key,
                                             date=visit_date,
                                             time=visit_time,
                                             duration=str(visit_duration * 60),  # convert minutes to seconds
                                             carer_count=carer_count)
            if service_user_id in visits_by_service_user:
                visits_by_service_user[service_user_id].append(local_visit)
            else:
                visits_by_service_user[service_user_id] = [local_visit]

        return [Problem.LocalVisits(service_user=str(service_user_id), visits=visits)
                for service_user_id, visits in visits_by_service_user.items()]

    def get_carers(self, area, begin_date, end_date):
        events_by_carer = collections.defaultdict(list)
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.CARER_WORKING_HOURS.format(begin_date, end_date, area.key)).fetchall():
            carer_id, begin_time, end_time = row
            events_by_carer[carer_id].append(AbsoluteEvent(begin=begin_time, end=end_time))
        carer_shifts = []
        for carer_id in events_by_carer.keys():
            diaries = [Diary(date=date, events=list(events), schedule_pattern=None)
                       for date, events
                       in itertools.groupby(events_by_carer[carer_id], key=lambda event: event.begin.date())]
            carer_shift = Problem.CarerShift(carer=Carer(sap_number=str(carer_id)), diaries=diaries)
            carer_shifts.append(carer_shift)
        return carer_shifts

    def get_service_users(self, area, begin_date, end_date):
        location_by_service_user = {}
        address_by_service_user = {}
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.LIST_SERVICE_USER_QUERY.format(begin_date, end_date, area.key)).fetchall():
            service_user, raw_address = row

            if service_user not in location_by_service_user:
                address = Address.parse(raw_address)
                location = self.__location_finder.find(address)
                if location is None:
                    logging.error("Failed to find location of the address '%s'", location)
                location_by_service_user[service_user] = location
                address_by_service_user[service_user] = address

        preference_by_service_user = collections.defaultdict(dict)
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.CARER_FREQUENCY_QUERY.format(area.key)).fetchall():
            service_user, carer, _carer_visit_count, _total_visit_count, frequency = row
            preference_by_service_user[service_user][carer] = frequency

        service_users = []
        for service_user_id, location in location_by_service_user.items():
            carer_preference = []
            for carer_id, preference in preference_by_service_user[service_user_id].items():
                carer_preference.append((str(carer_id), preference))

            service_users.append(ServiceUser(key=str(service_user_id),
                                             location=location,
                                             address=address_by_service_user[service_user_id],
                                             carer_preference=carer_preference))
        return service_users

    def get_visit_duration(self):

        def get_binominal_interval(n, p, confidence, max_error, max_size):
            pmf = [scipy.stats.binom.pmf(index, n, p) for index in range(0, n)]
            best_begin = None
            best_interval = [0]
            best_mean = 0.0
            for begin in range(0, n):
                interval = [pmf[begin]]
                next_index = begin + 1
                while len(interval) < max_size and next_index < n:
                    if abs(sum(interval) - confidence) <= max_error:
                        mean = statistics.mean(interval)
                        if mean > best_mean:
                            best_begin = begin
                            best_mean = mean
                            best_interval = list(interval)
                    interval.append(pmf[next_index])
                    next_index += 1
            if best_begin:
                return best_begin, best_begin + len(best_interval), sum(best_interval)
            else:
                return None, None, 0.0

        def get_percentile(p, values):
            pos = p * len(values)
            left_pos = int(math.trunc(pos))
            right_pos = left_pos + 1
            fraction = round(pos - left_pos, 4)
            if fraction == 0.0:
                return values[left_pos]
            else:
                return (1.0 - fraction) * values[left_pos] + fraction * values[right_pos]

        duration_by_tasks = {}
        rows = []
        for row in self.__get_connection().cursor().execute(SqlDataSource.GLOBAL_VISIT_DURATION).fetchall():
            visit, raw_tasks, raw_planned_duration, raw_real_duration = row
            rows.append((visit, raw_tasks, int(raw_planned_duration), int(raw_real_duration)))
        for key, group in itertools.groupby(rows, key=lambda r: r[1]):
            durations = [row[3] for row in group]
            durations.sort()
            _begin, end = get_binominal_interval(len(durations), 0.95, 0.95, 0.005, 40)
            if end:
                duration_by_tasks[key] = durations[end]

        print(duration_by_tasks)

    def reload(self):
        self.__get_connection_string()

    def __get_connection_string(self):
        if self.__connection_string:
            return self.__connection_string
        self.__connection_string = self.__build_connection_string()
        return self.__connection_string

    def __get_connection(self):
        if self.__connection:
            return self.__connection
        self.__connection = pyodbc.connect(self.__get_connection_string())
        return self.__connection

    def __enter__(self):
        self.__get_connection()

    def __exit__(self, exc_type, exc_value, traceback):
        if self.__connection:
            self.__connection.close()
            del self.__connection
            self.__connection = None

    def __load_credentials(self):
        path = pathlib.Path(real_path(self.__settings.database_credentials_path))
        if path.exists():
            with path.open() as file_stream:
                return file_stream.read().strip()
        return ""

    def __build_connection_string(self):
        config = {'Driver': '{ODBC Driver 13 for SQL Server}',
                  'Server': self.__settings.database_server,
                  'Database': self.__settings.database_name,
                  'UID': self.__settings.database_user,
                  'PWD': self.__load_credentials(),
                  'Encrypt': 'yes',
                  'TrustServerCertificate': 'yes'}
        return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])
