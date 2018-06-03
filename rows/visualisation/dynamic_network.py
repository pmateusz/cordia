import bs4
import os
import sys
import re
import random
import datetime
import time
import math
import colorsys
import numpy as np
from distutils.util import strtobool
from simplekml import Kml, Style, Color

class Carer:
    def __init__(self, id, sap_number):
        self.id = id
        self.sap_number = sap_number
        self.visits = []

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

    def set_dynamicparam(self, assigned_carer, satisfaction):
        self.assigned_carer = assigned_carer
        self.satisfaction = satisfaction

    def set_staticparam(self, lon, lat, user, start_time, duration):
        self.lon = lon
        self.lat = lat
        self.user = user
        self.start_time = start_time
        self.duration = duration


def generate_colors(n):
    HSV_tuples = [(x*1.0/n, 0.5, 0.5) for x in range(n)]
    RGB_tuples = map(lambda x: colorsys.hsv_to_rgb(*x), HSV_tuples)
    ret = list(RGB_tuples)
    colors = [(round(x[0] * 255.0), round(x[1] * 255.0), round(x[2] * 255.0)) for x in ret]
    return colors

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
                sap_number_attr = attributes.find('attvalue', attrs={'for': sap_number_id})
                work_relative_attr  = attributes.find('attvalue', attrs={'for': work_relative})
                work_total_time_attr  = attributes.find('attvalue', attrs={'for': work_total_time})
                work_available_time_attr  = attributes.find('attvalue', attrs={'for': work_available_time})
                work_service_time_attr  = attributes.find('attvalue', attrs={'for': work_service_time})
                work_travel_time_attr  = attributes.find('attvalue', attrs={'for': work_travel_time})
                work_idle_time_attr  = attributes.find('attvalue', attrs={'for': work_idle_time})
                work_visits_count_attr  = attributes.find('attvalue', attrs={'for': work_visits_count})

                carer = Carer(id_attr['value'], sap_number_attr['value'])
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

        return carers, visits


if __name__ == '__main__':
    if len(sys.argv) <= 1:
        print('Usage: {0} solution_directory'.format(__file__), file=sys.stderr)

    problems = []
    problem_dir = sys.argv[1]
    name_pattern = re.compile("^.*?_(?P<problem_name>\w\d+)[^\d].*$")
    for file in os.listdir(problem_dir):
        if file.endswith('.gexf'):
            problem_path = os.path.join(problem_dir, file)
            problems.append((file, problem_path))

    for problem, path in problems:
        carers, visits = load_problem(path)

        #write corresponding KLM file for display in Google Earth
        kml = Kml()      
        #create palette of colors for carers and sorting carers visits
        colors = generate_colors(len(carers))
        
        i=0

        for carer in carers:
            #setting style 
            color = colors[i]
            carer_style = Style()

            carer_style.iconstyle.color = Color.rgb(color[0], color[1], color[2])
            carer_style.iconstyle.icon.href = 'http://maps.google.com/mapfiles/kml/shapes/man.png'
            carer_style.linestyle.color = Color.rgb(color[0], color[1], color[2])
            carer_style.linestyle.width = 5
            carer.set_style(carer_style)
            i=i+1
        
        # carers pins + info
        for carer in carers:
            carer.sort_visits()

            pnt = kml.newpoint(coords=[(float(carer.visits[0].lon),float(carer.visits[0].lat))]) 
            pnt.extendeddata.newdata(name='sap_number', value=carer.sap_number, displayname="SAP number")
            pnt.extendeddata.newdata(name='work_relative', value=carer.work_relative, displayname="Relative working time") 
            pnt.extendeddata.newdata(name='work_total_time', value=carer.work_total_time, displayname="Total working time") 
            pnt.extendeddata.newdata(name='work_available_time', value=carer.work_available_time, displayname="Available working time") 
            pnt.extendeddata.newdata(name='work_service_time', value=carer.work_service_time, displayname="Service working time") 
            pnt.extendeddata.newdata(name='work_travel_time', value=carer.work_travel_time, displayname="Travelling time") 
            pnt.extendeddata.newdata(name='work_idle_time', value=carer.work_idle_time, displayname="Idle time") 
            pnt.extendeddata.newdata(name='work_visits_count', value=carer.work_visits_count, displayname="Number of visits") 

            if len(carer.visits)>0:
                pnt.style.iconstyle = carer.style.iconstyle

                pnt.timespan.begin = datetime.datetime.strptime(carer.visits[0].start_time, '%Y-%b-%d %H:%M:%S').isoformat() 
                pnt.timespan.end = datetime.datetime.strptime(carer.get_endtime(), '%Y-%b-%d %H:%M:%S').isoformat() 
            else:
                pnt.style.iconstyle.color = Color.rgb(0,0,0)  

        # visits pins + info
        for visit in visits:
            pnt = kml.newpoint(coords=[(float(visit.lon),float(visit.lat))]) 
            pnt.extendeddata.newdata(name='user', value=visit.user, displayname="User ID")
            pnt.extendeddata.newdata(name='start_time', value=visit.start_time, displayname="Start time")
            pnt.extendeddata.newdata(name='duration', value=visit.duration, displayname="Duration")
            if not strtobool(visit.dropped):
                pnt.extendeddata.newdata(name='assigned_carer', value=visit.assigned_carer, displayname="Assigned carer SAP")
                pnt.extendeddata.newdata(name='satisfaction', value=visit.satisfaction, displayname="User satisfaction")
            
            if not strtobool(visit.dropped):
                for carer in carers:
                    if visit.assigned_carer == carer.sap_number:
                        carer_style = carer.style
                        break
                    end_time = carer.get_endtime()
                pnt.style.iconstyle.color = carer_style.iconstyle.color
                pnt.timespan.begin = datetime.datetime.strptime(visit.start_time, '%Y-%b-%d %H:%M:%S').isoformat() 
                pnt.timespan.end = datetime.datetime.strptime(end_time, '%Y-%b-%d %H:%M:%S').isoformat()

            else:
                pnt.style.iconstyle.color = Color.rgb(0,0,0)

        # adding edges
        for carer in carers:
            for i  in range (0,len(carer.visits)-1):
                source = carer.visits[i]
                target = carer.visits[i+1]
                linestring = kml.newlinestring()
                linestring.coords = [(float(source.lon),float(source.lat),0), (float(target.lon),float(target.lat),0)]
                linestring.style.linestyle = carer.style.linestyle
                linestring.timespan.begin = datetime.datetime.strptime(source.start_time, '%Y-%b-%d %H:%M:%S').isoformat() 
                linestring.timespan.end = datetime.datetime.strptime(carer.get_endtime(), '%Y-%b-%d %H:%M:%S').isoformat()

        kml.save(path[:-5]+".kml")


