"""Test integration with SQL database"""

import unittest
import pyodbc
import pathlib
import collections
import statistics

from rows.model.area import Area


def load_credentials():
    path = pathlib.Path.home().joinpath(pathlib.PurePath('dev/cordia/.dev'))
    if path.exists():
        with path.open() as file_stream:
            return file_stream.read().strip()
    return ""


def build_connection_string():
    config = {'Driver': '{ODBC Driver 13 for SQL Server}',
              'Server': '192.168.56.1',
              'UID': 'dev',
              'Encrypt': 'yes',
              'Database': 'StrathClyde',
              'TrustServerCertificate': 'yes',
              'PWD': load_credentials()}
    return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])


class TestSqlIntegration(unittest.TestCase):
    """Test integration with SQL database"""

    def test_connection(self):
        try:
            connection = pyodbc.connect(build_connection_string())
            cursor = connection.cursor()
            cursor.execute('SELECT aom.aom_id, aom.area_code '
                           'FROM [StrathClyde].[dbo].[ListAom] aom '
                           'ORDER BY aom.area_code')
            areas = [Area(key=row[0], code=row[1]) for row in cursor.fetchall()]
            print(areas)
        except Exception as ex:
            print(ex)

    def test_get_continuity_of_care(self):
        try:
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
            for service_user, frequency_pair in frequency_results.items():
                visits_count = 0
                nominator = 0.0
                for visits, relative_frequency in frequency_pair:
                    visits_count += visits
                    nominator += relative_frequency * visits_count
                if visits_count > 0:
                    average_by_service_user[service_user] = nominator / visits_count
            print('mean={0}, median={1}, stddev={2}'.format(
                round(statistics.mean(average_by_service_user.values()), 4),
                round(statistics.stdev(average_by_service_user.values()), 4),
                round(statistics.median(average_by_service_user.values())), 4))

            standard_average_by_service_user = {}
            for service_user, frequency_pair in frequency_results.items():
                carers = 0
                nominator = 0.0
                for visits, relative_frequency in frequency_pair:
                    carers += 1
                    nominator += relative_frequency
                if visits_count > 0:
                    standard_average_by_service_user[service_user] = nominator / carers
            print('mean={0}, median={1}, stddev={2}'.format(
                round(statistics.mean(standard_average_by_service_user.values()), 4),
                round(statistics.stdev(standard_average_by_service_user.values()), 4),
                round(statistics.median(standard_average_by_service_user.values())), 4))

        except Exception as ex:
            print(ex)

    if __name__ == '__main__':
        unittest.main()
