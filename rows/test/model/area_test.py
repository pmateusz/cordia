"""Test area"""

import collections
import unittest

from rows.model.area import Area


class TestArea(unittest.TestCase):
    """Test area"""

    EXAMPLE_KEY = '1234567'

    def setUp(self):
        self.example_area = Area(key=TestArea.EXAMPLE_KEY)

    def test_example_area(self):
        """Can access properties and serialize to dictionary"""

        self.assertEqual(self.example_area.key, TestArea.EXAMPLE_KEY)
        self.assertEqual(self.example_area.as_dict(), collections.OrderedDict(key=TestArea.EXAMPLE_KEY))

    def test_dictionary_serialization(self):
        """Can deserialize from a dictionary"""

        actual_area = Area(**self.example_area.as_dict())

        self.assertEqual(actual_area, self.example_area)


if __name__ == '__main__':
    unittest.main()
