"""Base class for data objects"""


class PrintableObject(object): # pylint: disable=too-few-public-methods
    """Printable Object"""

    def __str__(self):
        return self.__dict__.__str__()

    def __repr__(self):
        return self.__dict__.__repr__()


class DataObject(PrintableObject): # pylint: disable=too-few-public-methods
    """Base class for data objects"""

    KEY = 'id'

    def __init__(self, **kwargs):
        self.__key = kwargs.get(DataObject.KEY, None)

    def __eq__(self, other):
        if not isinstance(other, DataObject):
            return False

        return self.__key == other.key

    def __hash__(self):
        return hash(self.tuple())

    def tuple(self):
        """Get object as tuple"""

        return tuple(self.__key)

    @property
    def key(self):
        """Get a property"""

        return self.__key
