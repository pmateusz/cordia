import pathlib

import pyodbc

from rows.util.file_system import real_path


class DatabaseConnector:

    def __init__(self, settings):
        self.__settings = settings
        self.__connection_string = None
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
        config = {'Driver': '{' + self.__settings.database_driver + '}',
                  'Server': self.__settings.database_server,
                  'Database': self.__settings.database_name,
                  'UID': self.__settings.database_user,
                  'PWD': self.__load_credentials(),
                  'Encrypt': 'yes',
                  'Connection Timeout': 5,
                  'TrustServerCertificate': 'yes'}
        return ';'.join(['{0}={1}'.format(key, value) for key, value in config.items()])

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
