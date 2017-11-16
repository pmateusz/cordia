"""Test finding geographic coordinates given an address"""

import unittest

from rows.location_finder import Coordinates, LocationFinder
from rows.address import Address


class TestLocationFinder(unittest.TestCase):
    """Test finding geographic coordinates given an address"""

    def test_location_found(self):
        """should find coordinates of an exact location"""

        address = Address(house_number=75, road='Montrose Street', city='Glasgow')
        location_service = LocationFinder()

        result = location_service.find(address)

        self.assertFalse(result.is_faulted)
        self.assertIsNotNone(result.address)
        self.assertEqual(result.coordinates, Coordinates(longitude='-4.245461', latitude='55.862235'))

    def test_multiple_locations_found(self):
        """should find multiple coordinates of a vogue location"""
        self.fail()

    def test_location_not_found(self):
        """should handle situations where location was not found"""
        self.fail()

    def test_service_not_available(self):
        """should handle situations where network service is not available"""
        self.fail()

    def test_network_not_available(self):
        """should handle situations where network is not available"""
        self.fail()

    def test_network_timeout(self):
        """should handle situations where network service did not return result within a certain timeout"""
        self.fail()


if __name__ == '__main__':
    unittest.main()
