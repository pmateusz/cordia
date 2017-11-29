"""Test carer"""

import collections
import unittest

from rows.model.address import Address
from rows.model.carer import Carer


class TestCarer(unittest.TestCase):
    """Test carer"""

    EXAMPLE_SAP_NUMBER = '1234567'
    EXAMPLE_POST_CODE = 'G2 3JU'
    EXAMPLE_POSITION = 'Senior Carer'

    def setUp(self):
        self.example_address = Address(post_code=TestCarer.EXAMPLE_POST_CODE)
        self.example_carer = Carer(position=TestCarer.EXAMPLE_POSITION,
                                   address=self.example_address,
                                   sap_number=TestCarer.EXAMPLE_SAP_NUMBER)

    def test_csv_carer(self):
        """Can access properties and serialize to dictionary"""

        self.assertIsNone(self.example_carer.key)
        self.assertEqual(self.example_carer.position, TestCarer.EXAMPLE_POSITION)
        self.assertEqual(self.example_carer.sap_number, TestCarer.EXAMPLE_SAP_NUMBER)
        self.assertEqual(self.example_carer.address, self.example_address)
        self.assertEqual(self.example_carer.as_dict(),
                         collections.OrderedDict([('key', None),
                                                  ('sap_number', TestCarer.EXAMPLE_SAP_NUMBER),
                                                  ('position', TestCarer.EXAMPLE_POSITION),
                                                  ('address', self.example_address)]))

    def test_dictionary_serialization(self):
        """Can deserialize from a dictionary"""

        actual = Carer(**self.example_carer.as_dict())

        self.assertEqual(actual, self.example_carer)


if __name__ == '__main__':
    unittest.main()
