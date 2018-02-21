"""Test integration with SQL database"""

import unittest
import pyodbc
import pathlib

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


# TODO: list carers available in the specified area

# TODO: list visits within date interval in the specified area

# TODO: list schedule of carers in the specified area

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

    if __name__ == '__main__':
        unittest.main()
