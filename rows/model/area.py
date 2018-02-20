"""Details area"""

import rows.model.object


class Area(rows.model.object.DatabaseObject):
    """Area of operations and management"""

    CODE = 'code'

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Area, self).__init__(**kwargs)

        self.__code = kwargs.get(Area.CODE)

    @property
    def code(self):
        """Get area code"""

        return self.__code

    def as_dict(self):
        """Returns object as dictionary"""

        bundle = super(Area, self).as_dict()
        bundle[Area.CODE] = self.__code
        return bundle

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        return Area(**json_obj)
