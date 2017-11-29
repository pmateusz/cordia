"""Details an employee who can perform a visit"""

import rows.model.plain_object


class Carer(rows.model.plain_object.PlainOldDatabaseObject):
    """Details an employee who can perform a visit"""

    SAP_NUMBER = 'sap_number'
    ADDRESS = 'address'
    POSITION = 'position'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Carer, self).__init__(**kwargs)

        self.__sap_number = kwargs.get(Carer.SAP_NUMBER, None)
        self.__address = kwargs.get(Carer.ADDRESS, None)
        self.__position = kwargs.get(Carer.POSITION, None)

    def as_dict(self):
        bundle = super(Carer, self).as_dict()
        bundle[Carer.SAP_NUMBER] = self.__sap_number
        bundle[Carer.POSITION] = self.__position
        bundle[Carer.ADDRESS] = self.__address
        return bundle

    @staticmethod
    def from_json(json_obj):
        """Converts object from dictionary"""

        return Carer(**json_obj)

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
