"""Implements the solve command"""

import subprocess

from rows.parser import Parser
from rows.util.file_system import real_path


# TODO: handle rejected input by the solver
# TODO: pass request to terminate calculations

class Handler:
    """Implements the solve command"""

    def __init__(self, application):
        self.__application = application
        self.__console = application.console
        self.__settings = application.settings

    def __call__(self, command):
        solver_args = [real_path(self.__settings.solver_path),
                       '--maps=' + real_path(self.__settings.maps_path),
                       '--problem=' + real_path(getattr(command, Parser.SOLVE_PROBLEM_ARG))]

        start_solution_path = getattr(command, 'start')
        if start_solution_path:
            start_solution_path = real_path(start_solution_path)
            solver_args.append('--solution=' + start_solution_path)
        solutions_limit_arg = getattr(command, 'solutions_limit')
        if solutions_limit_arg:
            solver_args.append('--solutions-limit=' + str(solutions_limit_arg))

        time_limit_arg = getattr(command, 'time_limit')
        if time_limit_arg:
            solver_args.append('--time-limit=' + time_limit_arg)

        solver_args.append('--console-format=json')

        with subprocess.Popen(solver_args, stdout=subprocess.PIPE, close_fds=True) as proc:
            while True:
                try:
                    line = proc.stdout.readline()
                    if line:
                        print(line)
                    else:
                        proc.wait(1)
                        return proc.returncode
                except subprocess.TimeoutExpired:
                    line = proc.stdout.readline()
                    if line:
                        print(line)
