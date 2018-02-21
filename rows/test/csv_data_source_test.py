"""Test CSV data source"""

import collections
import datetime
import unittest
import os

from rows.location_finder import Result
from rows.csv_data_source import CSVDataSource
from rows.model.area import Area
from rows.model.location import Location


class TestLocationFinder:

    def __init__(self):
        self.__location_pool = [Location(longitude='-4.2367472', latitude='55.8629486'),
                                Location(longitude='-4.2545638', latitude='55.8615508'),
                                Location(longitude='-4.2526512', latitude='55.8588369'),
                                Location(longitude='-4.2471298', latitude='55.8559'),
                                Location(longitude='-4.2484651', latitude='55.8599103'),
                                Location(longitude='-4.25445999', latitude='55.8646492')]
        self.__counter = 0

    def find(self, address):
        position = self.__counter
        self.__counter = (self.__counter + 1) % len(self.__location_pool)
        return Result(result_set={
            Result.LOCATION: self.__location_pool[position],
            Result.ADDRESS: address,
            Result.POINT_OF_INTEREST: {}})


@unittest.skipIf(os.getenv('TRAVIS', 'false') == 'true',
                 'Resource files required to run the test are not available in CI')
class CSVDataSourceTest(unittest.TestCase):
    """Test CSV data source"""

    def setUp(self):
        from rows.util.file_system import real_path
        location_finder = TestLocationFinder()

        self.data_source = CSVDataSource(location_finder,
                                         real_path('~/dev/cordia/data/cordia/home_carer_position.csv'),
                                         real_path('~/dev/cordia/data/cordia/home_carer_shift_pattern.csv'),
                                         real_path('~/dev/cordia/data/cordia/service_user_visit.csv'),
                                         real_path('~/dev/cordia/data/cordia/past_visits.csv'))

        self.example_area = Area(key='test_key')
        self.example_begin_datetime = datetime.datetime(2017, 2, 1)
        self.example_begin_date = self.example_begin_datetime.date()
        self.example_end_datetime = datetime.datetime(2017, 2, 7)
        self.example_end_date = self.example_end_datetime.date()

    def test_load_carers(self):
        """Can load carers"""

        carers = self.data_source.get_carers(self.example_area, self.example_begin_date, self.example_end_date)
        self.assertIsNotNone(carers)
        self.assertTrue(carers)

    def test_load_visits(self):
        """Can load visits"""

        visits = self.data_source.get_visits(self.example_area, self.example_begin_date, self.example_end_date)
        self.assertIsNotNone(visits)
        self.assertTrue(visits)

    def test_load_past_visits(self):
        """Can load past visits"""

        past_visits = self.data_source.get_past_schedule(self.example_area,
                                                         self.example_begin_date,
                                                         self.example_end_date).visits
        self.assertIsNotNone(past_visits)
        self.assertTrue(past_visits)

        cancelled_visits = list(filter(lambda visit: visit.cancelled, past_visits))
        self.assertIsNotNone(cancelled_visits)
        self.assertTrue(cancelled_visits)

        not_cancelled_visits = list(filter(lambda visit: not visit.cancelled, past_visits))
        self.assertIsNotNone(not_cancelled_visits)
        self.assertTrue(not_cancelled_visits)

        not_planned_visit = list(filter(lambda visit: not visit.visit, not_cancelled_visits))
        self.assertIsNotNone(not_planned_visit)
        self.assertTrue(not_planned_visit)

        moved_visits = list(filter(lambda visit: visit.visit and visit.time != visit.visit.time, not_cancelled_visits))
        self.assertIsNotNone(moved_visits)
        self.assertTrue(moved_visits)

        planned_visits = list(
            filter(lambda visit: visit.visit and visit.time == visit.visit.time, not_cancelled_visits))
        self.assertIsNotNone(planned_visits)
        self.assertTrue(planned_visits)

        self.assertEqual(len(past_visits),
                         len(cancelled_visits) + len(not_planned_visit) + len(moved_visits) + len(planned_visits))

    def test_load_past_schedule(self):
        """Can load past schedule within the area and time interval"""

        schedule = self.data_source.get_past_schedule(self.example_area, self.example_begin_date, self.example_end_date)

        self.assertTrue(schedule.visits)
        for visit in schedule.visits:
            self.assertLess(visit.date, self.example_end_date)
            self.assertLessEqual(self.example_begin_date, visit.date)

        visit_attendance = collections.defaultdict(int)

        def get_key(local_visit):
            return local_visit.service_user, local_visit.date, local_visit.time, local_visit.duration

        for visit in schedule.visits:
            calendar_visit = visit.visit
            if not calendar_visit:
                continue
            key = get_key(calendar_visit)
            visit_attendance[key] += 1

        for visit_bundle in self.data_source.get_visits(self.example_area,
                                                        self.example_begin_date,
                                                        self.example_end_date):
            for visit in visit_bundle.visits:
                key = (visit_bundle.service_user, visit.date, visit.time, visit.duration)
                if key in visit_attendance:
                    self.assertTrue(visit_attendance[key], visit.carer_count)

        # may be useful in the future
        # with open('past_solution.json', 'x') as file_stream:
        #     json.dump(schedule, file_stream, indent=2, sort_keys=False, cls=JSONEncoder)

    def test_load_visits_for_area(self):
        """Returns all visits within the area and time interval"""

        # when
        visits = self.data_source.get_visits(self.example_area, self.example_begin_date, self.example_end_date)

        # then
        self.assertIsNotNone(visits)
        self.assertTrue(visits)

        for visit_bundle in visits:
            for visit in visit_bundle.visits:
                self.assertTrue(self.example_begin_date <= visit.date)
                self.assertTrue(visit.date < self.example_end_date)

    def test_load_carers_for_area(self):
        """Returns all carers within the area who are available in the time interval"""

        # when
        carers = self.data_source.get_carers(self.example_area, self.example_begin_date, self.example_end_date)

        # then
        self.assertIsNotNone(carers)
        self.assertTrue(carers)


if __name__ == '__main__':
    unittest.main()
