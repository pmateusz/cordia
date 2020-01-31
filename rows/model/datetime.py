"""Utility functions for parsing datetime objects"""

import datetime
import logging
import typing


def try_parse_iso_date(text: str) -> typing.Optional[datetime.date]:
    """Parses date string in ISO format"""

    try:
        return datetime.datetime.strptime(text, '%Y-%m-%d').date()
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None


def try_parse_datetime(text: str) -> typing.Optional[datetime.datetime]:
    """Parses datetime string"""

    try:
        return datetime.datetime.strptime(text, '%Y-%b-%d %H:%M:%S')
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None


def try_parse_iso_datetime(text: str) -> typing.Optional[datetime.datetime]:
    """Parses datetime string in ISO format"""

    try:
        return datetime.datetime.strptime(text, '%Y-%m-%dT%H:%M:%S')
    except ValueError:
        try:
            return datetime.datetime.strptime(text, '%Y-%m-%dT%H:%M:%S.%f')
        except ValueError as ex:
            logging.error("Failed to parse '%s' due to error '%s'", text, ex)
            return None


def try_parse_iso_time(text: str) -> typing.Optional[datetime.time]:
    """Parses time in ISO format"""

    try:
        return datetime.datetime.strptime(text, '%H:%M:%S').time()
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None


def try_parse_duration(text: str) -> typing.Optional[datetime.timedelta]:
    """Parses time delta from seconds"""

    try:
        if ':' in text:
            time = datetime.datetime.strptime(text, '%H:%M:%S').time()
            return datetime.timedelta(hours=time.hour, minutes=time.minute, seconds=time.second)
        return datetime.timedelta(seconds=int(float(text)))
    except ValueError as ex:
        logging.error("Failed to parse '%s' due to error '%s'", text, ex)
        return None
