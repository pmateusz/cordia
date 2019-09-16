import datetime
import functools
import logging
import operator
import typing

import matplotlib
import matplotlib.dates
import matplotlib.pyplot
import matplotlib.ticker
import numpy
import pandas

import rows.model.problem
import rows.model.schedule
import rows.model.visit


class VisitDict:
    def __init__(self):
        self.__dict = {}

    def __contains__(self, item):
        return bool(self.__get_item_or_none(item) is not None)

    def __getitem__(self, item):
        element = self.__get_item_or_none(item)
        if element:
            return element
        raise KeyError(item)

    def __setitem__(self, key, value):
        self.__dict[key] = value

    def __get_item_or_none(self, item):
        if item in self.__dict:
            return self.__dict.get(item)
        if item.key:
            same_id_items = [visit for visit in self.__dict if visit.key == item.key]
            if same_id_items:
                if len(same_id_items) > 1:
                    logging.warning('More than one visit with id %s', item.key)
                return self.__dict[same_id_items[0]]

        visits_with_time_offset = [(visit, abs(self.__time_diff(visit.time, item.time).total_seconds()))
                                   for visit in self.__dict if visit.service_user == item.service_user]
        if visits_with_time_offset:
            visit, time_offset = min(visits_with_time_offset, key=operator.itemgetter(1))
            if time_offset > 2 * 3600:
                logging.warning('Suspiciously high time offset %s while finding a key of the visit %s', time_offset, visit)
            return self.__dict[visit]

        return None

    @staticmethod
    def __time_diff(left_time, right_time):
        __REF_DATE = datetime.date(2018, 1, 1)
        return datetime.datetime.combine(__REF_DATE, left_time) - datetime.datetime.combine(__REF_DATE, right_time)


class TimeDeltaConverter:

    def __init__(self):
        self.__zero = datetime.datetime(2018, 1, 1)
        self.__zero_num = matplotlib.dates.date2num(self.__zero)

    @property
    def zero(self):
        return self.__zero

    @property
    def zero_num(self):
        return self.__zero_num

    def convert(self, value):
        return matplotlib.dates.date2num(self.__zero + value) - self.__zero_num

    def __call__(self, series):
        return [self.convert(value) for value in series]


class CumulativeHourMinuteConverter:
    def __init__(self):
        self.__REFERENCE = matplotlib.dates.date2num(datetime.datetime(2018, 1, 1))

    @property
    def reference(self):
        return self.__REFERENCE

    def __call__(self, x, pos=None):
        time_delta = matplotlib.dates.num2timedelta(x - self.__REFERENCE)
        hours = int(time_delta.total_seconds() // matplotlib.dates.SEC_PER_HOUR)
        minutes = int((time_delta.total_seconds() - hours * matplotlib.dates.SEC_PER_HOUR) \
                      // matplotlib.dates.SEC_PER_MIN)
        return '{0:02d}:{1:02d}:00'.format(hours, minutes)


def get_schedule_data_frame(schedule: rows.model.schedule.Schedule,
                            problem: rows.model.problem.Problem,
                            duration_estimator: typing.Callable[[rows.model.visit.Visit], datetime.timedelta],
                            distance_estimator: typing.Callable[[rows.model.visit.Visit, rows.model.visit.Visit], datetime.timedelta]):
    data_set = []
    for route in schedule.routes():
        carer_diary = problem.get_diary(route.carer, schedule.date())

        if carer_diary is None:
            logging.warning('Working hours not available for carer %s', route.carer.sap_number)
            continue

        travel_time = datetime.timedelta()
        for source, destination in route.edges():
            travel_time += distance_estimator(source, destination)
        service_time = datetime.timedelta()
        for local_visit in route.visits:
            visit_duration = duration_estimator(local_visit.visit)
            if visit_duration:
                service_time += visit_duration
            else:
                service_time += local_visit.duration
        available_time = functools.reduce(operator.add, (event.duration for event in carer_diary.events))
        data_set.append([route.carer.sap_number,
                         available_time,
                         service_time,
                         travel_time,
                         float(service_time.total_seconds() + travel_time.total_seconds()) / available_time.total_seconds(),
                         len(route.visits)])
    data_set.sort(key=operator.itemgetter(4))
    return pandas.DataFrame(columns=['Carer', 'Availability', 'Service', 'Travel', 'Usage', 'Visits'], data=data_set)


def calculate_observed_visit_duration(schedule):
    observed_duration_by_visit = rows.plot.VisitDict()
    for past_visit in schedule.visits:
        if past_visit.check_in and past_visit.check_out:
            observed_duration = past_visit.check_out - past_visit.check_in
            if observed_duration.days < 0:
                logging.error('Observed duration %s is negative', observed_duration)
        else:
            logging.warning(
                'Visit %s is not supplied with information on check-in and check-out information',
                past_visit.visit.key)
            observed_duration = past_visit.duration
        observed_duration_by_visit[past_visit.visit] = observed_duration
    return observed_duration_by_visit


def add_legend(axis, handles, labels, ncol, bbox_to_anchor, loc='lower center'):
    legend = axis.legend(handles,
                         labels,
                         ncol=ncol,
                         loc=loc,
                         bbox_to_anchor=bbox_to_anchor,
                         fancybox=None,
                         edgecolor=None,
                         handletextpad=0.1,
                         columnspacing=0.15)
    return legend


FILE_FORMAT = 'pdf'


def save_figure(file_path, file_format=FILE_FORMAT):
    matplotlib.pyplot.savefig(file_path + '.' + file_format,
                              format=file_format,
                              transparent=True,
                              dpi=300)
    # bbox_extra_artists=(legend,))


def save_workforce_histogram(data_frame, file_path, output_format):
    __width = 0.35
    figure, axis = matplotlib.pyplot.subplots()
    try:
        indices = numpy.arange(len(data_frame.index))
        time_delta_converter = TimeDeltaConverter()

        travel_series = numpy.array(time_delta_converter(data_frame.Travel))
        service_series = numpy.array(time_delta_converter(data_frame.Service))
        idle_overtime_series = list(data_frame.Availability - data_frame.Travel - data_frame.Service)
        idle_series = numpy.array(time_delta_converter(
            map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
        overtime_series = numpy.array(time_delta_converter(
            map(lambda value: datetime.timedelta(
                seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(),
                idle_overtime_series)))

        service_handle = axis.bar(indices, service_series, __width, bottom=time_delta_converter.zero)
        travel_handle = axis.bar(indices, travel_series, __width,
                                 bottom=service_series + time_delta_converter.zero_num)
        idle_handle = axis.bar(indices, idle_series, __width,
                               bottom=service_series + travel_series + time_delta_converter.zero_num)
        overtime_handle = axis.bar(indices, overtime_series, __width,
                                   bottom=idle_series + service_series + travel_series + time_delta_converter.zero_num)

        axis.yaxis_date()
        axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
        axis.set_xlabel('Carer')
        axis.set_ylabel('Time Delta')
        legend = add_legend(axis,
                            [travel_handle, service_handle, idle_handle, overtime_handle],
                            ['Travel Time', 'Service Time', 'Idle Time', 'Overtime'],
                            ncol=4,
                            bbox_to_anchor=(0.5, -0.2))

        matplotlib.pyplot.tight_layout()

        figure.subplots_adjust(bottom=0.15)

        save_figure(file_path, output_format)
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)


def save_combined_histogram(top_data_frame, bottom_data_frame, labels, file_path, output_file_format):
    __width = 0.35

    y_label1, y_label2 = labels
    figure, (ax1, ax2) = matplotlib.pyplot.subplots(2, 1, sharex=True)
    max_y = 0
    try:
        time_delta_converter = TimeDeltaConverter()

        # put the same scale limits
        for axis, data_frame, y_label in [(ax1, top_data_frame, y_label1), (ax2, bottom_data_frame, y_label2)]:
            travel_series = numpy.array(time_delta_converter(data_frame.Travel))
            service_series = numpy.array(time_delta_converter(data_frame.Service))
            idle_overtime_series = list(data_frame.Availability - data_frame.Travel - data_frame.Service)
            idle_series = numpy.array(time_delta_converter(
                map(lambda value: value if value.days >= 0 else datetime.timedelta(), idle_overtime_series)))
            overtime_series = numpy.array(time_delta_converter(
                map(lambda value: datetime.timedelta(
                    seconds=abs(value.total_seconds())) if value.days < 0 else datetime.timedelta(),
                    idle_overtime_series)))

            indices = numpy.arange(len(data_frame.index))
            service_handle = axis.bar(indices, service_series, __width, bottom=time_delta_converter.zero)
            travel_handle = axis.bar(indices, travel_series, __width,
                                     bottom=service_series + time_delta_converter.zero_num)
            idle_handle = axis.bar(indices, idle_series, __width,
                                   bottom=service_series + travel_series + time_delta_converter.zero_num)
            overtime_handle = axis.bar(indices, overtime_series, __width,
                                       bottom=idle_series + service_series + travel_series + time_delta_converter.zero_num)

            max_y = max(max_y, (service_series + idle_series + travel_series + overtime_series).max())

            axis.yaxis_date()
            axis.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(CumulativeHourMinuteConverter()))
            axis.set_ylabel('Time Delta')
            axis.text(1.02, 0.5, y_label,
                      horizontalalignment='left',
                      verticalalignment='center',
                      rotation=90,
                      clip_on=False,
                      transform=axis.transAxes)

        for axis in [ax1, ax2]:
            axis.set_ylim(top=max_y + time_delta_converter.zero_num)

        ax2.set_xlabel('Carer')
        add_legend(axis,
                   [travel_handle, service_handle, idle_handle, overtime_handle],
                   ['Travel Time', 'Service Time', 'Idle Time', 'Overtime'],
                   ncol=4,
                   bbox_to_anchor=(0.5, -0.4))

        matplotlib.pyplot.tight_layout()

        figure.subplots_adjust(bottom=0.15, right=0.96, hspace=0.05)

        save_figure(file_path, output_file_format)
    finally:
        matplotlib.pyplot.cla()
        matplotlib.pyplot.close(figure)
