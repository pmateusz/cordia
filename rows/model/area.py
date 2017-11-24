"""Details area"""


class Area:
    """Area of operations and management"""

    ID = 'id'

    def __init__(self, **kwargs):
        self.__id = kwargs.get(Area.ID, None)

    def __eq__(self, other):
        if not isinstance(other, Area):
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
        return {Area.ID: self.__id}

    @property
    def id(self):
        """Get a property"""

        return self.__id

    @staticmethod
    def from_json(json_obj):
        return Area(**json_obj)
