class HistoricalVisit:

    def __init__(self, **kwargs):
        self.__visit = kwargs.get('visit', None)
        self.__service_user = kwargs.get('service_user', None)
        self.__planned_check_in = kwargs.get('planned_check_in', None)
        self.__planned_check_out = kwargs.get('planned_check_out', None)
        self.__planned_duration = kwargs.get('planned_duration', None)
        self.__real_check_in = kwargs.get('real_check_in', None)
        self.__real_check_out = kwargs.get('real_check_out', None)
        self.__real_duration = kwargs.get('real_duration', None)
        self.__tasks = kwargs.get('tasks', [])
        self.__carer_count = kwargs.get('carer_count', 1)

    def as_dict(self):
        return {'visit': self.visit,
                'service_user': self.service_user,
                'planned_check_in': self.planned_check_in,
                'planned_check_out': self.planned_check_in,
                'planned_duration': self.planned_duration,
                'real_check_in': self.real_check_in,
                'real_check_out': self.real_check_out,
                'real_duration': self.real_duration,
                'tasks': self.tasks,
                'carer_count': self.carer_count}

    @property
    def visit(self):
        return self.__visit

    @property
    def service_user(self):
        return self.__service_user

    @property
    def planned_check_in(self):
        return self.__planned_check_in

    @property
    def planned_check_out(self):
        return self.__planned_check_out

    @property
    def planned_duration(self):
        return self.__planned_duration

    @planned_duration.setter
    def planned_duration(self, value):
        self.__planned_duration = value

    @property
    def real_check_in(self):
        return self.__real_check_in

    @property
    def real_check_out(self):
        return self.__real_check_out

    @property
    def real_duration(self):
        return self.__real_duration

    @real_duration.setter
    def real_duration(self, value):
        self.__real_duration = value

    @property
    def tasks(self):
        return self.__tasks

    @property
    def carer_count(self):
        return self.__carer_count

    @carer_count.setter
    def carer_count(self, carer_count):
        self.__carer_count = carer_count
