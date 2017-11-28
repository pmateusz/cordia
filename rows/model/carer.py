"""Details an employee who can perform a visit"""

import rows.model.plain_object


class Carer(rows.model.plain_object.PlainOldDatabaseObject):
    """Details an employee who can perform a visit"""

    def __init__(self, **kwargs):  # pylint: disable=useless-super-delegation
        super(Carer, self).__init__(**kwargs)

    @staticmethod
    def from_json(json_obj):
        """Converts object from dictionary"""

        return Carer(**json_obj)
