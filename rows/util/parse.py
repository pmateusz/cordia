import datetime

def parse_date(value):
    date_time = datetime.datetime.strptime(value, '%Y-%m-%d')
    return date_time.date()


def parse_time(value):
    date_time = datetime.datetime.strptime(value, '%H:%M:%S')
    return date_time.time()


def parse_timedelta(value):
    total_seconds = int(value)
    return datetime.timedelta(seconds=total_seconds)


def parse_datetime(value):
    return datetime.datetime.strptime(value, '%Y-%m-%dT%H:%M:%S')
