"""Test visit"""

import collections
import unittest

from rows.model.visit import Visit


class TestVisit(unittest.TestCase):
    """Test carer"""

    EXAMPLE_KEY = '1234567'

    def setUp(self):
        self.example_visit = Visit(key=TestVisit.EXAMPLE_KEY)

    def test_example_visit(self):
        """Can access properties and serialize to dictionary"""

        self.assertEqual(self.example_visit.key, TestVisit.EXAMPLE_KEY)
        self.assertEqual(self.example_visit.as_dict(), collections.OrderedDict(key=TestVisit.EXAMPLE_KEY))

    def test_dictionary_serialization(self):
        """Can deserialize from a dictionary"""

        actual = Visit(**self.example_visit.as_dict())

        self.assertEqual(actual, self.example_visit)


if __name__ == '__main__':
    unittest.main()
