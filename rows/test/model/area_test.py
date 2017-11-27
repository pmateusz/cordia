"""Test area"""

import unittest

from rows.model.area import Area


class TestArea(unittest.TestCase):
    """Test area"""

    def test_dictionary_serialization(self):
        """Can serialize an area to a dictionary"""
        expected = Area(id='1234567')

        actual = Area(**expected.__dict__)

        self.assertEqual(actual, expected)


if __name__ == '__main__':
    unittest.main()
