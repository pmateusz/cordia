"""Test carer"""

import collections
import unittest

from rows.model.carer import Carer


class TestCarer(unittest.TestCase):
    """Test carer"""

    EXAMPLE_KEY = '1234567'

    def setUp(self):
        self.example_carer = Carer(key=TestCarer.EXAMPLE_KEY)

    def test_example_carer(self):
        """Can access properties and serialize to dictionary"""

        self.assertEqual(self.example_carer.key, TestCarer.EXAMPLE_KEY)
        self.assertEqual(self.example_carer.as_dict(), collections.OrderedDict(key=TestCarer.EXAMPLE_KEY))

    def test_dictionary_serialization(self):
        """Can deserialize from a dictionary"""

        actual = Carer(**self.example_carer.as_dict())

        self.assertEqual(actual, self.example_carer)


if __name__ == '__main__':
    unittest.main()
