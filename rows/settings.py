"""Implements persistent configuration settings"""

import json
import os.path

from rows.util.file_system import real_path


class Settings:
    """Implements persistent configuration settings"""

    DEFAULT_MAPS_PATH_KEY = 'maps_path'
    DEFAULT_LOCATION_CACHE_PATH_KEY = 'location_cache_path'
    DEFAULT_DIFFICULT_LOCATIONS_PATH_KEY = 'difficult_location_cache_path'
    DEFAULT_USER_GEO_TAGGING_PATH_KEY = 'geo_tagging_path'
    DEFAULT_SOLVER_PATH_KEY = 'solver_path'
    DEFAULT_CREDENTIALS_PATH_KEY = 'database_credentials_path'
    DEFAULT_DATABASE_SERVER_KEY = 'database_server'
    DEFAULT_DATABASE_NAME_KEY = 'database_name'
    DEFAULT_DATABASE_USER_KEY = 'database_user'

    DEFAULT_DATABASE_SERVER = '192.168.56.1'
    DEFAULT_DATABASE = 'StrathClyde'
    DEFAULT_DATABASE_USER = 'dev'

    def __init__(self, install_dir):
        self.__install_dir = real_path(install_dir)
        self.__data_dir = None
        self.__settings_path = None
        self.__maps_path = None
        self.__location_cache_path = None
        self.__difficult_locations_path = None
        self.__user_geo_tagging_path = None
        self.__solver_dir = None
        self.__database_credentials_path = None
        self.__database_server = None
        self.__database_name = None
        self.__database_user = None
        self.__set_default_settings()

    def reload(self):
        path = real_path(self.get_default_settings_path())
        try:
            with open(path, mode='r') as settings_file:
                settings = json.load(settings_file)
                self.__maps_path = settings.get(Settings.DEFAULT_MAPS_PATH_KEY, self.get_default_maps_path())
                self.__location_cache_path = settings.get(Settings.DEFAULT_LOCATION_CACHE_PATH_KEY,
                                                          self.get_default_location_cache())
                self.__difficult_locations_path = settings.get(Settings.DEFAULT_DIFFICULT_LOCATIONS_PATH_KEY,
                                                               self.get_default_difficult_location_cache())
                self.__user_geo_tagging_path = settings.get(Settings.DEFAULT_USER_GEO_TAGGING_PATH_KEY,
                                                            self.get_default_user_geo_tagging_path())
                self.__solver_dir = settings.get(Settings.DEFAULT_SOLVER_PATH_KEY, self.get_default_solver_path())
                self.__database_credentials_path = settings.get(Settings.DEFAULT_CREDENTIALS_PATH_KEY,
                                                                self.get_default_credentials_path())
                self.__database_server = settings.get(Settings.DEFAULT_DATABASE_SERVER_KEY,
                                                      Settings.DEFAULT_DATABASE_SERVER)
                self.__database_name = settings.get(Settings.DEFAULT_DATABASE_NAME_KEY,
                                                    Settings.DEFAULT_DATABASE)
                self.__database_user = settings.get(Settings.DEFAULT_DATABASE_USER_KEY,
                                                    Settings.DEFAULT_DATABASE_USER)

        except FileNotFoundError:
            self.__set_default_settings()
            with open(path, mode='w') as settings_file:
                json.dump(self.__as_dict(), settings_file, indent=2)
        except json.decoder.JSONDecodeError as ex:
            raise RuntimeError('Failed to parse the settings file: {0}'.format(path), ex)

    def __as_dict(self):
        return {Settings.DEFAULT_MAPS_PATH_KEY: self.maps_path,
                Settings.DEFAULT_LOCATION_CACHE_PATH_KEY: self.location_cache_path,
                Settings.DEFAULT_DIFFICULT_LOCATIONS_PATH_KEY: self.difficult_locations_path,
                Settings.DEFAULT_USER_GEO_TAGGING_PATH_KEY: self.user_geo_tagging_path,
                Settings.DEFAULT_SOLVER_PATH_KEY: self.solver_path,
                Settings.DEFAULT_CREDENTIALS_PATH_KEY: self.database_credentials_path,
                Settings.DEFAULT_DATABASE_SERVER_KEY: self.database_server,
                Settings.DEFAULT_DATABASE_NAME_KEY: self.database_name,
                Settings.DEFAULT_DATABASE_USER_KEY: self.database_user}

    def __set_default_settings(self):
        self.__data_dir = self.get_default_data_dir()
        self.__maps_path = self.get_default_maps_path()
        self.__solver_path = self.get_default_solver_path()
        self.__location_cache_path = self.get_default_location_cache()
        self.__difficult_locations_path = self.get_default_difficult_location_cache()
        self.__user_geo_tagging_path = self.get_default_user_geo_tagging_path()
        self.__database_credentials_path = self.get_default_credentials_path()
        self.__database_server = self.DEFAULT_DATABASE_SERVER
        self.__database_name = self.DEFAULT_DATABASE
        self.__database_user = self.DEFAULT_DATABASE_USER

    def get_default_data_dir(self):
        return os.path.join(self.install_dir, 'data')

    def get_default_maps_path(self):
        return os.path.join(self.data_dir, 'scotland-latest.osrm')

    def get_default_solver_path(self):
        return os.path.join(self.install_dir, 'build', 'rows-main')

    def get_default_settings_path(self):
        return os.path.join(self.data_dir, 'settings.json')

    def get_default_location_cache(self):
        return os.path.join(self.data_dir, 'location_cache.json')

    def get_default_difficult_location_cache(self):
        return os.path.join(self.data_dir, 'difficult_location_cache.json')

    def get_default_user_geo_tagging_path(self):
        return os.path.join(self.data_dir, 'user_geo_tagging.csv')

    def get_default_credentials_path(self):
        return os.path.join(self.data_dir, '.dev')

    @property
    def data_dir(self):
        return self.__data_dir

    @property
    def install_dir(self):
        return self.__install_dir

    @property
    def solver_path(self):
        return self.__solver_path

    @property
    def maps_path(self):
        return self.__maps_path

    @property
    def location_cache_path(self):
        return self.__location_cache_path

    @property
    def difficult_locations_path(self):
        return self.__difficult_locations_path

    @property
    def user_geo_tagging_path(self):
        return self.__user_geo_tagging_path

    @property
    def database_credentials_path(self):
        return self.__database_credentials_path

    @property
    def database_server(self):
        return self.__database_server

    @property
    def database_name(self):
        return self.__database_name

    @property
    def database_user(self):
        return self.__database_user
