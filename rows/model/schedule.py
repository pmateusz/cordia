"""Details visits to carers assignments"""

import collections
import datetime
import operator

import rows.model.carer
import rows.model.location
import rows.model.metadata
import rows.model.object
import rows.model.past_visit
import rows.model.service_user
import rows.model.visit


class Schedule(rows.model.object.DataObject):
    """Details visits to carers assignments"""

    METADATA = 'metadata'
    VISITS = 'visits'

    class Route:

        CARER = 'carer'
        VISITS = 'visits'

        def __init__(self, **kwargs):
            self.__carer = kwargs.get(self.CARER, None)
            self.__visits = kwargs.get(self.VISITS, None)

        def edges(self):
            if not self.__visits:
                return []

            result = []
            visit_it = iter(self.__visits)
            prev_visit = next(visit_it)
            for current_visit in visit_it:
                result.append((prev_visit, current_visit))
                prev_visit = current_visit
            return result

        @property
        def carer(self):
            return self.__carer

        @property
        def visits(self):
            return self.__visits

    def __init__(self, **kwargs):
        self.__metadata = kwargs.get(Schedule.METADATA, None)
        self.__visits = kwargs.get(Schedule.VISITS, [])

    def __hash__(self):
        if self.__visits:
            return hash(tuple((hash(visit) for visit in self.__visits)))
        return 0

    def as_dict(self):
        bundle = super(Schedule, self).as_dict()

        if self.__metadata:
            bundle[Schedule.METADATA] = self.__metadata

        if self.__visits:
            bundle[Schedule.VISITS] = self.__visits

        return bundle

    def routes(self):
        routes = collections.defaultdict(list)
        for visit in self.visits:
            routes[visit.carer].append(visit)
        for carer in routes:
            routes[carer].sort(key=operator.attrgetter('time'))
        return [Schedule.Route(carer=carer, visits=visits) for carer, visits in routes.items()]

    @property
    def metadata(self):
        """Get a property"""

        return self.__metadata

    @property
    def visits(self):
        """Get a property"""

        return self.__visits

    def carers(self):
        unique_carers = set()
        for visit in self.visits:
            unique_carers.add(visit.carer)
        return list(unique_carers)

    def date(self) -> datetime.date:
        dates = {visit.date for visit in self.__visits}

        assert len(dates) == 1

        return next(iter(dates))

    @staticmethod
    def from_json(schedule_json):
        metadata = None
        if Schedule.METADATA in schedule_json:
            metadata = rows.model.metadata.Metadata.from_json(schedule_json[Schedule.METADATA])
        visits = None
        if Schedule.VISITS in schedule_json:
            visits = [rows.model.past_visit.PastVisit.from_json(raw_visit)
                      for raw_visit in schedule_json[Schedule.VISITS]]
        return Schedule(metadata=metadata, visits=visits)

    @staticmethod
    def from_gexf(schedule_soup):
        attributes = {}
        for node in schedule_soup.find_all('attribute'):
            attributes[node['title']] = node['id']

        type_id = attributes['type']
        sap_number_id = attributes['sap_number']
        id_id = attributes['id']
        skills_id = attributes['skills']
        tasks_id = attributes['tasks']
        user_id = attributes['user']
        start_time_id = attributes['start_time']
        duration_id = attributes['duration']
        longitude_id = attributes['longitude']
        latitude_id = attributes['latitude']

        carers_by_id = {}
        users_by_id = {}
        visits_by_id = {}
        for node in schedule_soup.find_all('node'):
            attributes = node.find('attvalues')
            type_attr = attributes.find('attvalue', attrs={'for': type_id})
            if type_attr['value'] == 'carer':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                sap_number_attr = attributes.find('attvalue', attrs={'for': sap_number_id})
                skills_number_attr = attributes.find('attvalue', attrs={'for': skills_id})
                skills = list(map(int, skills_number_attr['value'].split(';')))
                carers_by_id[node['id']] = rows.model.carer.Carer(key=id_number_attr['value'],
                                                                  sap_number=sap_number_attr['value'],
                                                                  skills=skills)
            elif type_attr['value'] == 'user':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                longitude_attr = attributes.find('attvalue', attrs={'for': longitude_id})
                latitude_attr = attributes.find('attvalue', attrs={'for': latitude_id})
                user = rows.model.service_user.ServiceUser(key=id_number_attr['value'],
                                                           location=rows.model.location.Location(
                                                               latitude=latitude_attr['value'],
                                                               longitude=longitude_attr['value']))
                users_by_id[node['id']] = user
            elif type_attr['value'] == 'visit':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                user_attr = attributes.find('attvalue', attrs={'for': user_id})
                start_time_attr = attributes.find('attvalue', attrs={'for': start_time_id})
                duration_attr = attributes.find('attvalue', attrs={'for': duration_id})
                key = int(id_number_attr['value'])
                start_time = datetime.datetime.strptime(start_time_attr['value'], '%Y-%b-%d %H:%M:%S')
                duration = datetime.datetime.strptime(duration_attr['value'], '%H:%M:%S').time()
                tasks_number_attr = attributes.find('attvalue', attrs={'for': tasks_id})
                tasks = list(map(int, tasks_number_attr['value'].split(';')))
                visits_by_id[node['id']] = rows.model.visit.Visit(key=key,
                                                                  date=start_time.date(),
                                                                  time=start_time.time(),
                                                                  duration=datetime.timedelta(hours=duration.hour,
                                                                                              minutes=duration.minute,
                                                                                              seconds=duration.second),
                                                                  service_user=int(user_attr['value']),
                                                                  tasks=tasks)

        routes = collections.defaultdict(list)
        for edge in schedule_soup.find_all('edge'):
            source_id = edge['source']
            target_id = edge['target']
            if source_id in carers_by_id and target_id in visits_by_id:
                routes[carers_by_id[source_id]].append(visits_by_id[target_id])

        past_visits = []
        for carer in routes:
            for visit in routes[carer]:
                past_visits.append(rows.model.past_visit.PastVisit(visit=visit,
                                                                   date=visit.date,
                                                                   time=visit.time,
                                                                   duration=visit.duration,
                                                                   carer=carer))
        past_visits.sort(key=operator.attrgetter('time'))
        return Schedule(metadata=rows.model.metadata.Metadata(
            begin=min(past_visits, key=operator.attrgetter('date')).date,
            end=max(past_visits, key=operator.attrgetter('date')).date),
            visits=past_visits)
