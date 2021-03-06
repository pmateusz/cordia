import unittest

from rows.model.address import Address


class TestAddress(unittest.TestCase):

    def test_simple_address(self):
        expected_address = Address(house_number='44', city='Glasgow', road='James Weir Avenue', post_code='G199 ODR')

        for text in ['0/2, 44, James Weir Avenue, Glasgow, G199 ODR',
                     '., 44, James Weir Avenue, Glasgow, G199 ODR',
                     'Flat "O" alpha not numeral , 44, James Weir Avenue, Glasgow, G199 ODR',
                     '., 44, James Weir Avenue, Glasgow, G199 ODR',
                     '0/1, 44, James Weir Avenue, Glasgow, G199 ODR',
                     '., 44, James Weir Avenue, Glasgow, G199 ODR',
                     '0, 44, James Weir Avenue, Glasgow, G199 ODR',
                     '44 James Weir Avenue, Glasgow, G199 ODR',
                     '0/2, 44 James Weir Avenue, Glasgow, G199 ODR',
                     'Flat 3/3, The Observatory, 44 James Weir Avenue, ..., ..., ..., Glasgow, G199 ODR']:
            actual_address = Address.parse(text)
            self.assertEqual(expected_address, actual_address)
