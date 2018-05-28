import simplekml
import dateutil.parser as parser
from datetime import datetime, timedelta
import xml.etree.ElementTree as ET
import numpy as np
import random

## Read from solution file
tag = '{http://www.gexf.net/1.1draft}'
tree = ET.parse('solution.gexf')
sh = tree.find(tag+'graph') 

#create dictionary of attributes - IDs
dict_att={}
for atts in sh.findall(tag+'attributes'):
    for att in atts.findall(tag+'attribute'):
        dict_att[att.attrib['title']]=att.attrib['id']
type_id = dict_att['type']
sap_number_id = dict_att['sap_number']
id_id = dict_att['id']
assigned_carer_id = dict_att['assigned_carer']
st = dict_att['start_time']
dropped = dict_att['dropped']
duration = dict_att['duration']
work_relative = dict_att['work_relative']
lon = dict_att['longitude']
lat = dict_att['latitude']

#retrieve date of scheduling
Value=""
for eleme in sh.findall(tag+'nodes'):
    for ele in eleme.findall(tag+'node'):
        for el in ele.findall(tag+'attvalues'):
            for elm in el.findall(tag+'attvalue'):
                start_attrib = elm.attrib
                if start_attrib['for'] == st:
                    Value = elm.get('value')
        if Value: 
            date1 = parser.parse(Value)
            ele.set('start', date1.isoformat())   
            Value=""
#set default time
day = date1.isoformat()[0:10]    

#retrieve information about carers and visits (which carer service which visit and when the carer is ending the visits serviced)
dict_carer={}
dict_carer_id={}
dict_carer_endtime={}
dict_carer_starttime={}
dict_carer_firstvisit_pos={}
dict_carer_firstvisit_id={}
dict_carer_color={}
for nodes in sh.findall(tag+'nodes'):
    for node in nodes.findall(tag+'node'):
        for attvalues in node.findall(tag+'attvalues'):
            attributes = {}            
            for att in attvalues.findall(tag+'attvalue'):
                attributes[att.attrib['for']] = att.attrib['value'] 
        if attributes[type_id] == 'carer':
            SAP_number = attributes[sap_number_id] 
            dict_carer[SAP_number]=[]  
            dict_carer_id[node.attrib['id']]=SAP_number   
            dict_carer_endtime[SAP_number]=day+'T00:00:00'  
            dict_carer_starttime[SAP_number]=day+'T23:59:59'
            dict_carer_firstvisit_pos[SAP_number] = [0.0,0.0]          
            dict_carer_firstvisit_id[SAP_number] = ""
        elif attributes[type_id] == 'visit' :
            if assigned_carer_id in attributes.keys():              
                SAP_number = attributes[assigned_carer_id]
                #append visit id to dictionary
                id_visit = node.attrib['id']                        
                dict_carer[SAP_number].append(id_visit)
                #compute endtime visit and update endtime carer
                t0 = attributes[st]
                dt = attributes[duration]               
                FMT = '%H:%M:%S'
                parsed_time = datetime.strptime(t0[-8:], FMT)
                parsed_duration = datetime.strptime(dt, FMT)
                then = parsed_time + timedelta(hours=parsed_duration.hour,
                               minutes=parsed_duration.minute,
                               seconds=parsed_duration.second)
                result = then.strftime(FMT)
                tdelta_end = datetime.strptime(dict_carer_endtime[SAP_number][-8:], FMT) - datetime.strptime(result, FMT)
                tdelta_start = datetime.strptime(parsed_time.strftime(FMT), FMT) - datetime.strptime(dict_carer_starttime[SAP_number][-8:], FMT)
                if  tdelta_end.total_seconds() < 0:           
                    dict_carer_endtime[SAP_number] = day+'T'+result
                if  tdelta_start.total_seconds() < 0: 
                    dict_carer_starttime[SAP_number] = day+'T'+parsed_time.strftime(FMT)
                    dict_carer_firstvisit_pos[SAP_number][0] = float(attributes[lon])
                    dict_carer_firstvisit_pos[SAP_number][1] = float(attributes[lat])
                    dict_carer_firstvisit_id[SAP_number] = id_visit        

#create palette of colors for carers
number_of_colors = len(dict_carer.keys())
color = ["#"+''.join([random.choice('0123456789ABCDEF') for j in range(6)])
             for i in range(number_of_colors)]
i = 0
for carer_SAP in dict_carer.keys():
    dict_carer_color[carer_SAP]=color[i]
    i = i+1

kml = simplekml.Kml()
for carer_SAP in dict_carer:
    pnt = kml.newpoint(name="Carer " + carer_SAP, coords=[dict_carer_firstvisit_pos[carer_SAP]])  
    pnt.extendeddata.newdata(name='work_relative', value=1, displayname="Work relative")
    pnt.extendeddata.newdata(name='work_relative', value=1, displayname="Work relative")
    pnt.extendeddata.newdata(name='work_relative', value=1, displayname="Work relative")
    pnt.extendeddata.newdata(name='work_relative', value=1, displayname="Work relative")
    pnt.extendeddata.newdata(name='work_relative', value=1, displayname="Work relative")
    #pnt.style.iconstyle.icon.href='http://maps.google.com/mapfiles/kml/shapes/man.png'
    #pnt.style.iconstyle.color=dict_carer_color[carer_SAP]
    #pnt.style.balloonstyle.text="bla bla bla"
kml.save("schedule.kml")