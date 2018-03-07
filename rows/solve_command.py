"""Implements the solve command"""

import subprocess

from rows.util.file_system import real_path


class Handler:
    """Implements the solve command"""

    def __init__(self, application):
        self.__application = application
        self.__console = application.console

    def __call__(self, command):
        # ./rows-main --problem=../problem.json --map=../data/scotland-latest.osrm
        # --solution=../past_solution.json --solution-limit=1024 --time-limit=00:30:00
        executable = real_path('~/dev/cordia/build/rows-main')
        problem = real_path('~/dev/cordia/problem.json')
        map = real_path('~/dev/cordia/data/scotland-latest.osrm')
        with subprocess.Popen(
                [executable,
                 '--time-limit=00:30:00',
                 '--solution-limit=1024',
                 '--problem=' + problem,
                 '--map=' + map], stdout=subprocess.PIPE, close_fds=True) as proc:
            while True:
                try:
                    proc.wait(1)
                    line = proc.stdout.readline()
                    if line:
                        # TODO: use write from console
                        print(line)
                    break
                except subprocess.TimeoutExpired:
                    line = proc.stdout.readline()
                    if line:
                        print(line)
