#!/usr/bin/env python3

"""Robust Optimization for Workforce Scheduling command line utility.

Run with the `--help` option for more information on the supported commands and their syntax.
"""

import sys
from rows.parser import Parser


# TODO: implement the version command
# TODO: parse values and handle errors for each command
# TODO: set default time windows for the pull command


def main():
    """The entry point of the program"""

    parser = Parser()
    args = parser.parse_args()
    print(args)
    return 0


if __name__ == '__main__':
    EXIT_CODE = main()
    sys.exit(EXIT_CODE)
