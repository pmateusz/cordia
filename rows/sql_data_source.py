import concurrent
import concurrent.futures
import logging
import pandas
import pathlib
import statistics
import collections
import operator
import math
import itertools
import datetime
import warnings
import typing

import numpy

import pyodbc

import tqdm

import scipy.stats

from rows.model.metadata import Metadata
from rows.model.past_visit import PastVisit
from rows.model.schedule import Schedule
from rows.model.visit import Visit
from rows.util.file_system import real_path

from rows.model.address import Address
from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.event import AbsoluteEvent
from rows.model.problem import Problem
from rows.model.service_user import ServiceUser
from rows.model.historical_visit import HistoricalVisit
import rows.analysis
import rows.clustering


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
    PLANNED_RESOURCE_ESTIMATOR_NAME = 'planned'

    USED_RESOURCE_ESTIMATOR_NAME = 'used'

    LIST_AREAS_QUERY = """SELECT aom.aom_id, aom.area_code
FROM [dbo].[ListAom] aom
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
  WHERE window_visits.visit_date BETWEEN '{0}' AND '{1}' AND window_visits.aom_code = {2}
  GROUP BY window_visits.service_user_id,
    window_visits.visit_date,
    window_visits.requested_visit_duration,
    window_visits.requested_visit_time,
    task_no
) visit
GROUP BY visit.visit_id, visit.service_user_id, vdate, vtime, vduration"""

    LIST_CARER_SKILLS = """
SELECT CarerId, STRING_AGG(carer_skill.TaskNumber, ';') WITHIN GROUP (ORDER BY carer_skill.TaskNumber) as Skills
FROM (
    SELECT DISTINCT CarerId, Convert(INT, task_no) AS TaskNumber
FROM dbo.ListCarerIntervals carer_int
LEFT OUTER JOIN dbo.ListEmployees emp
ON carer_int.CarerId = emp.carer_id
LEFT OUTER JOIN dbo.ListCarerVisits carer_visits
ON carer_visits.OriginalCarerId = carer_int.CarerId
INNER JOIN dbo.ListVisitsWithinWindow visits
ON carer_visits.VisitID = visits.visit_id
WHERE carer_int.AomId = '{0}' AND StartDateTime BETWEEN '{1}' AND '{2}'
GROUP BY CarerId, task_no
) as carer_skill
GROUP BY carer_skill.CarerId
ORDER BY carer_skill.CarerId
"""

    LIST_PAST_VISITS_QUERY_WITH_CHECKOUT_INFO = """
SELECT carer_visits.VisitID, visit_tasks.[User], carer_visits.PlannedCarerID,
carer_visits.PlannedStartDateTime, carer_visits.PlannedEndDateTime,
dbo.CalculateDuration(carer_visits.PlannedStartDateTime, carer_visits.PlannedEndDateTime) as PlannedDuration, 
COALESCE(carer_visits.OriginalStartDateTime, carer_visits.PlannedStartDateTime) as OriginalStartDateTime,
COALESCE(carer_visits.OriginalEndDateTime, carer_visits.PlannedEndDateTime) as OriginalEndDateTime,
dbo.CalculateDuration(COALESCE(carer_visits.OriginalStartDateTime, carer_visits.PlannedStartDateTime), COALESCE(carer_visits.OriginalEndDateTime, carer_visits.PlannedEndDateTime)) as OriginalDuration,
carer_visits.CheckInDateTime,
carer_visits.CheckOutDateTime,
dbo.CalculateDuration(carer_visits.CheckInDateTime,carer_visits.CheckOutDateTime) as RealDuration,
carer_visits.CheckOutMethod,
visit_tasks.Tasks, visit_tasks.Area
FROM dbo.ListCarerVisits carer_visits
INNER JOIN (
    SELECT visit_orders.visit_id as 'VisitID',
    MIN(service_user_id) as 'User',
    MIN(area_code) as 'Area',
    STRING_AGG(visit_orders.task, '-') WITHIN GROUP (ORDER BY visit_orders.task) as 'Tasks'
    FROM (
        SELECT DISTINCT visit_window.visit_id as visit_id,
            CONVERT(int, visit_window.task_no) as task,
            MIN(visit_window.service_user_id) as service_user_id, 
            MIN(visit_window.requested_visit_time) as visit_time,
            MIN(visit_window.requested_visit_duration) as visit_duration,
            MIN(aom.area_code) as area_code
        FROM dbo.ListVisitsWithinWindow visit_window
        INNER JOIN dbo.ListAom aom
        ON aom.aom_id = visit_window.aom_code
        WHERE area_code = '{0}' AND visit_window.visit_date < '{1}'
        GROUP BY visit_window.visit_id, visit_window.task_no
    ) visit_orders
    GROUP BY visit_orders.visit_id
) visit_tasks
ON visit_tasks.VisitID = carer_visits.VisitID
"""

    LIST_CARER_AOM_QUERY = """SELECT employee.carer_id, employee.position_hours, aom.aom_id, aom.area_code
FROM dbo.ListEmployees employee
INNER JOIN dbo.ListAom aom
ON employee.aom_id = aom.aom_id"""

    LIST_VISITS_QUERY_BY_PLANNED_RESOURCES = """SELECT visit_id,
service_user_id,
vdate,
vtime,
vduration,
STRING_AGG(task_id, ';')  WITHIN GROUP (ORDER BY task_id) as 'tasks'
FROM (
 SELECT MIN(window_visits.visit_id) as visit_id,
    window_visits.service_user_id as service_user_id,
    CONVERT(date, carer_visits.PlannedStartDateTime) as vdate,
    CONVERT(time, carer_visits.PlannedStartDateTime) as vtime,
    DATEDIFF(minute, carer_visits.PlannedStartDateTime, carer_visits.PlannedEndDateTime) as vduration,
    CONVERT(int, task_no) as task_id
  FROM dbo.ListVisitsWithinWindow window_visits
  LEFT OUTER JOIN dbo.ListCarerVisits carer_visits
  ON window_visits.visit_id = carer_visits.VisitID
  WHERE window_visits.visit_date BETWEEN '{0}' AND '{1}'
    AND window_visits.aom_code = {2}
    AND carer_visits.VisitAssignmentID IS NOT NULL
  GROUP BY window_visits.service_user_id,
    carer_visits.PlannedStartDateTime,
    carer_visits.PlannedEndDateTime,
    task_no
) visit
GROUP BY visit.visit_id, visit.service_user_id, vdate, vtime, vduration"""

    SCHEDULE_QUERY = """SELECT DISTINCT visits.visit_id,
carer_visits.PlannedStartDateTime as 'planned_start_time',
carer_visits.PlannedEndDateTime as 'planned_end_time',
carer_visits.PlannedCarerID as 'carer_id',
COALESCE(emp.is_mobile_worker, 0) as 'is_mobile',
visits.service_user_id as 'service_user_id',
visits.tasks as 'tasks' FROM (
SELECT visit_task.visit_id,
MIN(visit_task.service_user_id) as 'service_user_id',
STRING_AGG(visit_task.task, ';') WITHIN GROUP (ORDER BY visit_task.task) as 'tasks'
FROM (
SELECT visit_window.visit_id as 'visit_id',
MIN(visit_window.service_user_id) as 'service_user_id',
CONVERT(int, visit_window.task_no) as 'task'
FROM dbo.ListVisitsWithinWindow visit_window
INNER JOIN ListAom aom
ON visit_window.aom_code = aom.aom_id
WHERE aom.area_code = '{0}' AND visit_window.visit_date BETWEEN '{1}' AND '{2}'
GROUP BY visit_window.visit_id, visit_window.task_no
) visit_task GROUP BY visit_task.visit_id
) visits LEFT OUTER JOIN dbo.ListCarerVisits carer_visits
ON visits.visit_id = carer_visits.VisitID
LEFT OUTER JOIN dbo.ListEmployees emp
ON emp.carer_id = carer_visits.PlannedCarerID
WHERE carer_visits.VisitID IS NOT NULL
ORDER BY carer_visits.PlannedCarerID, planned_start_time"""

    CARER_INTERVAL_QUERY = """SELECT intervals.CarerId as carer_id,
intervals.StartDateTime as start_datetime,
intervals.EndDateTime as end_datetime
  FROM dbo.ListCarerIntervals intervals
  WHERE intervals.StartDateTime >= '{0}'
    AND intervals.EndDateTime <= '{1}'"""

    SINGLE_CARER_INTERVAL_QUERY = """SELECT intervals.CarerId as carer_id,
    intervals.StartDateTime as start_datetime,
    intervals.EndDateTime as end_datetime
      FROM dbo.ListCarerIntervals intervals
      WHERE intervals.StartDateTime >= '{0}'
        AND intervals.EndDateTime <= '{1}'
        AND intervals.CarerId = {2}"""

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

    CARER_WORKING_HOURS = """SELECT CarerId, COALESCE(emp.is_mobile_worker, 0) as 'IsMobile', StartDateTime, EndDateTime
FROM dbo.ListCarerIntervals
LEFT OUTER JOIN dbo.ListEmployees emp
ON emp.carer_id = CarerId
WHERE AomId={2} AND StartDateTime BETWEEN '{0}' AND '{1}'
"""

    GLOBAL_VISIT_DURATION = """SELECT visit_id,
STRING_AGG(task_id, ';') WITHIN GROUP (ORDER BY visits.task_id) as 'tasks',
MIN(visits.planned_duration) as planned_duration,
MIN(visits.duration) as duration
FROM
(
  SELECT symmetric_visit.visit_id as visit_id, symmetric_visit.task_id as task_id, MAX(symmetric_visit.planned_duration) as planned_duration, MAX(symmetric_visit.duration) as duration
  FROM (
    SELECT task_visit.visit_id as 'visit_id',
    CONVERT(int, task_no) as 'task_id',
    requested_visit_duration * 60 as 'planned_duration',
    dbo.CalculateDuration(carer_visits.CheckInDateTime, carer_visits.CheckOutDateTime) as duration
    FROM dbo.ListVisitsWithinWindow task_visit
      INNER JOIN dbo.ListCarerVisits carer_visits
      ON carer_visits.VisitID = task_visit.visit_id
      WHERE (carer_visits.CheckOutMethod = 1 OR carer_visits.CheckOutMethod = 2)
    ) symmetric_visit
    WHERE symmetric_visit.duration >= 5 * 60
    GROUP BY symmetric_visit.visit_id, symmetric_visit.task_id
) visits
GROUP BY visits.visit_id
"""

    SCHEDULE_DETAIL_QUERY = """SELECT details.StartTime as 'start_time',
details.EndTime as 'end_time',
details.WeekNumber as 'week_number',
details.Day as 'day',
details.Type as 'type'
FROM dbo.ListSchedule schedule
INNER JOIN dbo.ListScheduleDetails details
ON details.SchedulePatternID = schedule.SchedulePatternId
WHERE schedule.EmployeePositionId = {0}
AND schedule.StartDate <= '{2}'
AND schedule.EndDate >= '{1}'"""

    CARER_SCHEDULE_QUERY = """SELECT DISTINCT details.WeekNumber as 'week_number',
details.Day as 'day',
details.StartTime as 'start_time',
details.EndTime as 'end_time'
FROM dbo.ListSchedule schedule
INNER JOIN dbo.ListScheduleDetails details
ON details.SchedulePatternID = schedule.SchedulePatternId
WHERE details.Type ='Shift'
AND schedule.StartDate <= '{2}'
AND schedule.EndDate >= '{1}'
AND schedule.EmployeePositionId = {0}"""

    PLANNED_SCHEDULE_QUERY = """SELECT carer_visits.VisitID as visit_id,
carer_visits.PlannedCarerID as carer_id,
carer_visits.CheckInDateTime as check_in,
carer_visits.CheckOutDateTime as check_out,
CONVERT(date, carer_visits.PlannedStartDateTime) as 'date',
CONVERT(time, carer_visits.PlannedStartDateTime) as 'planned_time',
CONVERT(time, carer_visits.CheckInDateTime) as 'real_time',
DATEDIFF(SECOND, carer_visits.PlannedStartDateTime, carer_visits.PlannedEndDateTime) as 'planned_duration',
DATEDIFF(SECOND, carer_visits.CheckInDateTime, carer_visits.CheckOutDateTime) as 'real_duration',
covered_visits.address as address,
covered_visits.service_user as service_user,
covered_visits.carer_count as carer_count
FROM dbo.ListCarerVisits carer_visits
INNER JOIN (
  SELECT inner_visits.visit_id, inner_visits.address, inner_visits.service_user, COUNT(inner_visits.carer_id) as carer_count
    FROM (
      SELECT DISTINCT inner_carer_visits.VisitID as 'visit_id',
      inner_carer_visits.PlannedCarerID as 'carer_id',
      planned_visits.display_address as 'address',
      planned_visits.service_user_id as 'service_user'
      FROM ListCarerVisits inner_carer_visits
      INNER JOIN dbo.ListVisitsWithinWindow planned_visits
      ON inner_carer_visits.VisitID = planned_visits.visit_id
      INNER JOIN dbo.ListAom aom
      ON aom.aom_id = planned_visits.aom_code
      WHERE aom.area_code = '{0}' AND CONVERT(date, inner_carer_visits.PlannedStartDateTime) BETWEEN '{1}' AND '{2}'
      GROUP BY inner_carer_visits.VisitID, planned_visits.display_address, planned_visits.service_user_id, inner_carer_visits.PlannedCarerID
    ) inner_visits
    GROUP BY visit_id, service_user, address
) covered_visits
ON carer_visits.VisitID = covered_visits.visit_id"""

    PAST_VISIT_DURATION = """SELECT carer_visits.VisitID as visit_id,
AVG(DATEDIFF(SECOND, carer_visits.CheckInDateTime, carer_visits.CheckOutDateTime)) as avg_duration
FROM dbo.ListCarerVisits carer_visits
INNER JOIN dbo.ListVisitsWithinWindow visit_window
ON visit_window.visit_id = carer_visits.VisitID
INNER JOIN dbo.ListAom aom
ON aom.aom_id = visit_window.aom_code
WHERE aom.area_code = '{0}'
AND (carer_visits.CheckInMethod = 1 OR carer_visits.CheckInMethod = 2)
AND visit_window.visit_date BETWEEN '{1}' AND '{2}'
GROUP BY carer_visits.VisitID
ORDER BY carer_visits.VisitID"""

    VISITS_HISTORY = """SELECT visits.visit_id,
carer_visits.PlannedStartDateTime as 'planned_start_time',
carer_visits.PlannedEndDateTime as 'planned_end_time',
carer_visits.CheckInDateTime as 'check_in',
carer_visits.CheckOutDateTime as 'check_out',
visits.service_user_id as 'service_user_id',
visits.tasks as 'tasks' FROM (
SELECT visit_task.visit_id,
MIN(visit_task.service_user_id) as 'service_user_id',
STRING_AGG(visit_task.task, ';') WITHIN GROUP (ORDER BY visit_task.task) as 'tasks'
FROM (
	SELECT visit_window.visit_id as 'visit_id',
	MIN(visit_window.service_user_id) as 'service_user_id',
	CONVERT(int, visit_window.task_no) as 'task'
	FROM dbo.ListVisitsWithinWindow visit_window
		INNER JOIN ListAom aom
	ON visit_window.aom_code = aom.aom_id
	WHERE aom.area_code = '{0}'
	GROUP BY visit_window.visit_id, visit_window.task_no
) visit_task GROUP BY visit_task.visit_id
) visits LEFT OUTER JOIN dbo.ListCarerVisits carer_visits
ON visits.visit_id = carer_visits.VisitID
LEFT OUTER JOIN dbo.ListEmployees emp
ON emp.carer_id = carer_visits.PlannedCarerID
WHERE carer_visits.VisitID IS NOT NULL
ORDER BY visits.visit_id"""

    class IntervalEstimatorBase(object):

        def __init__(self, min_duration=None):
            self.__min_duration = self.__get_min_duration(min_duration)

        def __call__(self, local_visit):
            return str(int(max(float(local_visit.duration), self.min_duration)))

        def reload_if(self, console, connection_factory):
            if self.should_reload:
                self.reload(console, connection_factory)

        def reload(self, console, connection_factory, area, start_date, end_date):
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

        def reload(self, console, connection_factory, area, start_date, end_date):
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

    class ProphetForecastEstimator(object):

        NAME = 'prophet-forecast'

        def __init__(self):
            super(SqlDataSource.ProphetForecastEstimator, self).__init__()

            self.__user_clusters = collections.defaultdict(list)
            self.__cluster_models = {}

        def reload(self, console, connection_factory, area, start_date, end_date):
            import rows.forecast.cluster
            import rows.forecast.visit
            import rows.forecast.forecast

            cursor = connection_factory().cursor()
            visits = []

            for row in cursor.execute(
                    SqlDataSource.LIST_PAST_VISITS_QUERY_WITH_CHECKOUT_INFO.format(area.code, start_date)) \
                    .fetchall():
                visit_raw_id, \
                user_raw_id, \
                planned_carer_id, \
                planned_start_date_time, \
                planned_end_date_time, \
                planned_duration, \
                original_start_date_time, \
                original_end_date_time, \
                original_duration, \
                real_start_date_time, \
                real_end_date_time, \
                real_duration, \
                check_out_raw_method, \
                raw_tasks, \
                area_code = row

                visit = rows.forecast.visit.Visit(
                    visit_id=int(visit_raw_id),
                    client_id=int(user_raw_id),
                    tasks=rows.forecast.visit.Tasks(raw_tasks),
                    area=area_code,
                    carer_id=int(planned_carer_id),
                    planned_start=planned_start_date_time,
                    planned_end=planned_end_date_time,
                    planned_duration=(planned_end_date_time - planned_start_date_time),
                    real_start=real_start_date_time,
                    real_end=real_end_date_time,
                    real_duration=(real_end_date_time - real_start_date_time),
                    check_in_processed=bool(check_out_raw_method))

                visits.append(visit)

            def cluster(visit_group):
                model = rows.forecast.cluster.AgglomerativeModel(
                    rows.forecast.cluster.NoSameDayPlannedStarDurationDistanceMatrix())
                return model.cluster(visit_group)

            def build_model(cluster, start_time, end_time):
                model = rows.forecast.forecast.ForecastModel()
                model.train(cluster.visits, start_time, end_time)
                return cluster, model

            with warnings.catch_warnings():
                warnings.filterwarnings('ignore', '', tqdm.TqdmSynchronisationWarning)
                visits_to_use = rows.forecast.visit.filter_incorrect_visits(visits)
                visits_to_use.sort(key=lambda v: v.client_id)
                visit_groups = {client_id: list(visit_group)
                                for client_id, visit_group in itertools.groupby(visits_to_use, lambda v: v.client_id)}

                user_clusters = collections.defaultdict(list)
                with warnings.catch_warnings():
                    warnings.filterwarnings('ignore', '', tqdm.TqdmSynchronisationWarning)
                    with concurrent.futures.ThreadPoolExecutor() as executor:
                        futures_list = [executor.submit(cluster, visit_groups[visit_group])
                                        for visit_group in visit_groups]
                        with tqdm.tqdm(desc='Computing clusters', total=len(futures_list),
                                       leave=False) as cluster_progress_bar:
                            for f in concurrent.futures.as_completed(futures_list):
                                try:
                                    client_clusters = f.result()
                                    for cluster in client_clusters:
                                        user_clusters[cluster.client_id].append(cluster)
                                    cluster_progress_bar.update(1)
                                except:
                                    logging.exception('Exception in processing results')
                                    pass

                        self.__user_clusters = dict(user_clusters)

                        start_time = datetime.datetime.combine(start_date, datetime.time())
                        end_time = datetime.datetime.combine(end_date, datetime.time())
                        cluster_models = dict()
                        futures_list = []
                        for client_id, clusters in self.__user_clusters.items():
                            for cluster in clusters:
                                future_handle = executor.submit(build_model, cluster, start_time, end_time)
                                futures_list.append(future_handle)

                        with tqdm.tqdm(desc='Forecasting', total=len(futures_list),
                                       leave=False) as forecast_progress_bar:
                            for f in concurrent.futures.as_completed(futures_list):
                                try:
                                    cluster, model = f.result()
                                    cluster_models[cluster] = model
                                    forecast_progress_bar.update(1)
                                except:
                                    logging.exception('Exception in processing results')

                        self.__cluster_models = cluster_models

        @property
        def should_reload(self):
            return True

        def __call__(self, local_visit):
            import rows.forecast.visit
            import rows.forecast.cluster

            planned_start_time = datetime.datetime.combine(local_visit.date, local_visit.time)
            planned_duration = datetime.timedelta(seconds=int(local_visit.duration))
            planned_end_time = planned_start_time + planned_duration
            tasks = rows.forecast.visit.Tasks(local_visit.tasks)

            visit_to_use = rows.forecast.visit.Visit(visit_id=local_visit.key,
                                                     client_id=local_visit.service_user,
                                                     carer_id=None,
                                                     area=None,
                                                     tasks=tasks,
                                                     planned_start=planned_start_time,
                                                     planned_end=planned_end_time,
                                                     planned_duration=planned_duration,
                                                     real_start=None,
                                                     real_end=None,
                                                     real_duration=None,
                                                     check_in_processed=False)

            if visit_to_use.client_id not in self.__user_clusters:
                return visit_to_use.planned_duration

            user_clusters = self.__user_clusters[visit_to_use.client_id]
            assert user_clusters

            distances = [(cluster, cluster.distance(visit_to_use)) for cluster in user_clusters]
            cluster, min_score = min(distances, key=operator.itemgetter(1))
            if min_score <= rows.forecast.cluster.AgglomerativeModel.DISTANCE_THRESHOLD and len(cluster.visits) >= 16:
                return self.__cluster_models[cluster].forecast(visit_to_use.planned_start.date())
            return visit_to_use.planned_duration

    class ArimaForecastEstimator(object):

        NAME = 'forecast'

        class MeanModel:
            def __init__(self, mean):
                if not isinstance(mean, float) or math.isnan(mean):
                    raise ValueError()

                self.__mean = mean

            def forecast(self, date):
                return self.__mean

        class ARIMAModel:
            def __init__(self, last_date, model, trend_mean, season_series):
                self.__last_date = last_date
                self.__model = model
                self.__trend_mean = trend_mean
                self.__season_series = season_series

            def forecast(self, date):
                date_to_use = datetime.datetime.combine(date, datetime.time())
                days_since_training = (date_to_use - self.__last_date).days

                if days_since_training <= 0:
                    raise ValueError()

                forecast = self.__model.forecast(days_since_training)
                return forecast[0][-1] + self.__trend_mean + self.__season_series.Duration[date_to_use]

        def __init__(self):
            super(SqlDataSource.ArimaForecastEstimator, self).__init__()

            self.__cluster_models = {}
            self.__user_clusters = collections.defaultdict(list)

        def reload(self, console, connection_factory, area, start_date, end_date):
            from rows.analysis import Tasks, SimpleVisit

            cursor = connection_factory().cursor()
            visits = []

            for row in cursor.execute(
                    SqlDataSource.LIST_PAST_VISITS_QUERY_WITH_CHECKOUT_INFO.format(area.code, start_date)) \
                    .fetchall():
                visit_raw_id, \
                user_raw_id, \
                planned_carer_id, \
                planned_start_date_time, \
                planned_end_date_time, \
                planned_duration, \
                original_start_date_time, \
                original_end_date_time, \
                original_duration, \
                real_start_date_time, \
                real_end_date_time, \
                real_duration, \
                check_out_raw_method, \
                raw_tasks, \
                area_code = row

                task_numbers = list(map(int, raw_tasks.split('-')))
                task_numbers.sort()
                tasks = Tasks(task_numbers)
                visits.append(SimpleVisit(id=int(visit_raw_id),
                                          user=int(user_raw_id),
                                          area=area.key,
                                          carer=int(planned_carer_id),
                                          tasks=tasks,
                                          planned_start=planned_start_date_time,
                                          planned_duration=(planned_end_date_time - planned_start_date_time),
                                          original_start=original_start_date_time,
                                          original_duration=(original_end_date_time - original_start_date_time),
                                          real_start=real_start_date_time,
                                          real_duration=(real_end_date_time - real_start_date_time),
                                          checkout_method=int(check_out_raw_method)))

            from rows.clustering import compute_kmeans_clusters, coalesce, find_repeating_component, distance

            import pandas
            import numpy
            import statsmodels.stats.stattools
            import statsmodels.tsa.stattools
            import statsmodels.api
            import statsmodels.tools.sm_exceptions
            import scipy.optimize

            cluster_groups = compute_kmeans_clusters(visits)

            self.__user_clusters = collections.defaultdict(list)
            for cluster_group in cluster_groups:
                for cluster in cluster_group:
                    if cluster.empty or cluster.data_frame().empty:
                        # now even clusters of size 1 are important
                        continue
                    self.__user_clusters[cluster.user].append(cluster)

            def compute_prediction_model(cluster):
                data_frame = cluster.data_frame()
                if data_frame.empty:
                    raise ValueError()

                if data_frame.Duration.count() < 64:
                    # we are predicting for 2 weeks, so any smaller value does not make sense
                    # especially the number of observations cannot be 12 to avoid division by 0 in the AICC formula
                    return SqlDataSource.ArimaForecastEstimator.MeanModel(data_frame.Duration.mean())

                start_date_to_use = datetime.datetime.combine(start_date, datetime.time())
                last_past_visit_date = start_date_to_use - datetime.timedelta(days=1)
                data_frame = coalesce(data_frame, None, last_past_visit_date).copy()
                first_index = data_frame.first_valid_index()
                last_index = data_frame.last_valid_index()

                correlation_test = statsmodels.stats.stattools.durbin_watson(data_frame.Duration)
                stationary_test = statsmodels.tsa.stattools.adfuller(data_frame.Duration)

                decomposition = statsmodels.api.tsa.seasonal_decompose(data_frame.Duration, model='additive')

                trend = coalesce(
                    pandas.DataFrame(
                        decomposition.trend[first_index:last_index],
                        columns=['Duration']),
                    begin_range=first_index,
                    end_range=last_past_visit_date)
                data_frame.Duration = data_frame.Duration - trend.Duration

                seasonal_component = find_repeating_component(decomposition.seasonal)

                first_index_dt = first_index.to_pydatetime()
                last_index_dt = last_index.to_pydatetime()
                first_index_year = datetime.datetime(year=first_index_dt.year, month=1, day=1)
                last_index_year = datetime.datetime(year=last_index_dt.year, month=12, day=31)

                # generate data range from the first index to the last index
                seasons = int(numpy.ceil((last_index_year - first_index_year).days / len(seasonal_component)))
                seasonal_component_df = pandas.DataFrame(
                    index=pandas.date_range(start=first_index_year, periods=len(seasonal_component) * seasons,
                                            freq='D'),
                    data=numpy.tile(seasonal_component, seasons),
                    columns=['Duration'])

                def seasonal_effect(x, a, b):
                    return a * numpy.asarray(x) + b

                popt, pcov = scipy.optimize.curve_fit(seasonal_effect,
                                                      seasonal_component_df[first_index:last_index].Duration,
                                                      data_frame.Duration)
                season_df = seasonal_component_df.copy()
                season_df.Duration = season_df.Duration * popt[0] + popt[1]
                data_frame.Duration = data_frame.Duration - season_df.Duration
                data_frame = coalesce(data_frame)

                data_frame = coalesce(data_frame, end_range=last_past_visit_date)[:last_past_visit_date]
                arma_config = statsmodels.api.tsa.ARMA(data_frame.Duration, (1, 0), freq='D')
                arma_model = arma_config.fit(disp=0)

                normal_test = scipy.stats.normaltest(arma_model.resid)

                r, q, p = statsmodels.tsa.stattools.acf(arma_model.resid.values, qstat=True, missing='drop')
                data = numpy.c_[range(1, 41), r[1:], q, p]
                ljung_box_df = pandas.DataFrame(data, columns=['lag', "AC", "Q", "Prob(>Q)"])

                return SqlDataSource.ArimaForecastEstimator.ARIMAModel(last_past_visit_date,
                                                                       arma_model,
                                                                       trend.Duration.mean(),
                                                                       season_df)

            with warnings.catch_warnings():
                warnings.simplefilter('ignore', statsmodels.tools.sm_exceptions.ConvergenceWarning)
                self.__cluster_models = {}
                for user, clusters in self.__user_clusters.items():
                    for cluster in clusters:
                        self.__cluster_models[cluster] = compute_prediction_model(cluster)

        @property
        def should_reload(self):
            return True

        def __call__(self, local_visit):
            if local_visit.service_user in self.__user_clusters:
                user_clusters = self.__user_clusters[local_visit.service_user]
                if not user_clusters:
                    raise ValueError()

                simple_visit = rows.analysis.SimpleVisit(id=local_visit.key,
                                                         user=local_visit.service_user,
                                                         tasks=rows.analysis.Tasks(local_visit.tasks),
                                                         original_start=datetime.datetime.combine(local_visit.date, local_visit.time),
                                                         original_duration=local_visit.duration,
                                                         planned_start=datetime.datetime.combine(local_visit.date, local_visit.time),
                                                         planned_duration=local_visit.duration)
                distances = []
                for index in range(len(user_clusters)):
                    cluster = user_clusters[index]
                    centroid_distance = rows.clustering.distance(simple_visit, cluster.centroid())
                    distances.append((cluster, centroid_distance))
                cluster, time_distance = min(distances, key=operator.itemgetter(1))
                if time_distance < 90:
                    # visit is within 90 minutes time distance from the centroid
                    return self.__cluster_models[cluster].forecast(simple_visit.original_start.date())
                return local_visit.duration
            else:
                # logging.warning('Failed to find a cluster for user %s', local_visit.service_user)
                pass
            return local_visit.duration

    class GlobalPercentileEstimator(IntervalEstimatorBase):

        NAME = 'global_percentile'

        def __init__(self, percentile, min_duration=None):
            super(SqlDataSource.GlobalPercentileEstimator, self).__init__(min_duration=min_duration)
            self.__duration_by_task = {}
            self.__percentile = percentile

        @property
        def should_reload(self):
            return bool(self.__duration_by_task)

        def reload(self, console, connection_factory, area, start_date, end_date):
            self.__duration_by_task.clear()

            super(SqlDataSource.GlobalPercentileEstimator, self) \
                .reload(console, connection_factory, area, start_date, end_date)

        def process(self, key, group):
            durations = [row[3] for row in group]
            durations.sort()

            percentile_duration = get_percentile(self.__percentile, durations)
            if percentile_duration and self.min_duration < percentile_duration:
                self.__duration_by_task[key] = str(percentile_duration)

        def __call__(self, local_visit):
            value = self.__duration_by_task.get(local_visit.tasks, None)
            if value:
                if float(value) > self.min_duration:
                    return value
                return str(int(self.min_duration))
            return super(SqlDataSource.GlobalPercentileEstimator, self).__call__(local_visit)

    class PastDurationEstimator:

        NAME = 'past'

        def __init__(self):
            self.__duration_by_visit = {}

        def __call__(self, local_visit):
            return self.__duration_by_visit.get(local_visit.key, local_visit.duration)

        @property
        def should_reload(self):
            return True

        def reload(self, console, connection_factory, area, start_date, end_date):
            self.__duration_by_visit.clear()

            with console.create_progress_bar(leave=False, unit='') as bar:
                bar.set_description_str('Connecting to the database...')
                cursor = connection_factory().cursor()

                bar.set_description_str('Estimating visit duration')
                for row in cursor.execute(
                        SqlDataSource.PAST_VISIT_DURATION.format(area.code, start_date, end_date)).fetchall():
                    visit_id, duration = row
                    self.__duration_by_visit[visit_id] = str(duration)

    class PlannedDurationEstimator:

        NAME = 'fixed'

        def __init__(self):
            pass

        def reload(self, console, connection_factory, area, begin, end):
            pass

        def reload_if(self, console, connection_factory):
            pass

        def __call__(self, local_visit):
            return local_visit.duration

    class ScheduleEventCollector:
        EVENT_TYPE_CONTRACT = 'contract'
        EVENT_TYPE_WORK = 'work'
        EVENT_TYPE_ASSUMED = 'assumed'
        CARER_TYPE_MOVED = 'moved'
        CARER_TYPE_NORMAL = 'normal'

        __COLUMNS = ['carer', 'carer_type', 'day', 'begin', 'end', 'event type']

        def __init__(self):
            self.__data = []

        def extend(self, events, event_type, date, carer_id, carer_type):
            for event in events:
                self.__data.append([carer_id, carer_type, date, event.begin, event.end, event_type])

        def save(self, path):
            frame = pandas.DataFrame(data=self.__data, columns=self.__COLUMNS)
            frame.to_csv(path)

    class Scheduler:

        def __init__(self, data_source):
            self.__data_source = data_source
            self.__area_by_carer = None
            self.__intervals_by_carer = None
            self.__schedules_by_carer = None
            self.__begin_date = None
            self.__end_date = None

        def initialize(self, begin_date, end_date):
            self.__area_by_carer = self.__data_source.get_carers_areas()
            self.__intervals_by_carer = self.__data_source.get_carers_intervals(begin_date, end_date)
            self.__schedules_by_carer = {}
            self.__begin_date = begin_date
            self.__end_date = end_date

        def get_area(self, carer_id):
            return self.__area_by_carer.get(carer_id, None)

        def get_working_hours(self, carer_id, date):
            if carer_id in self.__intervals_by_carer:
                return [event for event in self.__intervals_by_carer[carer_id] if event.begin.date() == date]
            schedule_opt = self.__get_schedule(carer_id)
            if schedule_opt:
                return schedule_opt.extrapolate(date)
            return list()

        def __get_schedule(self, carer_id):
            if carer_id in self.__schedules_by_carer:
                return self.__schedules_by_carer[carer_id]
            schedule = self.__data_source.get_schedule(carer_id, self.__begin_date, self.__end_date)
            self.__schedules_by_carer[carer_id] = schedule
            return schedule

        @staticmethod
        def adjust_work(actual_work, working_hours):
            actual_work_to_use = list(actual_work)
            actual_work_to_use.sort(key=lambda event: event.begin)
            working_hours_to_use = list(working_hours)
            working_hours_to_use.sort(key=lambda event: event.begin)

            filled_gaps = []
            event_it = iter(actual_work_to_use)
            current_event = next(event_it)
            for next_event in event_it:
                can_combine = False
                for slot in working_hours_to_use:
                    if current_event.end >= slot.begin and next_event.begin <= slot.end:
                        can_combine = True
                        break
                if can_combine:
                    current_event = AbsoluteEvent(begin=current_event.begin, end=next_event.end)
                else:
                    filled_gaps.append(current_event)
                    current_event = next_event
            filled_gaps.append(current_event)

            def get_cum_duration(events):
                duration = datetime.timedelta()
                for event in events:
                    duration += event.duration
                return duration

            def extend(actual_event, possible_event, max_time):
                result = actual_event
                rem_time = max_time
                if actual_event.begin > possible_event.begin:
                    offset = actual_event.begin - possible_event.begin
                    if offset > rem_time:
                        return AbsoluteEvent(begin=actual_event.begin - rem_time,
                                             end=actual_event.end), datetime.timedelta()
                    result = AbsoluteEvent(begin=possible_event.begin, end=actual_event.end)
                    rem_time -= offset
                if actual_event.end < possible_event.end:
                    offset = possible_event.end - actual_event.end
                    if offset > rem_time:
                        return AbsoluteEvent(begin=result.begin, end=result.end + rem_time), datetime.timedelta()
                    result = AbsoluteEvent(begin=result.begin, end=possible_event.end)
                    rem_time -= offset
                return result, rem_time

            actual_work_to_use = filled_gaps
            time_budget = get_cum_duration(working_hours_to_use) - get_cum_duration(actual_work_to_use)
            while time_budget.total_seconds() > 0:
                updated_work_to_use = []
                for actual_event in actual_work_to_use:
                    updated_event = actual_event
                    if time_budget.total_seconds() > 0:
                        containing_event = next((event for event in working_hours_to_use
                                                 if event.contains(actual_event) and event != actual_event), None)
                        if containing_event:
                            updated_event, time_budget = extend(actual_event, containing_event, time_budget)
                    updated_work_to_use.append(updated_event)

                if updated_work_to_use == actual_work_to_use:
                    # test how much it will affect - 2018-06-29
                    break

                    # no expansion possible containment of time intervals, try overlaps
                    updated_work_to_use = []
                    for actual_event in actual_work_to_use:
                        updated_event = actual_event
                        if time_budget.total_seconds() > 0:
                            overlapping_event = next((event for event in working_hours_to_use
                                                      if event.overlaps(actual_event) and event != actual_event), None)
                            if overlapping_event:
                                updated_event, time_budget = extend(actual_event, overlapping_event, time_budget)
                        updated_work_to_use.append(updated_event)

                if updated_work_to_use == actual_work_to_use:
                    break

                actual_work_to_use = SqlDataSource.Scheduler.merge_overlapping(updated_work_to_use)
                time_budget = get_cum_duration(working_hours_to_use) - get_cum_duration(actual_work_to_use)
            return actual_work_to_use

        @staticmethod
        def join_within_threshold(events, threshold):
            if not events:
                return events

            aggregated_events = []
            event_it = iter(SqlDataSource.Scheduler.merge_overlapping(events))
            last_event = next(event_it)
            for event in event_it:
                if (event.begin - last_event.end) <= threshold:
                    last_event = AbsoluteEvent(begin=last_event.begin, end=event.end)
                else:
                    aggregated_events.append(last_event)
                    last_event = event
            aggregated_events.append(last_event)
            return aggregated_events

        @staticmethod
        def merge_overlapping(events):
            result = list()
            if events:
                loc_event_it = iter(events)
                last_event = next(loc_event_it)
                for event in loc_event_it:
                    if event.begin <= last_event.end:
                        last_event = AbsoluteEvent(begin=last_event.begin,
                                                   end=last_event.end
                                                   if last_event.end > event.end else event.end)
                    else:
                        result.append(last_event)
                        last_event = event
                result.append(last_event)
            return result

        @staticmethod
        def patch_outlieres(events, max_outlier_duration, max_window):
            result = list()
            if events:
                events_it = iter(events)
                last_event = next(events_it)
                for event in events_it:
                    if (last_event.duration > max_outlier_duration and event.duration > max_outlier_duration) \
                            or (event.begin - last_event.end) > max_window:
                        result.append(last_event)
                        last_event = event
                        continue
                    last_event = AbsoluteEvent(begin=last_event.begin, end=event.end)
                result.append(last_event)
            return result

    class Schedule:
        def __init__(self, schedule):
            self.__weeks = 0
            self.__data = []
            for week, day, begin, end in schedule:
                if len(self.__data) < week:
                    for _ in range(week - len(self.__data)):
                        self.__data.append([list() for _ in range(7)])
                self.__data[week - 1][day - 1].append((begin, end))
            self.__weeks = len(self.__data)

        def extrapolate(self, date):
            if self.__data:
                year, week_number, week_day = date.isocalendar()
                events = self.__data[week_number % self.__weeks][week_day - 1]
                return [AbsoluteEvent(begin=datetime.datetime.combine(date, begin_time),
                                      end=datetime.datetime.combine(date, end_time)) for begin_time, end_time in events]
            return list()

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
        duration_estimator.reload(self.__console, self.__get_connection, area, begin_date, end_date)

        end_date_plus_one = datetime.datetime.combine(end_date, datetime.time()) + datetime.timedelta(days=1)

        carer_counts = {}
        for row in self.__get_connection().cursor() \
                .execute(
            SqlDataSource.LIST_MULTIPLE_CARER_VISITS_QUERY.format(begin_date, end_date_plus_one.date())).fetchall():
            visit_id, carer_count = row
            carer_counts[visit_id] = carer_count

        visits = []
        for row in self.__get_connection().cursor().execute(SqlDataSource.LIST_VISITS_QUERY.format(
                begin_date, end_date, area.key)).fetchall():
            visit_key, service_user_id, visit_date, visit_time, visit_duration, raw_tasks = row
            tasks = self.__parse_tasks(raw_tasks)

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            visits.append(Problem.LocalVisit(key=visit_key,
                                             service_user=service_user_id,
                                             date=visit_date,
                                             time=visit_time,
                                             duration=datetime.timedelta(minutes=visit_duration),
                                             tasks=tasks,
                                             carer_count=carer_count))

        visits_by_service_user = {}
        time_change = []
        for visit in visits:
            original_duration = visit.duration
            visit.duration = duration_estimator(visit)
            time_change.append((visit.duration - original_duration).total_seconds())

            if visit.service_user in visits_by_service_user:
                visits_by_service_user[visit.service_user].append(visit)
            else:
                visits_by_service_user[visit.service_user] = [visit]

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

    def get_historical_visits(self, area) -> typing.List[HistoricalVisit]:
        visits: typing.Dict[int, typing.List[HistoricalVisit]] = collections.defaultdict(list)
        for row in self.__get_connection().cursor().execute(SqlDataSource.VISITS_HISTORY.format(area.code)).fetchall():
            visit_id, planned_checkin, planned_checkout, real_checkin, real_checkout, service_user_id, raw_tasks = row
            tasks = self.__parse_tasks(raw_tasks)
            visits[visit_id].append(HistoricalVisit(visit=visit_id,
                                                    service_user=service_user_id,
                                                    tasks=tasks,
                                                    planned_check_in=planned_checkin,
                                                    planned_check_out=planned_checkout,
                                                    real_check_in=real_checkin,
                                                    real_check_out=real_checkout))

        def mean_duration(visits: typing.List[HistoricalVisit],
                          get_duration: typing.Callable[[HistoricalVisit], datetime.timedelta]) -> datetime.timedelta:
            durations = []
            for visit in visits:
                duration = get_duration(visit)
                if duration > datetime.timedelta():
                    durations.append(duration)
            if len(durations) > 0:
                average_total_seconds = sum(duration.total_seconds() for duration in durations) / len(durations)
                return datetime.timedelta(seconds=average_total_seconds)
            return datetime.timedelta()

        def real_duration(visit: HistoricalVisit) -> datetime.timedelta:
            return visit.real_check_out - visit.real_check_in

        def planned_duration(visit: HistoricalVisit) -> datetime.timedelta:
            return visit.planned_check_out - visit.planned_check_in

        unique_visits = []
        for visit_sequence in visits.values():
            if len(visit_sequence) > 1:
                authentic_sequence = [visit for visit in visit_sequence
                                      if visit.real_check_in != visit.planned_check_in and visit.real_check_out != visit.planned_check_out]
                if authentic_sequence:
                    real_duration_value = mean_duration(authentic_sequence, real_duration)
                else:
                    real_duration_value = mean_duration(visit_sequence, real_duration)
            else:
                real_duration_value = mean_duration(visit_sequence, real_duration)
            planned_duration_value = mean_duration(visit_sequence, planned_duration)
            representative = visit_sequence[0]
            unique_visits.append(PastVisit(visit=representative.visit,
                                           service_user=representative.service_user,
                                           tasks=representative.tasks,
                                           planned_check_in=representative.planned_check_in,
                                           planned_check_out=representative.planned_check_out,
                                           planned_duration=planned_duration_value,
                                           real_check_in=representative.real_check_in,
                                           real_check_out=representative.real_check_out,
                                           real_duration=real_duration_value,
                                           carer_count=len(visit_sequence)))
        return unique_visits

    def get_carers_areas(self):
        area_by_carer = {}
        for row in self.__get_connection().cursor().execute(SqlDataSource.LIST_CARER_AOM_QUERY):
            carer_id, position_hours, aom_id, area_code = row
            area_by_carer[carer_id] = Area(key=aom_id, code=area_code)
        return area_by_carer

    def get_carers_intervals(self, begin_date, end_date):
        intervals_by_carer = collections.defaultdict(list)
        for row in self.__get_connection().cursor().execute(SqlDataSource.CARER_INTERVAL_QUERY.format(
                begin_date, end_date)).fetchall():
            carer_id, begin, end = row
            intervals_by_carer[carer_id].append(AbsoluteEvent(begin=begin, end=end))
        return intervals_by_carer

    def get_schedule(self, carer_id, begin_date, end_date):
        bundle = []
        for row in self.__get_connection().cursor().execute(SqlDataSource.CARER_SCHEDULE_QUERY.format(carer_id,
                                                                                                      begin_date,
                                                                                                      end_date)):
            bundle.append(row)
        return SqlDataSource.Schedule(bundle)

    def get_carers(self, area, begin_date, end_date):
        events_by_carer = collections.defaultdict(list)
        skills_by_carer = collections.defaultdict(list)
        mobile_carers = set()
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.CARER_WORKING_HOURS.format(begin_date, end_date, area.key)).fetchall():
            carer_id, mobile, begin_time, end_time = row
            events_by_carer[carer_id].append(AbsoluteEvent(begin=begin_time, end=end_time))
            if mobile == 1:
                mobile_carers.add(carer_id)
        carer_shifts = []

        for row in self.__get_connection().cursor().execute(SqlDataSource.LIST_CARER_SKILLS.format(area.key, begin_date, end_date)):
            carer_id, raw_skills = row
            skills_by_carer[carer_id] = self.__parse_tasks(raw_skills)

        for carer in events_by_carer:
            events_by_carer[carer].sort(key=lambda event: event.begin.date())

        for carer_id in events_by_carer.keys():
            diaries = [Diary(date=date, events=list(events), schedule_pattern=None)
                       for date, events
                       in itertools.groupby(events_by_carer[carer_id], key=lambda event: event.begin.date())]
            carer_mobility = Carer.CAR_MOBILITY_TYPE if carer_id in mobile_carers else Carer.FOOT_MOBILITY_TYPE
            carer_shift = Problem.CarerShift(carer=Carer(sap_number=str(carer_id),
                                                         mobility=carer_mobility,
                                                         skills=skills_by_carer[carer_id]), diaries=diaries)
            carer_shifts.append(carer_shift)
        return carer_shifts

    def get_carers_windows(self, carer_id, begin_date, end_date):
        windows = []
        for row in self.__get_connection() \
                .cursor() \
                .execute(SqlDataSource.SINGLE_CARER_INTERVAL_QUERY.format(begin_date, end_date, carer_id)):
            _carer_id, begin_date_time, end_date_time = row
            windows.append(AbsoluteEvent(begin=begin_date_time, end=end_date_time))
        return windows

    def __parse_tasks(self, value: str) -> typing.List[int]:
        tasks = list(map(int, value.split(';')))
        tasks.sort()
        return tasks

    def get_visits_carers_from_schedule(self, area, begin_date, end_date, duration_estimator):
        duration_estimator.reload(self.__console, self.__get_connection, area, begin_date, end_date)

        end_date_plus_one = datetime.datetime.combine(end_date, datetime.time()) + datetime.timedelta(days=1)

        carer_counts = {}
        for row in self.__get_connection().cursor().execute(SqlDataSource.LIST_MULTIPLE_CARER_VISITS_QUERY.format(
                begin_date,
                end_date_plus_one.date())).fetchall():
            visit_id, carer_count = row
            carer_counts[visit_id] = carer_count

        data_set = self.__get_connection().cursor().execute(SqlDataSource.SCHEDULE_QUERY.format(area.code,
                                                                                                begin_date,
                                                                                                end_date)).fetchall()
        # get visits
        mobile_carers = set()
        marked_visit_ids = set()
        raw_visits = []
        for row in data_set:
            visit_key, start_date_time, end_date_time, carer_id, is_mobile, service_user_id, raw_tasks = row

            if visit_key in marked_visit_ids:
                continue
            marked_visit_ids.add(visit_key)

            if is_mobile == 1:
                mobile_carers.add(carer_id)

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            tasks = self.__parse_tasks(raw_tasks)

            # visit duration is not predicted
            raw_visits.append(Problem.LocalVisit(key=visit_key,
                                                 service_user=service_user_id,
                                                 date=start_date_time.date(),
                                                 time=start_date_time.time(),
                                                 duration=end_date_time - start_date_time,
                                                 tasks=tasks,
                                                 carer_count=carer_count))

        distinct_visits = self.__remove_duplicates(raw_visits)
        visits_by_service_user = collections.defaultdict(list)

        time_change = []
        for visit in distinct_visits:
            suggested_duration = duration_estimator(visit)

            if isinstance(suggested_duration, str):
                if not suggested_duration.isdigit():
                    # raise ValueError('Failed to estimate duration of the visit for user %s'.format(visit.service_user))
                    suggested_duration = visit.duration
                else:
                    suggested_duration = datetime.timedelta(seconds=int(suggested_duration))
            elif isinstance(suggested_duration, numpy.float):
                if math.isnan(suggested_duration) or numpy.isnan(suggested_duration):
                    raise ValueError('Failed to estimate duration of the visit for user %s'.format(visit.service_user))
                suggested_duration = datetime.timedelta(seconds=int(suggested_duration.item()))
            elif not isinstance(suggested_duration, datetime.timedelta):
                raise ValueError('Failed to estimate duration of the visit for user %s'.format(visit.service_user))

            time_change.append((suggested_duration - visit.duration).total_seconds())
            visit.duration = suggested_duration
            visits_by_service_user[visit.service_user].append(visit)

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

        visits = [Problem.LocalVisits(service_user=str(service_user_id), visits=visits)
                  for service_user_id, visits in visits_by_service_user.items()]

        # get carers
        raw_work_events_by_carer = collections.defaultdict(list)
        tasks_by_carer = collections.defaultdict(set)
        for row in data_set:
            _visit_key, start_date_time, end_date_time, carer_id, _is_mobile, _service_user_id, _raw_tasks = row
            raw_work_events_by_carer[carer_id].append((start_date_time, end_date_time))
            for task_number in self.__parse_tasks(_raw_tasks):
                tasks_by_carer[carer_id].add(task_number)

        work_events_by_carer = {}
        for carer in raw_work_events_by_carer.keys():
            events = raw_work_events_by_carer[carer]
            if not events:
                continue
            events.sort(key=lambda pair: pair[0])

            aggregated_events = []
            events_it = iter(events)
            interval_begin, interval_end = next(events_it)
            for current_begin, current_end in events_it:
                if interval_end == current_begin:
                    interval_end = current_end
                else:
                    aggregated_events.append(AbsoluteEvent(begin=interval_begin, end=interval_end))
                    interval_begin, interval_end = current_begin, current_end
            aggregated_events.append(AbsoluteEvent(begin=interval_begin, end=interval_end))
            work_events_by_carer[carer] = aggregated_events

        scheduler = SqlDataSource.Scheduler(self)
        scheduler.initialize(begin_date, end_date)

        intervals_by_carer = collections.defaultdict(list)
        for row in self.__get_connection().cursor().execute(SqlDataSource.CARER_INTERVAL_QUERY.format(
                begin_date, end_date)).fetchall():
            carer_id, begin, end = row
            intervals_by_carer[carer_id].append(AbsoluteEvent(begin=begin, end=end))

        schedule_event_collector = SqlDataSource.ScheduleEventCollector()

        carer_shifts = []
        for carer_id in work_events_by_carer.keys():
            diaries = []
            dates_to_use = list(set((event.begin.date() for event in work_events_by_carer[carer_id])))
            dates_to_use.sort()
            carer_area = scheduler.get_area(carer_id)
            if carer_area == area:
                for current_date in dates_to_use:
                    # carer is assigned to area
                    actual_work = [event for event in work_events_by_carer[carer_id]
                                   if event.begin.date() == current_date]

                    schedule_event_collector.extend(actual_work,
                                                    SqlDataSource.ScheduleEventCollector.EVENT_TYPE_WORK,
                                                    current_date,
                                                    carer_id,
                                                    SqlDataSource.ScheduleEventCollector.CARER_TYPE_NORMAL)

                    working_hours = scheduler.get_working_hours(carer_id, current_date)
                    if working_hours:
                        schedule_event_collector.extend(working_hours,
                                                        SqlDataSource.ScheduleEventCollector.EVENT_TYPE_CONTRACT,
                                                        current_date,
                                                        carer_id,
                                                        SqlDataSource.ScheduleEventCollector.CARER_TYPE_NORMAL)

                        work_to_use = scheduler.adjust_work(actual_work, working_hours)
                        work_to_use = scheduler.join_within_threshold(work_to_use, datetime.timedelta(minutes=15))
                        work_to_use = scheduler.patch_outlieres(work_to_use,
                                                                max_outlier_duration=datetime.timedelta(minutes=30),
                                                                max_window=datetime.timedelta(minutes=45))

                        schedule_event_collector.extend(work_to_use,
                                                        SqlDataSource.ScheduleEventCollector.EVENT_TYPE_ASSUMED,
                                                        current_date,
                                                        carer_id,
                                                        SqlDataSource.ScheduleEventCollector.CARER_TYPE_NORMAL)

                        diary = Diary(date=current_date,
                                      events=work_to_use,
                                      shift_type=Diary.STANDARD_SHIFT_TYPE)
                    else:
                        work_to_use = scheduler.join_within_threshold(actual_work, datetime.timedelta(minutes=45))
                        work_to_use = scheduler.patch_outlieres(work_to_use,
                                                                max_outlier_duration=datetime.timedelta(minutes=30),
                                                                max_window=datetime.timedelta(minutes=45))

                        schedule_event_collector.extend(work_to_use,
                                                        SqlDataSource.ScheduleEventCollector.EVENT_TYPE_ASSUMED,
                                                        current_date,
                                                        carer_id,
                                                        SqlDataSource.ScheduleEventCollector.CARER_TYPE_NORMAL)

                        diary = Diary(date=current_date,
                                      events=work_to_use,
                                      shift_type=Diary.EXTRA_SHIFT_TYPE)
                    diaries.append(diary)
            else:
                # carer is used conditionally
                for current_date in dates_to_use:
                    actual_work = [event for event in work_events_by_carer[carer_id]
                                   if event.begin.date() == current_date]
                    working_hours = scheduler.get_working_hours(carer_id, current_date)

                    schedule_event_collector.extend(actual_work,
                                                    SqlDataSource.ScheduleEventCollector.EVENT_TYPE_WORK,
                                                    current_date,
                                                    carer_id,
                                                    SqlDataSource.ScheduleEventCollector.CARER_TYPE_MOVED)

                    if working_hours:
                        schedule_event_collector.extend(working_hours,
                                                        SqlDataSource.ScheduleEventCollector.EVENT_TYPE_CONTRACT,
                                                        current_date,
                                                        carer_id,
                                                        SqlDataSource.ScheduleEventCollector.CARER_TYPE_MOVED)

                    if len(actual_work) == 1:
                        is_external = not working_hours

                        schedule_event_collector.extend(actual_work,
                                                        SqlDataSource.ScheduleEventCollector.EVENT_TYPE_ASSUMED,
                                                        current_date,
                                                        carer_id,
                                                        SqlDataSource.ScheduleEventCollector.CARER_TYPE_MOVED)

                        diary = Diary(date=current_date,
                                      events=actual_work,
                                      shift_type=Diary.EXTERNAL_SHIFT_TYPE if is_external
                                      else Diary.EXTERNAL_SHIFT_TYPE)
                    else:
                        is_external = not working_hours
                        if is_external:
                            work_to_use = scheduler.join_within_threshold(actual_work, datetime.timedelta(minutes=45))
                            work_to_use = scheduler.patch_outlieres(work_to_use,
                                                                    max_outlier_duration=datetime.timedelta(minutes=30),
                                                                    max_window=datetime.timedelta(minutes=45))

                            schedule_event_collector.extend(work_to_use,
                                                            SqlDataSource.ScheduleEventCollector.EVENT_TYPE_ASSUMED,
                                                            current_date,
                                                            carer_id,
                                                            SqlDataSource.ScheduleEventCollector.CARER_TYPE_MOVED)

                            diary = Diary(date=current_date,
                                          events=work_to_use,
                                          shift_type=Diary.EXTERNAL_SHIFT_TYPE)
                        else:
                            schedule_event_collector.extend(working_hours,
                                                            SqlDataSource.ScheduleEventCollector.EVENT_TYPE_CONTRACT,
                                                            current_date,
                                                            carer_id,
                                                            SqlDataSource.ScheduleEventCollector.CARER_TYPE_MOVED)

                            work_to_use = scheduler.adjust_work(actual_work, working_hours)
                            work_to_use = scheduler.join_within_threshold(work_to_use, datetime.timedelta(minutes=15))
                            work_to_use = scheduler.patch_outlieres(work_to_use,
                                                                    max_outlier_duration=datetime.timedelta(minutes=30),
                                                                    max_window=datetime.timedelta(minutes=45))

                            schedule_event_collector.extend(work_to_use,
                                                            SqlDataSource.ScheduleEventCollector.EVENT_TYPE_ASSUMED,
                                                            current_date,
                                                            carer_id,
                                                            SqlDataSource.ScheduleEventCollector.CARER_TYPE_MOVED)

                            diary = Diary(date=current_date,
                                          events=work_to_use,
                                          shift_type=Diary.EXTRA_SHIFT_TYPE)
                    diaries.append(diary)
            carer_mobility = Carer.CAR_MOBILITY_TYPE if carer_id in mobile_carers else Carer.FOOT_MOBILITY_TYPE
            skills = list(tasks_by_carer[carer_id])
            skills.sort()
            carer_shifts.append(Problem.CarerShift(carer=Carer(sap_number=str(carer_id),
                                                               mobility=carer_mobility,
                                                               skills=skills),
                                                   diaries=diaries))
        # schedule_event_collector.save('shifts3.csv')
        return visits, carer_shifts

    def get_service_users(self, area, begin_date, end_date):
        location_by_service_user = {}
        address_by_service_user = {}
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.LIST_SERVICE_USER_QUERY.format(begin_date, end_date, area.key)).fetchall():
            service_user, raw_address = row
            if service_user not in location_by_service_user:
                address = Address.parse(raw_address)
                location = self.__location_finder.find(service_user, address)
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
            service_users.append(ServiceUser(key=str(service_user_id),
                                             location=location,
                                             address=address_by_service_user[service_user_id]))
        return service_users

    def get_past_schedule(self, area, schedule_date):
        visits = []
        for row in self.__get_connection().cursor().execute(
                SqlDataSource.PLANNED_SCHEDULE_QUERY.format(area.code, schedule_date, schedule_date)) \
                .fetchall():
            visit_id, carer_id, check_in, check_out, date, planned_time, real_time, planned_duration, read_duration, \
            raw_address, service_user, carer_count = row
            visits.append(PastVisit(
                cancelled=False,
                carer=Carer(sap_number=str(carer_id)),
                visit=Visit(
                    key=visit_id,
                    service_user=str(service_user),
                    address=Address.parse(raw_address),
                    date=date,
                    time=planned_time,
                    duration=str(planned_duration),
                    carer_count=carer_count
                ),
                date=date,
                time=planned_time,
                duration=str(planned_duration),
                check_in=check_in,
                check_out=check_out
            ))
        return Schedule(visits=visits, metadata=Metadata(area=area, begin=schedule_date, end=schedule_date))

    def reload(self):
        self.__get_connection_string()

    @staticmethod
    def validate_resource_estimator(name):
        if name != SqlDataSource.USED_RESOURCE_ESTIMATOR_NAME and \
                name != SqlDataSource.PLANNED_RESOURCE_ESTIMATOR_NAME:
            return "Name '{0}' does not match any resource estimator." \
                   " Please use a valid name, for example: {1} or {2} instead." \
                .format(name,
                        SqlDataSource.USED_RESOURCE_ESTIMATOR_NAME,
                        SqlDataSource.PLANNED_RESOURCE_ESTIMATOR_NAME)
        return None

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
        return self.__get_connection()

    def __exit__(self, exc_type, exc_value, traceback):
        if self.__connection:
            self.__connection.close()
            del self.__connection
            self.__connection = None

    def __remove_duplicates(self, visits):
        user_index: typing.Dict[str, typing.Dict[datetime.date, typing.Dict[datetime.time, Problem.LocalVisit]]] = dict()
        for visit in visits:
            if visit.service_user not in user_index:
                user_index[visit.service_user] = dict()
            if visit.date not in user_index[visit.service_user]:
                user_index[visit.service_user][visit.date] = dict()

            user_date_slot = user_index[visit.service_user][visit.date]
            if visit.time in user_date_slot:
                previous_visit = user_date_slot[visit.time]
                previous_tasks = set(previous_visit.tasks)
                current_tasks = set(visit.tasks)
                if current_tasks.issubset(previous_tasks):
                    user_date_slot[visit.time] = visit
                elif not current_tasks.issubset(previous_tasks):
                    pass
                    # fixme: ignore error
                    # raise ValueError(
                    #     'Two visits happen at the same time {0} and both contain different tasks {1} vs {2}'
                    #         .format(visit.time, previous_tasks, current_tasks))
            else:
                user_date_slot[visit.time] = visit

        results = []
        for user in user_index:
            for date in user_index[user]:
                results.extend(user_index[user][date].values())
        return results

    def __load_credentials(self):
        path = pathlib.Path(real_path(self.__settings.database_credentials_path))
        try:
            with path.open() as file_stream:
                return file_stream.read().strip()
        except FileNotFoundError as ex:
            raise RuntimeError(
                "Failed to open the the file '{0}' which is expected to store the database credentials."
                " Create the file in the specified location and try again.".format(path), ex)

    def __build_connection_string(self):
        config = {'Driver': '{' + self.__settings.database_driver + '}',
                  'Server': self.__settings.database_server,
                  'Database': self.__settings.database_name,
                  'UID': self.__settings.database_user,
                  'PWD': self.__load_credentials(),
                  'Encrypt': 'yes',
                  'Connection Timeout': 5,
                  'TrustServerCertificate': 'yes'}
        return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])
