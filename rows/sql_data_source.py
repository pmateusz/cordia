import pathlib

import pyodbc

from rows.model.area import Area


class SqlDataSource:
    LIST_AREAS_QUERY = """SELECT aom.aom_id, aom.area_code
                       FROM [StrathClyde].[dbo].[ListAom] aom
                       ORDER BY aom.area_code"""

    def __init__(self, location_finder):
        self.__location_finder = location_finder
        self.__connection_string = None
        self.__connection = None

    def get_areas(self):
        cursor = self.__get_connection().cursor()
        cursor.execute(SqlDataSource.LIST_AREAS_QUERY)
        return [Area(key=row[0], code=row[1]) for row in cursor.fetchall()]

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
