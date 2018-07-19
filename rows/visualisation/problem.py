import bs4
import os
import sys
import re
import datetime
import time
from distutils.util import strtobool

class Carer:
    def __init__(self, id, sap_number,dropped):
        self.id = id
        self.dropped=dropped
        self.sap_number = sap_number
        self.visits = []
        self.work_service_time="00:00:00"
        self.work_travel_time="00:00:00"
        self.work_idle_time="00:00:00"

    def set_work_param(self, work_relative, work_total_time, work_available_time, work_service_time, work_travel_time, work_idle_time, work_visits_count):
        self.work_relative = work_relative
        self.work_total_time = work_total_time
        self.work_available_time = work_available_time
        self.work_service_time = work_service_time
        self.work_travel_time = work_travel_time
        self.work_idle_time = work_idle_time
        self.work_visits_count = work_visits_count

    def append_visit(self,visit):
        self.visits.append(visit)

    def set_style(self,style):
        self.style =  style

    def get_starttime(self):
        return self.visits[0].start_time

    def get_endtime(self):
        if len(self.visits)>0:
            x = self.visits[-1]
            start_date_time = time.strptime(x.start_time,'%Y-%b-%d %H:%M:%S')
            duration = time.strptime(x.duration,'%H:%M:%S')
            end_date_time = datetime.datetime.fromtimestamp(time.mktime(start_date_time)) + datetime.timedelta(hours=duration[3], minutes=duration[4], seconds=duration[5])

            return end_date_time.strftime("%Y-%b-%d %H:%M:%S")
    
    def sort_visits(self):
        if len(self.visits)>0:
            self.visits.sort(key=lambda x: time.mktime(time.strptime(x.start_time,'%Y-%b-%d %H:%M:%S')), reverse=False)

class Visit:
    def __init__(self, dropped):
        self.dropped = dropped
        self.number_of_carers_needed = 1.0

    def set_dynamicparam(self, assigned_carer, satisfaction):
        self.assigned_carer = assigned_carer
        self.satisfaction = satisfaction

    def set_staticparam(self, lon, lat, user, start_time, duration):
        self.lon = lon
        self.lat = lat
        self.user = user
        self.start_time = start_time
        self.duration = duration

    def set_number_of_carers_needed(self,ncarers):
        self.number_of_carers_needed = ncarers

    def is_multiple_carers(self):
        if self.number_of_carers_needed > 1:
            return True
        else:
            return False

def load_problem(filepath):
    with open(filepath, 'r') as fp:
        soup = bs4.BeautifulSoup(fp, "html5lib")

        attributes = {}
        for node in soup.find_all('attribute'):
            attributes[node['title']] = node['id']

        type_id = attributes['type']
        id_id = attributes['id']

        #for carers
        sap_number_id = attributes['sap_number']
        work_relative = attributes['work_relative']
        work_total_time = attributes['work_total_time']
        work_available_time = attributes['work_available_time']
        work_service_time = attributes['work_service_time']
        work_travel_time = attributes['work_travel_time']
        work_idle_time = attributes['work_idle_time']
        work_visits_count = attributes['work_visits_count']

        #for visits
        assigned_carer_id = attributes['assigned_carer']	
        dropped = attributes['dropped']
        lon = attributes['longitude']
        lat = attributes['latitude']
        satisfaction = attributes['satisfaction']
        user = attributes['user']
        start_time = attributes['start_time']
        duration = attributes['duration']

        carers = []
        visits = []
        for node in soup.find_all('node'):
            attributes = node.find('attvalues')
            type_attr = attributes.find('attvalue', attrs={'for': type_id})
            if type_attr['value'] == 'carer':
                id_attr = attributes.find('attvalue', attrs={'for': id_id})
                dropped_attr = attributes.find('attvalue', attrs={'for': dropped})
                sap_number_attr = attributes.find('attvalue', attrs={'for': sap_number_id})
                work_relative_attr  = attributes.find('attvalue', attrs={'for': work_relative})
                work_total_time_attr  = attributes.find('attvalue', attrs={'for': work_total_time})
                work_available_time_attr  = attributes.find('attvalue', attrs={'for': work_available_time})
                work_service_time_attr  = attributes.find('attvalue', attrs={'for': work_service_time})
                work_travel_time_attr  = attributes.find('attvalue', attrs={'for': work_travel_time})
                work_idle_time_attr  = attributes.find('attvalue', attrs={'for': work_idle_time})
                work_visits_count_attr  = attributes.find('attvalue', attrs={'for': work_visits_count})

                carer = Carer(id_attr['value'], sap_number_attr['value'],dropped_attr['value']) if dropped_attr else Carer(id_attr['value'], sap_number_attr['value'],"False")
                if not strtobool(carer.dropped):
                    carer.set_work_param(work_relative_attr['value'], work_total_time_attr['value'], work_available_time_attr['value'], work_service_time_attr['value'], work_travel_time_attr['value'], work_idle_time_attr['value'], work_visits_count_attr['value'])
                carers.append(carer)
                
            elif type_attr['value'] == 'visit':
                dropped_attr = attributes.find('attvalue', attrs={'for': dropped})
                lon_attr = attributes.find('attvalue', attrs={'for': lon})
                lat_attr = attributes.find('attvalue', attrs={'for': lat})
                user_attr = attributes.find('attvalue', attrs={'for': user})
                start_time_attr = attributes.find('attvalue', attrs={'for': start_time})
                duration_attr = attributes.find('attvalue', attrs={'for': duration})                		
                visit = Visit(dropped_attr['value']) if dropped_attr else Visit("False")		
                visit.set_staticparam(lon_attr['value'], lat_attr['value'], user_attr['value'], start_time_attr['value'], duration_attr['value'])		
                if not strtobool(visit.dropped):
                    assigned_carer_attr = attributes.find('attvalue', attrs={'for': assigned_carer_id})
                    satisfaction_attr = attributes.find('attvalue', attrs={'for': satisfaction})
                    visit.set_dynamicparam(assigned_carer_attr['value'], satisfaction_attr['value'])
                visits.append(visit)

        for visit in visits:
             if not strtobool(visit.dropped):
                sap_number = visit.assigned_carer
                for carer in carers:
                    if carer.sap_number == sap_number:
                        carer.append_visit(visit)

        #count number of carers needed
        count = 0
        for visit1 in visits:
            for visit2 in visits:
                if visit1.user == visit2.user and visit1.start_time == visit2.start_time:
                    count = count+1
            visit1.set_number_of_carers_needed(count)
            count = 0

        return carers, visits
