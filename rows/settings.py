"""Implements persistent configuration settings"""

import json

from rows.util.file_system import real_path


class Settings:
    """Implements persistent configuration settings"""

    DEFAULT_MAPS_PATH = '~/dev/cordia/data/scotland-latest.osrm'
    DEFAULT_MAPS_PATH_KEY = 'maps_path'

    DEFAULT_LOCATION_CACHE_PATH = '~/dev/cordia/data/cordia/location_cache.json'
    DEFAULT_LOCATION_CACHE_PATH_KEY = 'location_cache_path'

    DEFAULT_DIFFICULT_LOCATIONS_PATH = '~/dev/cordia/data/cordia/difficult_location_cache.json'
    DEFAULT_DIFFICULT_LOCATIONS_PATH_KEY = 'difficult_location_cache_path'

    DEFAULT_SOLVER_PATH = '~/dev/cordia/build/rows-main'
    DEFAULT_SOLVER_PATH_KEY = 'solver_path'

    DEFAULT_CREDENTIALS_PATH = '~/dev/cordia/.dev'
    DEFAULT_CREDENTIALS_PATH_KEY = 'database_credentials_path'

    DEFAULT_DATABASE_SERVER = '192.168.56.1'
    DEFAULT_DATABASE_SERVER_KEY = 'database_server'

    DEFAULT_DATABASE_NAME = 'StrathClyde'
    DEFAULT_DATABASE_NAME_KEY = 'database_name'

    DEFAULT_DATABASE_USER = 'dev'
    DEFAULT_DATABASE_USER_KEY = 'database_user'

    def __init__(self):
        self.__maps_path = None
        self.__location_cache_path = None
        self.__difficult_locations_path = None
        self.__solver_path = None
        self.__database_credentials_path = None
        self.__database_server = None
        self.__database_name = None
        self.__database_user = None
        self.__set_default_settings()

    def reload(self):
        path = real_path('~/dev/cordia/settings.json')
        try:
            with open(path, mode='r') as settings_file:
                settings = json.load(settings_file)
                self.__maps_path = settings.get(Settings.DEFAULT_MAPS_PATH_KEY,
                                                Settings.DEFAULT_MAPS_PATH)
                self.__location_cache_path = settings.get(Settings.DEFAULT_LOCATION_CACHE_PATH_KEY,
                                                          Settings.DEFAULT_LOCATION_CACHE_PATH)
                self.__difficult_locations_path = settings.get(Settings.DEFAULT_DIFFICULT_LOCATIONS_PATH_KEY,
                                                               Settings.DEFAULT_DIFFICULT_LOCATIONS_PATH)
                self.__solver_path = settings.get(Settings.DEFAULT_SOLVER_PATH_KEY,
                                                  Settings.DEFAULT_SOLVER_PATH)
                self.__database_credentials_path = settings.get(Settings.DEFAULT_CREDENTIALS_PATH_KEY,
                                                                Settings.DEFAULT_CREDENTIALS_PATH)
                self.__database_server = settings.get(Settings.DEFAULT_DATABASE_SERVER_KEY,
                                                      Settings.DEFAULT_DATABASE_SERVER)
                self.__database_name = settings.get(Settings.DEFAULT_DATABASE_NAME_KEY,
                                                    Settings.DEFAULT_DATABASE_NAME)
                self.__database_user = settings.get(Settings.DEFAULT_DATABASE_USER_KEY,
                                                    Settings.DEFAULT_DATABASE_USER)

        except FileNotFoundError:
            self.__set_default_settings()
            with open(path, mode='w') as settings_file:
                json.dump(self.__as_dict(), settings_file, indent=2)
        except json.decoder.JSONDecodeError as ex:
            raise RuntimeError('Failed to parse the settings file: {0}'.format(path), ex)

    def __as_dict(self):
        return {Settings.DEFAULT_MAPS_PATH_KEY: self.maps_path}

    def __set_default_settings(self):
        self.__maps_path = Settings.DEFAULT_MAPS_PATH
        self.__location_cache_path = Settings.DEFAULT_LOCATION_CACHE_PATH
        self.__difficult_locations_path = Settings.DEFAULT_DIFFICULT_LOCATIONS_PATH
        self.__solver_path = Settings.DEFAULT_SOLVER_PATH
        self.__database_credentials_path = Settings.DEFAULT_CREDENTIALS_PATH
        self.__database_server = Settings.DEFAULT_DATABASE_SERVER
        self.__database_name = Settings.DEFAULT_DATABASE_NAME
        self.__database_user = Settings.DEFAULT_DATABASE_USER

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
    def solver_path(self):
        return self.__solver_path

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
