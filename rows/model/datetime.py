"""Utility functions for parsing datetime objects"""

import datetime


# TODO: handle parsing errors
def try_parse_iso_date(text):
    """Parses date string in ISO format"""

    return datetime.datetime.strptime(text, '%Y-%m-%d').date()


def try_parse_iso_datetime(text):
    """Parses datetime string in ISO format"""

    return datetime.datetime.strptime(text, '%Y-%m-%dT%H:%M:%S')


def try_parse_iso_time(text):
    """Parses time in ISO format"""
    return datetime.datetime.strptime(text, '%H:%M:%S').time()


def try_parse_duration(text):
    """Parses time delta from seconds"""

    return datetime.timedelta(seconds=int(text))
