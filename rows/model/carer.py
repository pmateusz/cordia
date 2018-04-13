"""Details an employee who can perform a visit"""

import rows.model.object
from rows.model.address import Address


class Carer(rows.model.object.DatabaseObject):
    """Details an employee who can perform a visit"""

    SAP_NUMBER = 'sap_number'
    ADDRESS = 'address'
    POSITION = 'position'
    EXTERNAL = 'external'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Carer, self).__init__(**kwargs)

        self.__sap_number = kwargs.get(Carer.SAP_NUMBER, None)
        self.__address = kwargs.get(Carer.ADDRESS, None)
        self.__position = kwargs.get(Carer.POSITION, None)
        self.__external = kwargs.get(Carer.EXTERNAL, False)

    def as_dict(self):
        bundle = super(Carer, self).as_dict()
        bundle[Carer.SAP_NUMBER] = self.__sap_number
        bundle[Carer.POSITION] = self.__position
        bundle[Carer.ADDRESS] = self.__address
        bundle[Carer.EXTERNAL] = self.__external
        return bundle

    @staticmethod
    def from_json(json):
        """Converts object from dictionary"""

        address_json = json.get(Carer.ADDRESS, None)
        address = Address(**address_json) if address_json else None
        return Carer(**{
            Carer.KEY: json.get(Carer.KEY),
            Carer.ADDRESS: address,
            Carer.POSITION: json.get(Carer.POSITION),
            Carer.SAP_NUMBER: json.get(Carer.SAP_NUMBER),
            Carer.EXTERNAL: json.get(Carer.EXTERNAL)
        })

    @property
    def sap_number(self):
        """Return a property"""

        return self.__sap_number

    @property
    def address(self):
        """Return a property"""

        return self.__address

    @property
    def position(self):
        """Return a property"""

        return self.__position

    @property
    def external(self):
        """Return a property"""

        return self.__external
