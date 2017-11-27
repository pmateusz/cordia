"""Details a requested visit"""

import rows.model.data_object


class Visit(rows.model.data_object.DataObject):  # pylint: disable=too-few-public-methods
    """Details a requested visit"""

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Visit, self).__init__(**kwargs)

    def __eq__(self, other):
        return isinstance(other, Visit) and super(Visit, self).__eq__(other)

    @staticmethod
    def from_json(json_obj):
        """Create object from dictionary"""

        return Visit(**json_obj)
