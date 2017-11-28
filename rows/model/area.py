"""Details area"""

import rows.model.plain_object


class Area(rows.model.plain_object.PlainOldDatabaseObject):
    """Area of operations and management"""

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Area, self).__init__(**kwargs)

    @staticmethod
    def from_json(json_obj):
        """Deserialize object from dictionary"""

        return Area(**json_obj)
