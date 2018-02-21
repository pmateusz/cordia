import logging
import pathlib

import pyodbc

from rows.model.address import Address
from rows.model.area import Area
from rows.model.problem import Problem
from rows.model.service_user import ServiceUser


class SqlDataSource:
    LIST_AREAS_QUERY = """SELECT aom.aom_id, aom.area_code
FROM [StrathClyde].[dbo].[ListAom] aom
ORDER BY aom.area_code"""

    LIST_VISITS_QUERY = """SELECT MIN(visits.visit_id) as visit_id, visits.service_user_id, visits.visit_date,
visits.requested_visit_time, visits.requested_visit_duration, visits.display_address
FROM dbo.ListVisitsWithinWindow visits
WHERE visits.visit_date BETWEEN '{0}' AND '{1}' AND visits.aom_code = {2}
GROUP BY visits.service_user_id, visits.visit_date, visits.requested_visit_duration,
visits.display_address, visits.requested_visit_time"""

    LIST_MULTIPLE_CARER_VISITS_QUERY = """SELECT visits.VisitID as visit_id, COUNT(visits.VisitID) as carer_count
FROM dbo.ListCarerVisits visits
WHERE visits.PlannedStartDateTime BETWEEN '{0}' AND '{1}'
GROUP BY visits.PlannedStartDateTime, visits.VisitID
HAVING COUNT(visits.VisitID) > 1"""

    def __init__(self, location_finder):
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
        location_by_service_user = {}
        address_by_service_user = {}

        for row in self.__get_connection().cursor().execute(
                SqlDataSource.LIST_VISITS_QUERY.format(begin_date, end_date, area.key)).fetchall():
            visit_key, service_user, visit_date, visit_time, visit_duration, display_address = row

            carer_count = 1
            if visit_key in carer_counts:
                carer_count = carer_counts[visit_key]

            address = Address.parse(display_address)
            local_visit = Problem.LocalVisit(key=visit_key,
                                             date=visit_date,
                                             time=visit_time,
                                             duration=visit_duration,
                                             carer_count=carer_count)
            if service_user in visits_by_service_user:
                visits_by_service_user[service_user].append(local_visit)
            else:
                visits_by_service_user[service_user] = [local_visit]
                print(address)
                location = self.__location_finder.find(address)
                if location is None:
                    logging.error("Failed to find location of the address '%s'", location)
                location_by_service_user[service_user] = location
                address_by_service_user[service_user] = address

        return [Problem.LocalVisits(service_user=ServiceUser(key=service_user), visits=visits)
                for service_user, visits in visits_by_service_user.items()]

    def get_carers(self, area, begin_date, end_date):
        return []

    def get_service_users(self, area, begin_date, end_date):
        return []

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
        path = pathlib.Path.home().joinpath(pathlib.PurePath('dev/cordia/.dev'))
        if path.exists():
            with path.open() as file_stream:
                return file_stream.read().strip()
        return ""

    def __build_connection_string(self):
        config = {'Driver': '{ODBC Driver 13 for SQL Server}',
                  'Server': '192.168.56.1',
                  'UID': 'dev',
                  'Encrypt': 'yes',
                  'Database': 'StrathClyde',
                  'TrustServerCertificate': 'yes',
                  'PWD': self.__load_credentials()}
        return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])
