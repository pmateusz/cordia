"""Visualise an optimal solution"""
import os

from rows.visualisation.dynamic_network import generate_graph
from rows.visualisation.print_schedule import generate_MSExcel


class Handler:
    """Visualise an optimal solution"""

    def __init__(self, application):
        self.__application = application
        self.__data_source = application.data_source
        self.__console = application.console

    def __call__(self, command):
        problem_file = getattr(command, 'problem')
        input_file = getattr(command, 'file')
        output_file = getattr(command, 'output')

        if not output_file:
            filename, file_extension = os.path.splitext(input_file)
            output_file = filename

        try:
            print("Generating network map (use Google Earth to visualise it): "+output_file+".kml")
            generate_graph(input_file,output_file+".kml")
            print("Print optimised schedule and human planner schedule in Excel: "+output_file+".xls")
            generate_MSExcel(problem_file, input_file,output_file+".xls")

        except FileExistsError:
            error_msg = "The file '{0}' doesn't exists. Please try again with a different name.".format(input_file)
            self.__console.write_line(error_msg)
            return 1
        except FileExistsError:
            error_msg = "The file '{0}' already exists. Please try again with a different name.".format(output_file)
            self.__console.write_line(error_msg)
            return 1
        except RuntimeError as ex:
            logging.error('Failed to save Google Earth KML file due to error: %s', ex)
            return 1
