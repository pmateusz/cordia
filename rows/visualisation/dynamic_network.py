import bs4
import os
import sys
import re
import simplekml
import dateutil.parser as parser
from datetime import datetime, timedelta
import xml.etree.ElementTree as ET
import numpy as np
import random

# find visits
# find carers

class Carer:
    def __init__(self, id, sap_number):
        self.id = id
        self.sap_number = sap_number
	self.visits = []

    def set_work_param(self, work_relative, work_total_time, work_available_time, work_service_time, work_travel_time, work_idle_time, work_visits_count)
	self.work_relative = work_relative
	self.work_total_time = work_total_time
	self.work_available_time = work_available_time
	self.work_service_time = work_service_time
	self.work_travel_time = work_travel_time
	self.work_idle_time = work_idle_time
	self.work_visits_count = work_visits_count

    def append_visit(self,visit)
	self.visits.append(visit)

    def set_color(self,color)
        self.color =  color

    def get_starttime(self)
	return self.visits[0].start_time

    def get_endtime(self)
	return self.visits[-1].start_time + self.visits[-1].duration

class Visit:
    def __init__(self, dropped):
	self.dropped = dropped

    def set_dynamicparam(self, assigned_carer, satisfaction)
        self.assigned_carer = assigned_carer
	self.satisfaction = satisfaction

    def set_staticparam(self, lon, lat, user, start_time, duration)
	self.lon = lon
	self.lat = lat
	self.user = user
	self.start_time = start_time
	self.duration = duration

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
	user = attributes['users']
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
		work_relative = attributes.find('attvalue', attrs={'for': work_relative})
		work_total_time = attributes.find('attvalue', attrs={'for': work_total_time})
		work_available_time = attributes.find('attvalue', attrs={'for': work_available_time})
		work_service_time = attributes.find('attvalue', attrs={'for': work_service_time})
		work_travel_time = attributes.find('attvalue', attrs={'for': work_travel_time})
		work_idle_time = attributes.find('attvalue', attrs={'for': work_idle_time})
		work_visits_count = attributes.find('attvalue', attrs={'for': work_visits_count})

                carers.append(Carer(id_attr['value'], sap_number_attr['value']))
		carers[-1].set_work_param(work_relative, work_total_time, work_available_time, work_service_time, work_travel_time, work_idle_time, work_visits_count)
            elif type_attr['value'] == 'visit':
		dropped = attributes.find('attvalue', attrs={'for': dropped})
		lon = attributes.find('attvalue', attrs={'for': lon})
                lat = attributes.find('attvalue', attrs={'for': lat})
	        user = attributes.find('attvalue', attrs={'for': user})
		start_time = attributes.find('attvalue', attrs={'for': start_time})
		duration = attributes.find('attvalue', attrs={'for': duration})                		
		visit = Visit(dropped)		
		visit.set_staticparam(lon, lat, user, start_time, duration)		
		if !dropped:
		    assigned_carer = attributes.find('attvalue', attrs={'for': assigned_carer_id})
                    satisfaction = attributes.find('attvalue', attrs={'for': satisfaction})
		    visit.set_dynamicparam(assigned_carer, satisfaction)
		visits.append(visit)

	for visit in visits:
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
            match = name_pattern.match(file)
            problem_name = match.group('problem_name')
            problem_path = os.path.join(problem_dir, file)
            problems.append((problem_name, problem_path))

    for problem, path in problems:
        carers, visits = load_problem(path)
          
	#create palette of colors for carers
	number_of_colors = len(carers)
	color = ["#"+''.join([random.choice('0123456789ABCDEF') for j in range(6)])
             for i in range(number_of_colors)]
	i = 0
	for carer in carers:
    	    carer.set_color(color[i])
    	    i = i+1
	#write corresponding KLM file for display in Google Earth
	kml = simplekml.Kml()
	for carer in carers:
    	    pnt = kml.newpoint(name="Carer", coords=[carer.visits[0].lon,carer.visits[0].lat]) 
            pnt.extendeddata.newdata(name='sap_number', value=carer.sap_number, displayname="SAP number")
	    pnt.extendeddata.newdata(name='work_relative', value=carer.work_relative, displayname="Relative working time") 
            pnt.extendeddata.newdata(name='work_total_time', value=carer.work_total_time, displayname="Total working time") 
            pnt.extendeddata.newdata(name='work_available_time', value=carer.work_available_time, displayname="Available working time") 
            pnt.extendeddata.newdata(name='work_service_time', value=carer.work_service_time, displayname="Service working time") 
            pnt.extendeddata.newdata(name='work_travel_time', value=carer.work_travel_time, displayname="Travelling time") 
            pnt.extendeddata.newdata(name='work_idle_time', value=carer.work_idle_time, displayname="Idle time") 
            pnt.extendeddata.newdata(name='work_visits_count', value=carer.work_visits_count, displayname="Number of visits") 

	for visit in visits:
    	    pnt = kml.newpoint(name="Visit", coords=[visits.lon,carer.visits.lat]) 
            pnt.extendeddata.newdata(name='sap_number', value=carer.sap_number, displayname="SAP number")
	    pnt.extendeddata.newdata(name='work_relative', value=carer.work_relative, displayname="Relative working time") 
            pnt.extendeddata.newdata(name='work_total_time', value=carer.work_total_time, displayname="Total working time") 
            pnt.extendeddata.newdata(name='work_available_time', value=carer.work_available_time, displayname="Available working time") 
            pnt.extendeddata.newdata(name='work_service_time', value=carer.work_service_time, displayname="Service working time") 
            pnt.extendeddata.newdata(name='work_travel_time', value=carer.work_travel_time, displayname="Travelling time") 
            pnt.extendeddata.newdata(name='work_idle_time', value=carer.work_idle_time, displayname="Idle time") 
            pnt.extendeddata.newdata(name='work_visits_count', value=carer.work_visits_count, displayname="Number of visits") 

            #pnt.style.iconstyle.icon.href='http://maps.google.com/mapfiles/kml/shapes/man.png'
            #pnt.style.iconstyle.color=dict_carer_color[carer_SAP]

        kml.save(path+".kml")
