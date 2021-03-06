#!/usr/bin/env python3

"""Robust Optimization for Workforce Scheduling command line utility.

Run with the `--help` option for more information on the supported commands and their syntax.
"""

import sys
import logging
import os.path

import rows.application


def handle_exception(exc_type, exc_value, exc_traceback):
    """Logs uncaught exceptions"""

    if issubclass(exc_type, KeyboardInterrupt):
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
    else:
        logging.error("Uncaught exception", exc_info=(exc_type, exc_value, exc_traceback))


if __name__ == '__main__':
    logging.getLogger(__name__).addHandler(logging.StreamHandler(stream=sys.stdout))

    sys.excepthook = handle_exception

    APPLICATION = None
    EXIT_CODE = 0

    try:
        script_file = os.path.realpath(__file__)
        install_dir = os.path.dirname(script_file)
        APPLICATION = rows.application.Application(install_dir)
        args_to_use = APPLICATION.load(sys.argv[1:])
        EXIT_CODE = APPLICATION.run(args_to_use)
    except RuntimeError as ex:
        if ex.args:
            print(ex.args[0], file=sys.stderr)
        if logging.getLogger().isEnabledFor(logging.DEBUG):
            logging.error(ex, exc_info=True)
        EXIT_CODE = 1
    finally:
        if APPLICATION:
            try:
                APPLICATION.dispose()
            except RuntimeError:
                logging.error('Error during application shutdown: %s', sys.exc_info()[0])
        APPLICATION = None

    sys.exit(EXIT_CODE)
