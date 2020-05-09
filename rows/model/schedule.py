"""Details visits to carers assignments"""

import collections
import datetime
import operator
import re
import typing

import rows.model.carer
import rows.model.datetime
import rows.model.location
import rows.model.metadata
import rows.model.object
import rows.model.past_visit
import rows.model.rest
import rows.model.service_user
import rows.model.visit


class Schedule(rows.model.object.DataObject):
    """Details visits to carers assignments"""

    METADATA = 'metadata'
    VISITS = 'visits'
    ROUTES = 'routes'

    class Route:

        CARER = 'carer'
        NODES = 'nodes'

        def __init__(self, **kwargs):
            self.__carer = kwargs.get(self.CARER, None)
            self.__nodes = kwargs.get(self.NODES, None)

        def edges(self):
            if not self.__nodes:
                return []

            result = []
            visit_it = iter(self.visits)
            prev_visit = next(visit_it)
            for current_visit in visit_it:
                result.append((prev_visit, current_visit))
                prev_visit = current_visit
            return result

        @property
        def carer(self) -> rows.model.carer.Carer:
            return self.__carer

        @property
        def nodes(self) -> typing.List:
            return self.__nodes

        @property
        def visits(self) -> typing.List[rows.model.past_visit.PastVisit]:
            return [node for node in self.__nodes if isinstance(node, rows.model.past_visit.PastVisit)]

    def __init__(self, **kwargs):
        self.__metadata = kwargs.get(Schedule.METADATA, None)
        self.__visits = kwargs.get(Schedule.VISITS, [])
        self.__routes = kwargs.get(Schedule.ROUTES, [])

        if not self.__routes:
            past_visits_by_carer = collections.defaultdict(list)
            for past_visit in self.__visits:
                past_visits_by_carer[past_visit.carer].append(past_visit)
            for carer in past_visits_by_carer:
                past_visits_by_carer[carer].sort(key=operator.attrgetter('check_in'))
            self.__routes = [Schedule.Route(carer=carer, nodes=past_visits_by_carer[carer]) for carer in past_visits_by_carer]

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

    @property
    def metadata(self) -> rows.model.metadata.Metadata:
        """Get a property"""

        return self.__metadata

    @property
    def routes(self) -> typing.List[Route]:
        return self.__routes

    @property
    def visits(self) -> typing.List[rows.model.past_visit.PastVisit]:
        """Get a property"""

        return self.__visits

    @property
    def carers(self) -> typing.List[rows.model.carer.Carer]:
        unique_carers = set()
        for visit in self.visits:
            unique_carers.add(visit.carer)
        return list(unique_carers)

    @property
    def date(self) -> datetime.date:
        dates = {visit.date for visit in self.__visits}

        assert len(dates) == 1

        return next(iter(dates))

    @staticmethod
    def from_json(schedule_json):
        metadata = None
        if Schedule.METADATA in schedule_json:
            metadata = rows.model.metadata.Metadata.from_json(schedule_json[Schedule.METADATA])

        past_visits = []
        if Schedule.VISITS in schedule_json:
            past_visits = [rows.model.past_visit.PastVisit.from_json(raw_visit)
                           for raw_visit in schedule_json[Schedule.VISITS]]
        past_visits.sort(key=operator.attrgetter('time'))

        return Schedule(metadata=metadata, visits=past_visits)

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
        assigned_carer_id = attributes['assigned_carer']

        carers_by_id = {}
        users_by_id = {}
        visits_by_id = {}
        breaks_by_id = {}

        def parse_skills(attr_container) -> typing.List[int]:
            skills_text = [text for text in attr_container['value'].split(';') if text]
            if skills_text:
                return list(map(int, skills_text))
            return list()

        break_counter = collections.Counter()
        for node in schedule_soup.find_all('node'):
            attributes = node.find('attvalues')
            type_attr = attributes.find('attvalue', attrs={'for': type_id})
            if type_attr['value'] == 'carer':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                sap_number_attr = attributes.find('attvalue', attrs={'for': sap_number_id})
                skills_number_attr = attributes.find('attvalue', attrs={'for': skills_id})
                carers_by_id[node['id']] = rows.model.carer.Carer(key=int(id_number_attr['value']),
                                                                  sap_number=sap_number_attr['value'],
                                                                  skills=parse_skills(skills_number_attr))
            elif type_attr['value'] == 'user':
                id_number_attr = attributes.find('attvalue', attrs={'for': id_id})
                longitude_attr = attributes.find('attvalue', attrs={'for': longitude_id})
                latitude_attr = attributes.find('attvalue', attrs={'for': latitude_id})
                key = int(id_number_attr['value'])
                user = rows.model.service_user.ServiceUser(key=key,
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
                service_user = int(user_attr['value'])
                start_time = rows.model.datetime.try_parse_datetime(start_time_attr['value'])
                duration = rows.model.datetime.try_parse_duration(duration_attr['value'])
                assert start_time is not None and duration is not None

                tasks_number_attr = attributes.find('attvalue', attrs={'for': tasks_id})
                visits_by_id[node['id']] = rows.model.visit.Visit(key=key,
                                                                  date=start_time.date(),
                                                                  time=start_time.time(),
                                                                  duration=duration,
                                                                  service_user=service_user,
                                                                  tasks=parse_skills(tasks_number_attr))
            elif type_attr['value'] == 'break':
                assigned_carer_attr = attributes.find('attvalue', attrs={'for': assigned_carer_id})
                start_time_attr = attributes.find('attvalue', attrs={'for': start_time_id})
                duration_attr = attributes.find('attvalue', attrs={'for': duration_id})

                assigned_carer_key = int(assigned_carer_attr['value'])
                carer_key = None
                for carer_id in carers_by_id:
                    if carers_by_id[carer_id].key == assigned_carer_key:
                        carer_key = carer_id
                        break
                assert carer_key is not None

                break_counter[carer_key] += 1

                break_id = '{0}_b{1}'.format(carer_key, break_counter[carer_key])
                start_time = rows.model.datetime.try_parse_datetime(start_time_attr['value'])
                duration = rows.model.datetime.try_parse_duration(duration_attr['value'])
                assert start_time is not None and duration is not None

                breaks_by_id[break_id] = rows.model.rest.Rest(id=break_id, carer=assigned_carer_key, start_time=start_time, duration=duration)

        carer_id_pattern = re.compile('^c\d+$')
        visit_id_pattern = re.compile('^v\d+$')
        break_id_pattern = re.compile('^c\d+_b\d+$')

        def is_carer(id: str) -> bool:
            pattern = carer_id_pattern.match(id)
            return pattern is not None

        def is_visit(id: str) -> bool:
            pattern = visit_id_pattern.match(id)
            return pattern is not None

        def is_break(id: str) -> bool:
            pattern = break_id_pattern.match(id)
            return pattern is not None

        edges = {edge['source']: edge['target'] for edge in schedule_soup.find_all('edge')}
        raw_routes = {}
        for source_id in edges:
            if not is_carer(source_id):
                continue

            carer = carers_by_id[source_id]
            visits_and_breaks = []

            current_id = source_id
            while current_id in edges:
                next_id = edges[current_id]

                if is_visit(next_id):
                    visit = visits_by_id[next_id]
                    visits_and_breaks.append(visit)
                elif is_break(next_id):
                    rest = breaks_by_id[next_id]
                    visits_and_breaks.append(rest)
                else:
                    assert False

                current_id = next_id

            assert carer not in raw_routes
            raw_routes[carer] = visits_and_breaks

        past_visits = []
        routes = []
        for carer in raw_routes:
            carer_route = []
            for work_item in raw_routes[carer]:
                if isinstance(work_item, rows.model.visit.Visit):
                    check_in = datetime.datetime.combine(work_item.date, work_item.time)
                    check_out = datetime.datetime.combine(work_item.date, work_item.time) + work_item.duration
                    past_visit = rows.model.past_visit.PastVisit(visit=work_item,
                                                                 date=work_item.date,
                                                                 time=work_item.time,
                                                                 duration=work_item.duration,
                                                                 carer=carer,
                                                                 check_in=check_in,
                                                                 check_out=check_out)
                    past_visits.append(past_visit)
                    carer_route.append(past_visit)
                elif isinstance(work_item, rows.model.rest.Rest):
                    carer_route.append(work_item)
            routes.append(Schedule.Route(carer=carer, nodes=carer_route))

        past_visits.sort(key=operator.attrgetter('time'))
        return Schedule(metadata=rows.model.metadata.Metadata(begin=min(past_visits, key=operator.attrgetter('date')).date,
                                                              end=max(past_visits, key=operator.attrgetter('date')).date),
                        visits=past_visits,
                        routes=routes)
