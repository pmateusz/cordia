"""Test integration with SQL database"""
import datetime
import itertools
import unittest
import pyodbc
import pathlib
import collections
import statistics
import os

import rows.model.area
import rows.console
import rows.settings
import rows.location_finder
import rows.sql_data_source

from rows.model.area import Area
from rows.model.carer import Carer
from rows.model.diary import Diary
from rows.model.event import AbsoluteEvent
from rows.model.problem import Problem


def load_credentials():
    path = pathlib.Path.home().joinpath(pathlib.PurePath('dev/cordia/data/.dev'))
    if path.exists():
        with path.open() as file_stream:
            return file_stream.read().strip()
    return ""


def build_connection_string():
    config = {'Driver': '{ODBC Driver 13 for SQL Server}',
              'Server': 'mae-mps.mecheng.strath.ac.uk',
              'UID': 'dev',
              'Encrypt': 'yes',
              'Database': 'StrathClyde',
              'TrustServerCertificate': 'yes',
              'PWD': load_credentials()}
    return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])


@unittest.skipIf(os.getenv('TRAVIS', 'false') == 'true',
                 'Resource files required to run the test are not available in CI')
class TestSqlIntegration(unittest.TestCase):
    """Test integration with SQL database"""

    def test_connection(self):
        connection = pyodbc.connect(build_connection_string())
        cursor = connection.cursor()
        cursor.execute('SELECT aom.aom_id, aom.area_code '
                       'FROM [StrathClyde].[dbo].[ListAom] aom '
                       'ORDER BY aom.area_code')
        areas = [Area(key=row[0], code=row[1]) for row in cursor.fetchall()]
        self.assertTrue(areas)

    def test_get_continuity_of_care(self):
        connection = pyodbc.connect(build_connection_string())
        cursor = connection.cursor()
        frequency_results = collections.defaultdict(list)
        for row in cursor.execute('''SELECT user_visit.service_user_id as 'user', carer_visit.PlannedCarerID as 'carer',
COUNT(user_visit.visit_id) AS visits_count, MIN(total_visits.total_visits) AS total_visits,
ROUND(CONVERT(float, COUNT(user_visit.visit_id)) / MIN(total_visits.total_visits), 4) as care_continuity
FROM dbo.ListCarerVisits carer_visit
INNER JOIN (SELECT DISTINCT service_user_id, visit_id 
FROM dbo.ListVisitsWithinWindow
WHERE aom_code = 617) user_visit 
ON carer_visit.VisitID = user_visit.visit_id
INNER JOIN (SELECT service_user_id as service_user_id, COUNT(visit_id) as total_visits
FROM (SELECT DISTINCT service_user_id, visit_id 
FROM dbo.ListVisitsWithinWindow
WHERE aom_code = 617) local_visit
GROUP BY service_user_id) total_visits
ON total_visits.service_user_id = user_visit.service_user_id
GROUP BY user_visit.service_user_id, carer_visit.PlannedCarerID, total_visits.service_user_id
ORDER BY user_visit.service_user_id, care_continuity DESC''').fetchall():
            service_user, carer, visits, total_visits, relative_freq = row
            frequency_results[service_user].append((visits, relative_freq))
        # calculate weighted average for every carer
        average_by_service_user = {}
        visits_count = 0
        for service_user, frequency_pair in frequency_results.items():
            nominator = 0.0
            for visits, relative_frequency in frequency_pair:
                visits_count += visits
                nominator += relative_frequency * visits_count
            if visits_count > 0:
                average_by_service_user[service_user] = nominator / visits_count
        self.assertGreater(round(statistics.mean(average_by_service_user.values()), 4), 0.0)
        self.assertGreater(round(statistics.stdev(average_by_service_user.values()), 4), 0.0)
        self.assertGreater(round(statistics.median(average_by_service_user.values()), 4), 0.0)

        standard_average_by_service_user = {}
        for service_user, frequency_pair in frequency_results.items():
            carers = 0
            nominator = 0.0
            for visits, relative_frequency in frequency_pair:
                carers += 1
                nominator += relative_frequency
            if visits_count > 0:
                standard_average_by_service_user[service_user] = nominator / carers
        self.assertGreater(round(statistics.mean(standard_average_by_service_user.values()), 4), 0.0)
        self.assertGreater(round(statistics.stdev(standard_average_by_service_user.values()), 4), 0.0)
        self.assertGreaterEqual(round(statistics.median(standard_average_by_service_user.values()), 4), 0.0)

    def test_global_visit_duration(self):
        settings = rows.settings.Settings()
        console = rows.console.Console()
        location_cache = rows.location_finder.FileSystemCache(settings)
        location_finder = rows.location_finder.RobustLocationFinder(location_cache, timeout=5.0)
        data_source = rows.sql_data_source.SqlDataSource(settings, console, location_finder)

        area = next((a for a in data_source.get_areas() if a.code == 'C050'), None)
        self.assertTrue(area)

        visits = data_source.get_visits(area,
                                        '2/1/2017',
                                        '2/14/2017',
                                        rows.sql_data_source.SqlDataSource.PlannedDurationEstimator())
        self.assertTrue(visits)
        for visit_group in visits:
            for visit in visit_group.visits:
                self.assertGreater(float(visit.duration), 0.0)

    def test_schedule_retrieval(self):
        connection = pyodbc.connect(build_connection_string())
        cursor = connection.cursor()

        begin_interval = '3/30/2017'
        end_interval = '3/31/2017'
        area_code = 'C240'

        carer_counts = {}
        for row in cursor.execute(rows.sql_data_source.SqlDataSource.LIST_MULTIPLE_CARER_VISITS_QUERY.format(
                begin_interval,
                end_interval)).fetchall():
            visit_id, carer_count = row
            carer_counts[visit_id] = carer_count

        cursor.execute("SELECT DISTINCT visits.visit_id,"
                       "carer_visits.PlannedStartDateTime as 'planned_start_time',"
                       "carer_visits.PlannedEndDateTime as 'planned_end_time',"
                       "carer_visits.PlannedCarerID as 'carer_id',"
                       "visits.service_user_id as 'service_user_id',"
                       "visits.tasks as 'tasks'"
                       " FROM ("
                       " SELECT visit_task.visit_id,"
                       " MIN(visit_task.service_user_id) as 'service_user_id',"
                       " STRING_AGG(visit_task.task, ',') WITHIN GROUP (ORDER BY visit_task.task) as 'tasks'"
                       " FROM ("
                       " SELECT visit_window.visit_id as 'visit_id',"
                       " MIN(visit_window.service_user_id) as 'service_user_id',"
                       " CONVERT(int, visit_window.task_no) as 'task'"
                       " FROM dbo.ListVisitsWithinWindow visit_window"
                       " INNER JOIN dbo.ListAom aom"
                       " ON aom.aom_id = visit_window.aom_code"
                       " WHERE aom.area_code = '{0}' AND visit_window.visit_date BETWEEN '{1}' AND '{2}'"
                       " GROUP BY visit_window.visit_id, visit_window.task_no"
                       " ) visit_task"
                       "  GROUP BY visit_task.visit_id"
                       ") visits"
                       " LEFT OUTER JOIN dbo.ListCarerVisits carer_visits"
                       " ON visits.visit_id = carer_visits.VisitID"
                       " WHERE carer_visits.VisitID IS NOT NULL"
                       " ORDER BY carer_visits.PlannedCarerID, planned_start_time".format(area_code,
                                                                                          begin_interval,
                                                                                          end_interval))
        # get visits
        visits = []
        data_set = cursor.fetchall()
        for row in data_set:
            visit_key, start_date_time, end_date_time, carer_id, service_user_id, tasks = row

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            visits.append(Problem.LocalVisit(key=visit_key,
                                             service_user=service_user_id,
                                             date=start_date_time.date(),
                                             time=start_date_time.time(),
                                             # convert minutes to seconds
                                             duration=str(int((end_date_time - start_date_time).total_seconds())),
                                             tasks=tasks,
                                             carer_count=carer_count))

        raw_events_by_carer = collections.defaultdict(list)
        for row in data_set:
            _visit_key, start_date_time, end_date_time, carer_id, _service_user_id, _tasks = row
            raw_events_by_carer[carer_id].append((start_date_time, end_date_time))

        events_by_carer = {}
        for carer in raw_events_by_carer.keys():
            events = raw_events_by_carer[carer]
            events.sort(key=lambda pair: pair[0])
            if not events:
                continue

            aggregated_events = []
            events_it = iter(events)
            interval_begin, interval_end = next(events_it)
            for current_begin, current_end in events_it:
                if interval_end == current_begin:
                    interval_end = current_end
                else:
                    aggregated_events.append((interval_begin, interval_end))
                    interval_begin, interval_end = current_begin, current_end
            aggregated_events.append(AbsoluteEvent(begin=interval_begin, end=interval_end))
            events_by_carer[carer] = aggregated_events

        carer_shifts = []
        for carer_id in events_by_carer.keys():
            diaries = [Diary(date=date, events=list(events), schedule_pattern=None)
                       for date, events
                       in itertools.groupby(events_by_carer[carer_id], key=lambda event: event.begin.date())]
            carer_shift = Problem.CarerShift(carer=Carer(sap_number=str(carer_id)), diaries=diaries)
            carer_shifts.append(carer_shift)

        # get service users

    def test_adjust_schedule(self):
        # can fill overlap inside
        begin = datetime.datetime(2017, 2, 1, 8, 30)
        end = datetime.datetime(2017, 2, 1, 12, 30)
        work = [AbsoluteEvent(begin=begin, end=datetime.datetime.combine(begin.date(), datetime.time(9, 30))),
                AbsoluteEvent(begin=datetime.datetime.combine(begin.date(), datetime.time(10, 0)), end=end)]
        pattern = [AbsoluteEvent(begin=begin, end=end)]
        expected = [AbsoluteEvent(begin=begin, end=end)]
        actual = rows.sql_data_source.SqlDataSource.Scheduler.adjust_work(work, pattern)
        self.assertEqual(actual, expected)

        # can fill partial overlap from above
        work = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 8, 0),
                              end=datetime.datetime.combine(begin.date(), datetime.time(9, 30))),
                AbsoluteEvent(begin=datetime.datetime.combine(begin.date(), datetime.time(10, 0)),
                              end=datetime.datetime(2017, 2, 1, 12, 0))]
        pattern = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 8, 30),
                                 end=datetime.datetime(2017, 2, 1, 12, 30))]
        expected = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 8, 0),
                                  end=datetime.datetime(2017, 2, 1, 12, 0))]
        actual = rows.sql_data_source.SqlDataSource.Scheduler.adjust_work(work, pattern)
        self.assertEqual(actual, expected)

        # can fill partial overlap from below
        work = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 9, 0),
                              end=datetime.datetime.combine(begin.date(), datetime.time(9, 30))),
                AbsoluteEvent(begin=datetime.datetime.combine(begin.date(), datetime.time(10, 0)),
                              end=datetime.datetime(2017, 2, 1, 13, 0))]
        pattern = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 8, 30),
                                 end=datetime.datetime(2017, 2, 1, 12, 30))]
        expected = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 9, 0),
                                  end=datetime.datetime(2017, 2, 1, 13, 0))]
        actual = rows.sql_data_source.SqlDataSource.Scheduler.adjust_work(work, pattern)
        self.assertEqual(actual, expected)

        # does nothing if there is no overlap
        work = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 9, 0),
                              end=datetime.datetime.combine(begin.date(), datetime.time(9, 30))),
                AbsoluteEvent(begin=datetime.datetime.combine(begin.date(), datetime.time(10, 30)),
                              end=datetime.datetime.combine(begin.date(), datetime.time(11, 0)))]
        pattern = [AbsoluteEvent(begin=datetime.datetime(2017, 2, 1, 10, 0),
                                 end=datetime.datetime(2017, 2, 1, 12, 0))]
        expected = None
        actual = rows.sql_data_source.SqlDataSource.Scheduler.adjust_work(work, pattern)
        self.assertEqual(actual, work)
        # does increase if possible from above

        # does increase if possible from below

        # does increase if possible from above and below

        pass

    if __name__ == '__main__':
        unittest.main()
