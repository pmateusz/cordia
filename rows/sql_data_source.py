import logging
import pathlib
import statistics
import collections
import math
import itertools
import datetime

import pyodbc
import scipy.stats

from rows.util.file_system import real_path

from rows.model.address import Address
from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.event import AbsoluteEvent
from rows.model.problem import Problem
from rows.model.service_user import ServiceUser


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
    pos = p * (len(values) - 1)
    left_pos = int(math.trunc(pos))
    right_pos = left_pos + 1
    fraction = round(pos - left_pos, 4)
    if fraction == 0.0:
        return values[left_pos]
    else:
        return (1.0 - fraction) * values[left_pos] + fraction * values[right_pos]


class IntervalSampler:

    def __init__(self, p, confidence, error):
        self.__p = p
        self.__error = error
        self.__confidence = confidence
        self.__cache = {}

        left = 40
        while math.fsum([scipy.stats.binom.pmf(index, left, self.__p) for index in range(0, left)]) > confidence:
            left = int(left / 2)

        right = 80
        while math.fsum([scipy.stats.binom.pmf(index, left, self.__p) for index in range(0, right)]) < confidence:
            right = int(right * 2)

        while left != right:
            middle = int((left + right) / 2)
            if math.fsum([scipy.stats.binom.pmf(index, left, self.__p) for index in range(0, left)]) >= confidence:
                right = middle
            else:
                left = middle

        self.__min_sample_size = left

    def __call__(self, n):
        if n <= self.__min_sample_size:
            return None, None, 0.0

        if n in self.__cache:
            return self.__cache[n]

        pmf = [scipy.stats.binom.pmf(index, n, self.__p) for index in range(0, n)]

        # find left and right end of initial confidence interval around the p'th percentile
        pos = self.__p * max(n - 1, 0)
        left_pos = int(math.trunc(pos))
        fraction = round(pos - left_pos, 4)
        if fraction > 0.0001:
            # percentile is a weighted average of two elements
            left = left_pos
            right = left_pos + 1
            current_confidence = pmf[left] + pmf[right]
        else:
            # percentile is exactly one element
            left = right = left_pos
            current_confidence = pmf[left]

        while abs(self.__confidence - current_confidence) >= self.__error and current_confidence < self.__confidence:
            left_opt = left - 1
            right_opt = right + 1

            if left_opt >= 0:
                if right_opt < n:
                    if pmf[left_opt] > pmf[right_opt]:
                        current_confidence += pmf[left_opt]
                        left = left_opt
                    else:
                        current_confidence += pmf[right_opt]
                        right = right_opt
                else:
                    current_confidence += pmf[left_opt]
                    left = left_opt
            elif right_opt < n:
                current_confidence += pmf[right_opt]
                right = right_opt
            else:
                self.__cache[n] = (None, None, 0.0)
                break
        self.__cache[n] = (left, right, current_confidence)
        return self.__cache[n]


class SqlDataSource:
    LIST_AREAS_QUERY = """SELECT aom.aom_id, aom.area_code
FROM [StrathClyde].[dbo].[ListAom] aom
ORDER BY aom.area_code"""

    LIST_VISITS_QUERY = """SELECT visit_id,
service_user_id,
vdate,
vtime,
vduration,
STRING_AGG(task_id, ';')  WITHIN GROUP (ORDER BY task_id) as 'tasks'
FROM (
  SELECT MIN(window_visits.visit_id) as visit_id,
    window_visits.service_user_id as service_user_id,
    window_visits.visit_date as vdate,
    window_visits.requested_visit_time as vtime,
    window_visits.requested_visit_duration as vduration,
    CONVERT(int, task_no) as task_id
  FROM dbo.ListVisitsWithinWindow window_visits
  LEFT OUTER JOIN dbo.UserVisits user_visits
  ON user_visits.visit_id = window_visits.visit_id
  WHERE window_visits.visit_date BETWEEN '{0}' AND '{1}' AND window_visits.aom_code = {2}
  GROUP BY window_visits.service_user_id,
    window_visits.visit_date,
    window_visits.requested_visit_duration,
    window_visits.requested_visit_time,
    task_no
) visit
GROUP BY visit.visit_id, visit.service_user_id, vdate, vtime, vduration"""

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

    class IntervalEstimatorBase(object):

        def __init__(self, min_duration=None):
            self.__min_duration = self.__get_min_duration(min_duration)

        def __call__(self, local_visit):
            return local_visit.duration

        def reload_if(self, console, connection_factory):
            if self.should_reload:
                self.reload(console, connection_factory)

        def reload(self, console, connection_factory):
            rows = []
            groups = []

            with console.create_progress_bar(leave=False, unit='') as bar:
                bar.set_description_str('Connecting to the database...')
                cursor = connection_factory().cursor()

                bar.set_description_str('Pulling information on tasks...')
                for row in cursor.execute(SqlDataSource.GLOBAL_VISIT_DURATION).fetchall():
                    visit, raw_tasks, raw_planned_duration, raw_real_duration = row
                    rows.append((visit, raw_tasks, int(raw_planned_duration), int(raw_real_duration)))
                bar.set_description_str('Aggregating tasks...')
                for key, group in itertools.groupby(rows, key=lambda r: r[1]):
                    groups.append((key, list(group)))

            with console.create_progress_bar(total=len(groups),
                                             desc='Estimating visit duration',
                                             unit='',
                                             leave=False) as bar:

                for key, group in groups:
                    bar.update(1)
                    self.process(key, group)

        def process(self, key, group):
            pass

        @property
        def should_reload(self):
            return False

        @property
        def min_duration(self):
            return self.__min_duration

        @staticmethod
        def __get_min_duration(min_duration):
            if not min_duration:
                return 0

            date_time = datetime.datetime.strptime(min_duration, '%H:%M:%S')
            time_delta = datetime.timedelta(hours=date_time.hour,
                                            minutes=date_time.minute,
                                            seconds=date_time.second)
            return time_delta.total_seconds()

    class GlobalTaskConfidenceIntervalEstimator(IntervalEstimatorBase):

        NAME = 'global_ci'

        def __init__(self, percentile, confidence, error, min_duration=None):
            super(SqlDataSource.GlobalTaskConfidenceIntervalEstimator, self).__init__(min_duration=min_duration)
            self.__sampler = IntervalSampler(percentile, confidence, error)
            self.__duration_by_task = {}

        def reload(self, console, connection_factory):
            self.__duration_by_task.clear()
            super(SqlDataSource.GlobalTaskConfidenceIntervalEstimator, self).reload(console, connection_factory)

        def process(self, key, group):
            durations = [row[3] for row in group]
            _lower_limit, upper_limit, _confidence = self.__sampler(len(durations))
            if upper_limit and self.min_duration < upper_limit:
                durations.sort()
                self.__duration_by_task[key] = str(durations[upper_limit])

        @property
        def should_reload(self):
            return bool(self.__duration_by_task)

        def __call__(self, local_visit):
            value = self.__duration_by_task.get(local_visit.tasks, None)
            if value:
                return value
            return super(SqlDataSource.IntervalEstimatorBase, self).__call__(local_visit)

    class GlobalPercentileEstimator(IntervalEstimatorBase):

        NAME = 'global_percentile'

        def __init__(self, percentile, min_duration=None):
            super(SqlDataSource.GlobalPercentileEstimator, self).__init__(min_duration=min_duration)
            self.__duration_by_task = {}
            self.__percentile = percentile

        @property
        def should_reload(self):
            return bool(self.__duration_by_task)

        def reload(self, console, connection_factory):
            self.__duration_by_task.clear()

            super(SqlDataSource.GlobalPercentileEstimator, self).reload(console, connection_factory)

        def process(self, key, group):
            durations = [row[3] for row in group]
            durations.sort()

            percentile_duration = get_percentile(self.__percentile, durations)
            if percentile_duration and self.min_duration < percentile_duration:
                self.__duration_by_task[key] = str(percentile_duration)

        def __call__(self, local_visit):
            value = self.__duration_by_task.get(local_visit.tasks, None)
            return value if value else super(SqlDataSource.GlobalPercentileEstimator, self).__call__(local_visit)

    class PlannedDurationEstimator:

        NAME = 'fixed'

        def __init__(self):
            pass

        def reload(self, console, connection_factory):
            pass

        def reload_if(self, console, connection_factory):
            pass

        def __call__(self, local_visit):
            return local_visit.duration

    def __init__(self, settings, console, location_finder):
        self.__settings = settings
        self.__console = console
        self.__location_finder = location_finder
        self.__connection_string = None
        self.__connection = None

    def get_areas(self):
        cursor = self.__get_connection().cursor()
        cursor.execute(SqlDataSource.LIST_AREAS_QUERY)
        return [Area(key=row[0], code=row[1]) for row in cursor.fetchall()]

    def get_visits(self, area, begin_date, end_date, duration_estimator):
        duration_estimator.reload(self.__console, self.__get_connection)

        carer_counts = {}
        for row in self.__get_connection().cursor() \
                .execute(SqlDataSource.LIST_MULTIPLE_CARER_VISITS_QUERY.format(begin_date, end_date)).fetchall():
            visit_id, carer_count = row
            carer_counts[visit_id] = carer_count

        visits_by_service_user = {}
        time_change = []
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.LIST_VISITS_QUERY.format(begin_date, end_date, area.key)).fetchall():
            visit_key, service_user_id, visit_date, visit_time, visit_duration, tasks = row

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            local_visit = Problem.LocalVisit(key=visit_key,
                                             date=visit_date,
                                             time=visit_time,
                                             duration=str(visit_duration * 60),  # convert minutes to seconds
                                             tasks=tasks,
                                             carer_count=carer_count)

            original_duration = int(float(local_visit.duration))
            local_visit.duration = duration_estimator(local_visit)
            duration_to_use = int(float(local_visit.duration))
            time_change.append(duration_to_use - original_duration)

            if service_user_id in visits_by_service_user:
                visits_by_service_user[service_user_id].append(local_visit)
            else:
                visits_by_service_user[service_user_id] = [local_visit]

        if time_change:
            mean_stats = int(statistics.mean(time_change))
            median_stats = int(statistics.median(time_change))
            stddev_stats = int(statistics.stdev(time_change))

            def get_sign(value):
                return '-' if value < 0 else '+'

            # we apply custom logic to display negative duration, because the standard format is misleading
            self.__console.write_line('Change in visit duration: mean {0}{1}, median: {2}{3}, stddev {4}{5}'
                                      .format(get_sign(mean_stats), datetime.timedelta(seconds=abs(mean_stats)),
                                              get_sign(median_stats), datetime.timedelta(seconds=abs(median_stats)),
                                              get_sign(stddev_stats), datetime.timedelta(seconds=abs(stddev_stats))))

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
        try:
            self.__connection = pyodbc.connect(self.__get_connection_string())
        except pyodbc.OperationalError as ex:
            error_msg = "Failed to establish connection with the database server: '{0}'. " \
                        "Ensure that the database server is available in the network," \
                        " database '{1}' exists, username '{2}' is authorized to access the database." \
                        " and the password is valid".format(self.__settings.database_server,
                                                            self.__settings.database_name,
                                                            self.__settings.database_user)
            raise RuntimeError(error_msg, ex)
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
        try:
            with path.open() as file_stream:
                return file_stream.read().strip()
        except FileNotFoundError as ex:
            raise RuntimeError("Failed to open the the file '{0}' which is expected to store the database credentials."
                               " Create the file in the specified location and try again.".format(path), ex)

    def __build_connection_string(self):
        config = {'Driver': '{ODBC Driver 13 for SQL Server}',
                  'Server': self.__settings.database_server,
                  'Database': self.__settings.database_name,
                  'UID': self.__settings.database_user,
                  'PWD': self.__load_credentials(),
                  'Encrypt': 'yes',
                  'Connection Timeout': 5,
                  'TrustServerCertificate': 'yes'}
        return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])
