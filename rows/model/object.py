"""Base class for Plain Old Data Object"""

import collections


class DataObject:
    """Base class for Plain Old Data Object"""

    def __eq__(self, other):
        return self.__class__ == other.__class__ and self.as_dict() == other.as_dict()

    def __hash__(self):
        return hash(tuple(self.as_dict().values()))

    def __str__(self):
        return self.as_dict().__str__()

    def __repr__(self):
        return self.as_dict().__repr__()

    @staticmethod
    def as_dict():
        """Returns object as dictionary"""

        return collections.OrderedDict()


class DatabaseObject(DataObject):
    """Base class for Plain Old Data Objects stored in database"""

    KEY = 'key'

    def __init__(self, **kwargs):
        self.__key = kwargs.get(DatabaseObject.KEY, None)

    def as_dict(self):
        """Returns object as dictionary"""

        bundle = super(DatabaseObject, self).as_dict()
        bundle[DatabaseObject.KEY] = self.__key
        return bundle

    @property
    def key(self):
        """Returns a property"""

        return self.__key
