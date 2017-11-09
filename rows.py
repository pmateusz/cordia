#!/usr/bin/env python3

"""Robust Optimization for Workforce Scheduling command line utility.

Run with the `--help` option for more information on the supported commands and their syntax.
"""

import sys
import rows.application


if __name__ == '__main__':
    try:
        APPLICATION = rows.application.Application()
        EXIT_CODE = APPLICATION.run(sys.argv[1:])
    except RuntimeError:
        print(sys.exc_info()[0], file=sys.stderr)
        EXIT_CODE = 1
    sys.exit(EXIT_CODE)
