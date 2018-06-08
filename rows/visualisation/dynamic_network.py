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
from simplekml import Kml, Style, Color, ColorMode

from rows.visualisation.problem import load_problem

def generate_colors(n):
    palette = []
    dict_colors = {}

    #generate dictionaries of colors
    for name_color,value_color in vars(Color).items():
        if len(str(value_color)) == 8:
            dict_colors[name_color]=value_color

    if 'black' in dict_colors.keys():
        del dict_colors['black']

    # generate palette for problem
    if n <= len(dict_colors):
        palette = [ dict_colors[key] for key in random.sample(dict_colors.keys(), n) ]
    else:
        N = np.floor(n/len(dict_colors))
        palette = [ palette.extend(dict_colors.values()) for i in range(0,N) ] 
        reminder = n - N*len(dict_colors)
        random_sublist = [ dict_colors[key] for key in random.sample(dict_colors.keys(), reminder) ]
        palette.extend(random_sublist)

    return palette

def generate_graph(input_file, output_file):

    carers, visits = load_problem(input_file)

    #write corresponding KLM file for display in Google Earth
    kml = Kml()      
    #create palette of colors for carers and sorting carers visits
    colors = generate_colors(len(carers))
        
    i=0
    for carer in carers:
        #setting style 
        carer_style = Style()

        carer_style.iconstyle.color = colors[i]
        carer_style.iconstyle.icon.href = 'http://maps.google.com/mapfiles/kml/shapes/man.png'
        carer_style.linestyle.color = colors[i] 
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
            
        if visit.is_multiple_carers():
            pnt.style.iconstyle.icon.href='http://maps.google.com/mapfiles/kml/paddle/wht-blank.png' 
        else:
            pnt.style.iconstyle.icon.href='http://maps.google.com/mapfiles/kml/pushpin/wht-pushpin.png'

        if not strtobool(visit.dropped):
            pnt.extendeddata.newdata(name='assigned_carer', value=visit.assigned_carer, displayname="Assigned carer SAP")
            pnt.extendeddata.newdata(name='satisfaction', value=visit.satisfaction, displayname="User satisfaction")
            
        if not strtobool(visit.dropped):
            for carer in carers:
                if visit.assigned_carer == carer.sap_number:
                    carer_style = carer.style
                    end_time = carer.get_endtime()
                    break              

            if carer_style: 
                pnt.style.iconstyle.color = carer_style.iconstyle.color
                pnt.timespan.begin = datetime.datetime.strptime(visit.start_time, '%Y-%b-%d %H:%M:%S').isoformat() 
                pnt.timespan.end = datetime.datetime.strptime(end_time, '%Y-%b-%d %H:%M:%S').isoformat()
        else: #if viist is dropped
            pnt.style.iconstyle.color = Color.rgb(0,0,0)
                        
    # adding edges
    for carer in carers:
        if len(carer.visits)>1:
            for i  in range (0,len(carer.visits)-1):
                source = carer.visits[i]
                target = carer.visits[i+1]
                linestring = kml.newlinestring()
                linestring.coords = [(float(source.lon),float(source.lat),0), (float(target.lon),float(target.lat),0)]
                linestring.style.linestyle = carer.style.linestyle
                linestring.timespan.begin = datetime.datetime.strptime(target.start_time, '%Y-%b-%d %H:%M:%S').isoformat() 
                linestring.timespan.end = datetime.datetime.strptime(carer.get_endtime(), '%Y-%b-%d %H:%M:%S').isoformat()


    kml.save(output_file)


