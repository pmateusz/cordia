"""Details area"""

import rows.model.data_object


class Area(rows.model.data_object.DataObject):
    """Area of operations and management"""

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Area, self).__init__(**kwargs)

    def __eq__(self, other):
        if isinstance(other, Area) and super(Area, self).__eq__(other):
            return True

        return False

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        return Area(**json_obj)
