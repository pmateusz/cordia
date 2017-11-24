"""Details a requested visit"""


class Visit:
    """Details a requested visit"""

    ID = 'id'

    def __init__(self, **kwargs):
        self.__id = kwargs.get(Visit.ID, None)

    def __eq__(self, other):
        if not isinstance(other, Visit):
            return False

        return self.__id == other.id

    def __hash__(self):
        return hash(self.tuple())

    def __str__(self):
        return self.dict().__str__()

    def __repr__(self):
        return self.dict().__repr__()

    def tuple(self):
        """Returns object as tuple"""
        return self.__id,

    def dict(self):
        """Returns object as dictionary"""
        return {Visit.ID: self.__id}

    @property
    def id(self):
        """Get a property"""

        return self.__id
