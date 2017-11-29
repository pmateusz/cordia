"""Test CSV data source"""

import datetime
import unittest
import os

from rows.csv_data_source import CSVDataSource
from rows.model.area import Area


@unittest.skipIf(os.getenv('TRAVIS', 'false') == 'true',
                 'Resource files required to run the test are not available in CI')
class CSVDataSourceTest(unittest.TestCase):
    """Test CSV data source"""

    def setUp(self):
        self.data_source = CSVDataSource('../../data/cordia/home_carer_position.csv',
                                         '../../data/cordia/home_carer_shift_pattern.csv',
                                         '../../data/cordia/service_user_visit.csv')

    def test_load_carers(self):
        """Can load carers"""

        carers = self.data_source.get_carers()
        self.assertIsNotNone(carers)
        self.assertTrue(carers)

    def test_load_visits(self):
        """Can load visits"""

        visits = self.data_source.get_visits()
        self.assertIsNotNone(visits)
        self.assertTrue(visits)

    def test_load_visits_for_area(self):
        """Returns all visits within the area and time interval"""

        # given
        area = Area(key='test_key')
        begin = datetime.date(2017, 2, 1)
        end = datetime.date(2017, 2, 7)

        # when
        visits = self.data_source.get_visits_for_area(area, begin, end)

        # then
        self.assertIsNotNone(visits)
        self.assertTrue(visits)

        for visit in visits:
            self.assertTrue(begin <= visit.date)
            self.assertTrue(visit.date < end)

    def test_load_carers_for_area(self):
        """Returns all carers within the area who are available in the time interval"""

        # given
        area = Area(key='test_key')
        begin = datetime.date(2017, 2, 1)
        end = datetime.date(2017, 2, 7)

        # when
        carers = self.data_source.get_carers_for_area(area, begin, end)

        # then
        self.assertIsNotNone(carers)
        self.assertTrue(carers)

    def test_can_cover_visits(self):
        """Can cover every visit"""

        missed_visits = []
        for visit in self.data_source.get_visits():
            carers = self.data_source.get_carers_for_visit(visit)
            if not carers:
                missed_visits.append(visit)

        self.assertTrue(len(missed_visits) < 20)


if __name__ == '__main__':
    unittest.main()
