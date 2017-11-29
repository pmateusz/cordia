"""Test visit"""

import collections
import datetime
import unittest

from rows.model.address import Address
from rows.model.visit import Visit


class TestVisit(unittest.TestCase):
    """Test carer"""

    EXAMPLE_SERVICE_USER = '1234567'
    EXAMPLE_DATE = datetime.datetime.utcnow().date()
    EXAMPLE_TIME = datetime.datetime.utcnow().time()
    EXAMPLE_DURATION = datetime.timedelta(minutes=15)
    EXAMPLE_ADDRESS = Address(road='Montrose Street', house_number='75', city='Glasgow', post_code='G1 1XJ')

    def setUp(self):
        self.example_visit = Visit(service_user=TestVisit.EXAMPLE_SERVICE_USER,
                                   date=TestVisit.EXAMPLE_DATE,
                                   time=TestVisit.EXAMPLE_TIME,
                                   duration=TestVisit.EXAMPLE_DURATION,
                                   address=TestVisit.EXAMPLE_ADDRESS)

    def test_csv_visit(self):
        """Can access properties and serialize to dictionary"""

        self.assertIsNone(self.example_visit.key)
        self.assertEqual(self.example_visit.address, TestVisit.EXAMPLE_ADDRESS)
        self.assertEqual(self.example_visit.service_user, TestVisit.EXAMPLE_SERVICE_USER)
        self.assertEqual(self.example_visit.date, TestVisit.EXAMPLE_DATE)
        self.assertEqual(self.example_visit.time, TestVisit.EXAMPLE_TIME)
        self.assertEqual(self.example_visit.duration, TestVisit.EXAMPLE_DURATION)
        self.assertEqual(self.example_visit.as_dict(),
                         collections.OrderedDict(
                             [('key', None),
                              ('service_user', TestVisit.EXAMPLE_SERVICE_USER),
                              ('address', TestVisit.EXAMPLE_ADDRESS),
                              ('date', TestVisit.EXAMPLE_DATE),
                              ('time', TestVisit.EXAMPLE_TIME),
                              ('duration', TestVisit.EXAMPLE_DURATION)]))

    def test_dictionary_serialization(self):
        """Can deserialize from a dictionary"""

        actual = Visit(**self.example_visit.as_dict())

        self.assertEqual(actual, self.example_visit)

    if __name__ == '__main__':
        unittest.main()
