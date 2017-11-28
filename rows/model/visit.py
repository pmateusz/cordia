"""Details a requested visit"""

import rows.model.plain_object


class Visit(rows.model.plain_object.PlainOldDatabaseObject):
    """Details a requested visit"""

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Visit, self).__init__(**kwargs)

    @staticmethod
    def from_json(json_obj):
        """Create object from dictionary"""

        return Visit(**json_obj)
