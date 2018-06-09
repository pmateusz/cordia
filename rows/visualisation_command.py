"""Visualise an optimal solution"""

from rows.visualisation.dynamic_network import generate_graph
from rows.visualisation.excel_graphs import generate_stats


class Handler:
    """Visualise an optimal solution"""

    def __init__(self, application):
        self.__application = application
        self.__data_source = application.data_source
        self.__console = application.console

    def __call__(self, command):
        input_file = getattr(command, 'file')
        output_file = getattr(command, 'output')

        try:
            generate_graph(input_file,output_file+".kml")
            generate_stats(input_file,output_file+".xls")
            
            # run google earth and excel for visualisation

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
