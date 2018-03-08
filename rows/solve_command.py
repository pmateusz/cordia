"""Implements the solve command"""

import subprocess

from rows.util.file_system import real_path


# TODO: accept command line arguments and pass them
# TODO: handle rejected input by the solver
# TODO: pass request to terminate calculations

class Handler:
    """Implements the solve command"""

    def __init__(self, application):
        self.__application = application
        self.__console = application.console
        self.__settings = application.settings

    def __call__(self, command):
        # ./rows-main --problem=../problem.json --solution=../past_solution.json --solution-limit=1024 --time-limit=00:30:00
        solver_path = real_path(self.__settings.solver_path)
        maps_path = real_path(self.__settings.maps_path)
        problem = real_path('~/dev/cordia/problem.json')
        with subprocess.Popen(
                [solver_path,
                 '--time-limit=00:30:00',
                 '--solution-limit=1024',
                 '--problem=' + problem,
                 '--map=' + maps_path], stdout=subprocess.PIPE, close_fds=True) as proc:
            while True:
                try:
                    line = proc.stdout.readline()
                    if line:
                        print(line)
                    else:
                        proc.wait(1)
                except subprocess.TimeoutExpired:
                    line = proc.stdout.readline()
                    if line:
                        print(line)
