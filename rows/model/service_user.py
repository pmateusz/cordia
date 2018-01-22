"""Details a service user who is being visited"""

import rows.model.object

from rows.model.address import Address
from rows.model.location import Location


class ServiceUser(rows.model.object.DatabaseObject):
    """Details a service user who is being visited"""

    LOCATION = 'location'
    ADDRESS = 'address'
    CARER_PREFERENCE = 'carer_preference'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(ServiceUser, self).__init__(**kwargs)

        self.__address = kwargs.get(ServiceUser.ADDRESS, None)
        self.__location = kwargs.get(ServiceUser.LOCATION, None)
        self.__carer_preference = kwargs.get(ServiceUser.CARER_PREFERENCE, None)

    def as_dict(self):
        bundle = super(ServiceUser, self).as_dict()
        bundle[ServiceUser.LOCATION] = self.__location
        bundle[ServiceUser.ADDRESS] = self.__address
        bundle[ServiceUser.CARER_PREFERENCE] = self.__carer_preference
        return bundle

    @staticmethod
    def from_json(json):
        """Converts object from dictionary"""

        address_json = json.get(ServiceUser.ADDRESS, None)
        address = Address(**address_json) if address_json else None

        location_json = json.get(ServiceUser.LOCATION, None)
        location = Location(**location_json)

        return ServiceUser(**{
            ServiceUser.KEY: json.get(ServiceUser.KEY),
            ServiceUser.LOCATION: location,
            ServiceUser.ADDRESS: address,
            ServiceUser.CARER_PREFERENCE: json.get(ServiceUser.CARER_PREFERENCE, None)
        })

    @property
    def carer_preference(self):
        """Return a property"""

        return self.__carer_preference

    @property
    def address(self):
        """Return a property"""

        return self.__address

    @property
    def location(self):
        """Return a property"""

        return self.__location
