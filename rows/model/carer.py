"""Details an employee who can perform a visit"""


class Carer:
    """Details an employee who can perform a visit"""

    KEY = 'id'

    def __init__(self, **kwargs):
        self.__key = kwargs.get(Carer.KEY, None)

    def __eq__(self, other):
        if not isinstance(other, Carer):
            return False

        return self.__key == other.key

    def __hash__(self):
        return hash(self.tuple())

    def __str__(self):
        return self.dict().__str__()

    def __repr__(self):
        return self.dict().__repr__()

    def tuple(self):
        """Returns object as tuple"""
        return self.__key,

    def dict(self):
        """Returns object as dictionary"""
        return {Carer.KEY: self.__key}

    @property
    def key(self):
        """Get a property"""

        return self.__key

    @staticmethod
    def from_json(json_obj):
        """Converts object from dictionary"""

        return Carer(**json_obj)
