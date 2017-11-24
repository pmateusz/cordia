"""Test carer"""

import unittest

from rows.model.carer import Carer


class TestCarer(unittest.TestCase):
    """Test carer"""

    def test_dictionary_serialization(self):
        """Can serialize a carer to a dictionary"""
        expected = Carer(id='1234567')

        actual = Carer(**expected.dict())

        self.assertEqual(actual, expected)


if __name__ == '__main__':
    unittest.main()
