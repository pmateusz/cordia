"""Implements the solve command"""

import subprocess
import threading
import sys
import os
import math
import logging
import codecs
import json

from rows.parser import Parser
from rows.util.file_system import real_path


def memory_to_human_readable(value):
    UNIT = 1024
    if value < UNIT:
        return '{0} B'.format(value)
    exp = int(math.log(value) / math.log(UNIT))
    prefix = 'KMGTPE'[exp - 1]
    return '{0:.1f} {1:s}B'.format(math.pow(UNIT, exp), prefix)


class Handler:
    """Implements the solve command"""

    ENCODING = 'utf-8'

    class SolverController:

        def __init__(self, console, settings, command):
            self.__console = console
            self.__settings = settings
            self.__args = self.__create_args(command)
            self.__lock = threading.Lock()
            self.__return_code_condition = threading.Condition()
            self.__thread = None
            self.__process = None
            self.__return_code = None
            self.__progress_header_printed = False

        def start(self):
            self.__thread = threading.Thread(target=self.__control_loop)
            self.__thread.start()
            self.__process = None
            self.__return_code = None

        def stop(self):
            process = self.process
            if process:
                try:
                    process.stdin.write(codecs.encode('stop' + os.linesep, encoding=Handler.ENCODING, errors='ignore'))
                    process.stdin.flush()
                except Exception as ex:
                    logging.error(ex, exc_info=True)

        def join(self):
            with self.__return_code_condition:
                if not self.return_code:
                    self.__return_code_condition.wait()

        def __create_args(self, command):
            args = [real_path(self.__settings.solver_path),
                    '--maps=' + real_path(self.__settings.maps_path),
                    '--problem=' + real_path(getattr(command, Parser.SOLVE_PROBLEM_ARG))]

            start_solution_path = getattr(command, 'start')
            if start_solution_path:
                start_solution_path = real_path(start_solution_path)
                args.append('--solution=' + start_solution_path)
            solutions_limit_arg = getattr(command, 'solutions_limit')
            if solutions_limit_arg:
                args.append('--solutions-limit=' + str(solutions_limit_arg))

            time_limit_arg = getattr(command, 'time_limit')
            if time_limit_arg:
                args.append('--time-limit=' + time_limit_arg)

            schedule_date_arg = getattr(command, 'schedule_date')
            if schedule_date_arg:
                args.append('--scheduling-date={0}-{1}-{2}'.format(schedule_date_arg.year,
                                                                   schedule_date_arg.month,
                                                                   schedule_date_arg.day))
            args.append('--console-format=json')
            return args

        def __handle_message(self, message):
            message_type = message.get('type')
            content = message.get('content')
            if message_type == 'message':
                self.__console.write_line(content)
            elif message_type == 'problem_definition':
                self.__console.write_line(
                    'problem definition:\ttime_window={0} | visits={1} | carers={2} | covered_visits={3}'.format(
                        content.get('visit_time_windows', '?'),
                        content.get('visits', '?'),
                        content.get('carers', '?'),
                        content.get('covered_visits', '?')))
            elif message_type == 'progress_step':
                if not self.__progress_header_printed:
                    self.__progress_header_printed = True
                    self.__console.write_line(
                        '{0:>16s} | {1:>16s} | {2:>10s} | {3:>12} |'
                        ' {4:>16} | {5:>}'.format('Cost', 'Dropped Visits', 'Solutions', 'Branches', 'Memory Usage',
                                                  'Wall Time'))

                content['memory_usage'] = memory_to_human_readable(content['memory_usage'])
                self.__console.write_line(
                    '{cost:16g} | {dropped_visits:16d} | {solutions:10d} | {branches:12}'
                    ' | {memory_usage:>16} | {wall_time}'.format(
                        **content))
            elif message_type == 'tracing_event':
                pass
            else:
                logging.error('Failed to decode message: {0}', message)

        def __control_loop(self):
            try:
                with subprocess.Popen(self.__args,
                                      stdout=subprocess.PIPE,
                                      stdin=subprocess.PIPE,
                                      close_fds=True) as process:
                    self.process = process
                    while True:
                        try:
                            raw_bytes = process.stdout.readline()
                            if raw_bytes:
                                line = None
                                try:
                                    line = codecs.decode(raw_bytes, encoding=Handler.ENCODING, errors='ignore')
                                    message = json.loads(line, encoding=Handler.ENCODING)
                                    self.__handle_message(message)
                                except json.decoder.JSONDecodeError:
                                    if line:
                                        logging.warning('Failed to decode message: ' + line.lstrip())
                                except Exception as ex:
                                    logging.error(ex, exc_info=True)
                            else:
                                process.wait(1)
                                with self.__return_code_condition:
                                    self.__return_code = process.returncode
                                    self.__return_code_condition.notify_all()
                                return
                        except subprocess.TimeoutExpired:
                            line = process.stdout.readline()
                            if line:
                                self.__console.write_line(line)
            finally:
                self.process = None

        @property
        def process(self):
            try:
                self.__lock.acquire()
                return self.__process
            finally:
                self.__lock.release()

        @process.setter
        def process(self, value):
            try:
                self.__lock.acquire()
                self.__process = value
            finally:
                self.__lock.release()

        @property
        def return_code(self):
            return self.__return_code

    def __init__(self, application):
        self.__application = application
        self.__console = application.console
        self.__settings = application.settings

    def __call__(self, command):
        controller = Handler.SolverController(self.__console, self.__settings, command)
        controller.start()

        while True:
            line = sys.stdin.readline()
            if not line:
                break
            command = line.strip().lower()
            if command == 'stop':
                controller.stop()
                break
            else:
                self.__console.write_line("Unknown command '{0}'. Use 'stop' instead.".format(command))

        controller.join()
        return controller.return_code
