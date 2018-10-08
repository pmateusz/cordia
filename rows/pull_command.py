"""Implements the pull command"""

import json
import logging

import rows.version

from rows.model.json import JSONEncoder
from rows.model.metadata import Metadata
from rows.model.problem import Problem
from rows.sql_data_source import SqlDataSource


class Handler:
    """Implements the pull command"""

    def __init__(self, application):
        self.__application = application
        self.__data_source = application.data_source
        self.__console = application.console

    def __call__(self, command):
        area = getattr(command, 'area')
        begin_date = getattr(command, 'from').value
        end_date = getattr(command, 'to').value
        output_file = getattr(command, 'output')
        resource_estimator_name = getattr(command, 'resource_estimator', None)
        duration_estimator_name = getattr(command, 'duration_estimator', None)

        problem = self.__create_problem(area, begin_date, end_date, resource_estimator_name, duration_estimator_name)
        try:
            if not problem.visits:
                logging.warning('Problem does not contain any visits')

            with open(output_file, self.__application.output_file_mode) as file_stream:
                json.dump(problem, file_stream, indent=2, sort_keys=False, cls=JSONEncoder)
            return 0
        except FileExistsError:
            error_msg = "The file '{0}' already exists. Please try again with a different name.".format(output_file)
            self.__console.write_line(error_msg)
            return 1
        except RuntimeError as ex:
            logging.error('Failed to save problem instance due to error: %s', ex)
            return 1

    def __create_problem(self, area, begin_date, end_date, resource_estimator_name, duration_estimator_name):
        visits, carers, service_users = [], [], []
        if resource_estimator_name == SqlDataSource.PLANNED_RESOURCE_ESTIMATOR_NAME:
            carers = self.__data_source.get_carers(area, begin_date, end_date)
            visits = self.__data_source.get_visits(area,
                                                   begin_date,
                                                   end_date,
                                                   self.__create_duration_estimator(duration_estimator_name))
            service_users = self.__data_source.get_service_users(area, begin_date, end_date)
        elif resource_estimator_name == SqlDataSource.USED_RESOURCE_ESTIMATOR_NAME:
            visits, carers = self.__data_source.get_visits_carers_from_schedule(area,
                                                                                begin_date,
                                                                                end_date,
                                                                                self.__create_duration_estimator(
                                                                                    duration_estimator_name))
            service_users = self.__data_source.get_service_users(area, begin_date, end_date)
        else:
            error_msg = SqlDataSource.validate_resource_estimator(resource_estimator_name)
            raise RuntimeError(error_msg)
        return Problem(metadata=Metadata(area=area, begin=begin_date, end=end_date, version=rows.version.VERSION),
                       carers=carers,
                       visits=visits,
                       service_users=service_users)

    @staticmethod
    def __create_duration_estimator(name):
        percentile = 0.60
        confidence = 0.90
        error = 0.005
        min_duration = '00:05:00'

        if not name:
            return SqlDataSource.PlannedDurationEstimator()

        name_to_use = Handler.__normalize(name)
        if name_to_use == SqlDataSource.GlobalTaskConfidenceIntervalEstimator.NAME:
            return SqlDataSource.GlobalTaskConfidenceIntervalEstimator(percentile,
                                                                       confidence,
                                                                       error,
                                                                       min_duration=min_duration)
        elif name_to_use == SqlDataSource.GlobalPercentileEstimator.NAME:
            return SqlDataSource.GlobalPercentileEstimator(percentile, min_duration=min_duration)
        elif name_to_use == SqlDataSource.PlannedDurationEstimator.NAME:
            return SqlDataSource.PlannedDurationEstimator()
        elif name_to_use == SqlDataSource.PastDurationEstimator.NAME:
            return SqlDataSource.PastDurationEstimator()
        elif name_to_use == SqlDataSource.ForecastEstimator.NAME:
            return SqlDataSource.ForecastEstimator()
        return SqlDataSource.PlannedDurationEstimator()

    @staticmethod
    def __normalize(text):
        return text.strip().lower()
