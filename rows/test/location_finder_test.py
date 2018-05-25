"""Test finding geographic coordinates given an address"""

import unittest
import unittest.mock

import requests

from rows.location_finder import AddressLocationFinder
from rows.model.address import Address
from rows.model.location import Location


class TestLocationFinder(unittest.TestCase):
    """Test finding geographic coordinates given an address"""

    def test_location_found(self):
        """should find coordinates of an exact location"""

        address = Address(house_number=75, road='Montrose Street', city='Glasgow')
        location_service = AddressLocationFinder()

        result = location_service.find(address)

        self.assertFalse(result.is_faulted)
        self.assertIsNotNone(result.result_set)
        self.assertTrue('location' in result.result_set)
        self.assertEqual(result.result_set['location'], Location(longitude='-4.245461', latitude='55.862235'))

    def test_multiple_locations_found(self):
        """should find multiple coordinates of a vogue location"""

        address = Address(house_number=1, road='Montrose Street', city='Glasgow')
        location_service = AddressLocationFinder()

        result = location_service.find(address)

        self.assertFalse(result.is_faulted)
        self.assertIsNotNone(result.result_set)
        self.assertTrue(isinstance(result.result_set, list))
        self.assertTrue(len(result.result_set) > 1)

    def test_location_not_found(self):
        """should handle situations where location was not found"""
        address = Address(house_number=99999, road='Montrose Street', city='Glasgow')
        location_service = AddressLocationFinder()

        result = location_service.find(address)

        self.assertFalse(result.is_faulted)
        self.assertTrue(isinstance(result.result_set, list))
        for row in result.result_set:
            self.assertTrue('poi' in row)
            self.assertIsNotNone(row['poi'].tags)
            for tag in row['poi'].tags:
                self.assertTrue(tag.label == 'way' or tag.label == 'highway')

    def test_network_timeout(self):
        """should handle situations where network service did not return result within a certain timeout"""
        address = Address(house_number=1, road='Montrose Street', city='Glasgow')
        location_service = AddressLocationFinder(timeout=0.01)

        result = location_service.find(address)

        self.assertTrue(result.is_faulted)
        self.assertTrue(isinstance(result.exception, requests.ConnectionError))

    def test_service_not_available(self):
        """should handle situations where network service is not available"""
        http_mock = unittest.mock.Mock(spec=requests)
        http_mock.request.side_effect = requests.ConnectionError()
        address = Address(house_number=-1, road='', city='None')

        location_service = AddressLocationFinder(http_client=http_mock)
        result = location_service.find(address)

        self.assertTrue(result.is_faulted)
        self.assertTrue(isinstance(result.exception, requests.ConnectionError))


if __name__ == '__main__':
    unittest.main()
