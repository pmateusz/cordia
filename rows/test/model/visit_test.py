"""Test visit"""

import unittest

from rows.model.visit import Visit


class TestVisit(unittest.TestCase):
    """Test carer"""

    def test_dictionary_serialization(self):
        """Can serialize a visit to a dictionary"""
        expected = Visit(id='1234567')

        actual = Visit(**expected.dict())

        self.assertEqual(actual, expected)


if __name__ == '__main__':
    unittest.main()
