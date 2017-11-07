"""Parse and validate command line arguments."""

import argparse


class Parser:
    """Parse and validate command line arguments."""

    def __init__(self):
        """Register parsers for supported commands"""

        self.parser_ = argparse.ArgumentParser(prog='rows',
                                               description='Robust Optimization '
                                                           'for Workforce Scheduling command line utility')
        subparsers = self.parser_.add_subparsers()

        subparsers.add_parser(name='version',
                              help='show the version of this program and exit')

        pull_parser = subparsers.add_parser(name='pull',
                                            help='pull an instance of the scheduling problem from an external source')
        pull_parser.add_argument('area',
                                 help='an administration, operations and management area'
                                      ' where the requested visits are assigned to')
        pull_parser.add_argument('-f',
                                 '--from',
                                 help='limit considered visits to these that are requested'
                                      ' after the specified date and time')
        pull_parser.add_argument('-t',
                                 '--to',
                                 help='limit considered visits to these that are requested'
                                      ' until the specified date and time')

        subparsers.add_parser(name='solve',
                              help='solve an instance of the scheduling problem')

        export_parser = subparsers.add_parser(name='export',
                                              help='export a schedule in the CSV format')
        export_parser.add_argument('-o',
                                   '--output',
                                   help='an output file where the schedule should be saved')

    def parse_args(self):
        """Parse command line arguments"""

        return self.parser_.parse_args()
