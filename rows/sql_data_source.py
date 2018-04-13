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
    PLANNED_RESOURCE_ESTIMATOR_NAME = 'planned'

    USED_RESOURCE_ESTIMATOR_NAME = 'used'

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
  LEFT OUTER JOIN dbo.UserVisits user_visits
  ON window_visits.visit_id = user_visits.visit_id
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
visits.service_user_id as 'service_user_id',
visits.tasks as 'tasks' FROM (
    SELECT visit_task.visit_id,
      MIN(visit_task.service_user_id) as 'service_user_id',
      STRING_AGG(visit_task.task, ',') WITHIN GROUP (ORDER BY visit_task.task) as 'tasks'
    FROM (
      SELECT visit_window.visit_id as 'visit_id',
        MIN(visit_window.service_user_id) as 'service_user_id',
        CONVERT(int, visit_window.task_no) as 'task'
      FROM dbo.ListVisitsWithinWindow visit_window
      WHERE visit_window.aom_code = {0} AND visit_window.visit_date BETWEEN '{1}' AND '{2}'
      GROUP BY visit_window.visit_id, visit_window.task_no
    ) visit_task GROUP BY visit_task.visit_id
  ) visits LEFT OUTER JOIN dbo.ListCarerVisits carer_visits
  ON visits.visit_id = carer_visits.VisitID
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

    CARER_WORKING_HOURS = """SELECT CarerId, StartDateTime, EndDateTime
FROM dbo.ListCarerIntervals
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

                actual_work_to_use = merge_overlapping(updated_work_to_use)
                time_budget = get_cum_duration(working_hours_to_use) - get_cum_duration(actual_work_to_use)
            return actual_work_to_use

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
            year, week_number, week_day = date.isocalendar()
            events = self.__data[week_number % self.__weeks][week_day - 1]
            return [AbsoluteEvent(begin=datetime.datetime.combine(date, begin_time),
                                  end=datetime.datetime.combine(date, end_time)) for begin_time, end_time in events]

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

        visits = []
        for row in self.__get_connection().cursor().execute(SqlDataSource.LIST_VISITS_QUERY.format(
                begin_date, end_date, area.key)).fetchall():
            visit_key, service_user_id, visit_date, visit_time, visit_duration, tasks = row

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            visits.append(Problem.LocalVisit(key=visit_key,
                                             service_user=service_user_id,
                                             date=visit_date,
                                             time=visit_time,
                                             duration=str(visit_duration * 60),  # convert minutes to seconds
                                             tasks=tasks,
                                             carer_count=carer_count))

        visits_by_service_user = {}
        time_change = []
        for visit in visits:
            original_duration = int(float(visit.duration))
            visit.duration = duration_estimator(visit)
            duration_to_use = int(float(visit.duration))
            time_change.append(duration_to_use - original_duration)

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

    def get_carers_windows(self, carer_id, begin_date, end_date):
        windows = []
        for row in self.__get_connection() \
                .cursor() \
                .execute(SqlDataSource.SINGLE_CARER_INTERVAL_QUERY.format(begin_date, end_date, carer_id)):
            _carer_id, begin_date_time, end_date_time = row
            windows.append(AbsoluteEvent(begin=begin_date_time, end=end_date_time))
        return windows

    def get_visits_carers_from_schedule(self, area, begin_date, end_date, duration_estimator):
        duration_estimator.reload(self.__console, self.__get_connection)

        carer_counts = {}
        for row in self.__get_connection().cursor().execute(SqlDataSource.LIST_MULTIPLE_CARER_VISITS_QUERY.format(
                begin_date,
                end_date)).fetchall():
            visit_id, carer_count = row
            carer_counts[visit_id] = carer_count

        data_set = self.__get_connection().cursor().execute(SqlDataSource.SCHEDULE_QUERY.format(area.key,
                                                                                                begin_date,
                                                                                                end_date)).fetchall()
        # get visits
        raw_visits = []
        for row in data_set:
            visit_key, start_date_time, end_date_time, carer_id, service_user_id, tasks = row

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            # visit duration is not predicted
            raw_visits.append(Problem.LocalVisit(key=visit_key,
                                                 service_user=service_user_id,
                                                 date=start_date_time.date(),
                                                 time=start_date_time.time(),
                                                 # convert minutes to seconds
                                                 duration=str(
                                                     int((end_date_time - start_date_time).total_seconds())),
                                                 tasks=tasks,
                                                 carer_count=carer_count))

        visits_by_service_user = collections.defaultdict(list)

        time_change = []
        for visit in raw_visits:
            original_duration = int(float(visit.duration))
            visit.duration = duration_estimator(visit)
            duration_to_use = int(float(visit.duration))
            time_change.append(duration_to_use - original_duration)

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
        for row in data_set:
            _visit_key, start_date_time, end_date_time, carer_id, _service_user_id, _tasks = row
            raw_work_events_by_carer[carer_id].append((start_date_time, end_date_time))

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

        carer_shifts = []
        for carer_id in work_events_by_carer.keys():
            diaries = None
            carer_area = scheduler.get_area(carer_id)
            if carer_area == area:
                diaries = []
                dates_to_use = list(set((event.begin.date() for event in work_events_by_carer[carer_id])))
                dates_to_use.sort()
                for current_date in dates_to_use:
                    # carer is assigned to area
                    working_hours = scheduler.get_working_hours(carer_id, current_date)
                    if working_hours:
                        actual_work = [event for event in work_events_by_carer[carer_id]
                                       if event.begin.date() == current_date]
                        work_to_use = scheduler.adjust_work(actual_work, working_hours)
                        diaries.append(Diary(date=current_date, events=work_to_use, schedule_pattern=None))
                    else:
                        diaries.append(Diary(date=current_date,
                                             events=[event for event in work_events_by_carer[carer_id]
                                                     if event.begin.date() == current_date],
                                             schedule_pattern=None))
            else:
                # carer is used conditionally
                diaries = [Diary(date=date, events=list(events), schedule_pattern=None)
                           for date, events
                           in itertools.groupby(work_events_by_carer[carer_id],
                                                key=lambda event: event.begin.date())]
            carer_shifts.append(Problem.CarerShift(carer=Carer(sap_number=str(carer_id)), diaries=diaries))
        return visits, carer_shifts

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
            raise RuntimeError(
                "Failed to open the the file '{0}' which is expected to store the database credentials."
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
