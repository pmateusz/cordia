"""Utility functions for parsing datetime objects"""

import datetime
import logging


def try_parse_iso_date(text):
    """Parses date string in ISO format"""

    try:
        return datetime.datetime.strptime(text, '%Y-%m-%d').date()
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None


def try_parse_iso_datetime(text):
    """Parses datetime string in ISO format"""

    try:
        return datetime.datetime.strptime(text, '%Y-%m-%dT%H:%M:%S')
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None


def try_parse_iso_time(text):
    """Parses time in ISO format"""

    try:
        return datetime.datetime.strptime(text, '%H:%M:%S').time()
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None


def try_parse_duration(text):
    """Parses time delta from seconds"""

    try:
        return datetime.timedelta(seconds=int(text))
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None
