import dateutil.parser as parser
from datetime import datetime, timedelta
import xml.etree.ElementTree as ET
import numpy as np
import random

#add namespace
#registering namespaces
nsm = {"":  "http://www.gexf.net/1.1draft", "viz": "http://www.gexf.net/1.2draft/viz"}
for i in nsm.keys():
    ET.register_namespace(i,nsm[i])

tree = ET.parse('solution.gexf')
root = tree.getroot()
tag = '{http://www.gexf.net/1.1draft}'

#convert to dynamic
sh= tree.find(tag+'graph') 
sh.set('mode','dynamic')

dict_att={}
#create dictionary of attributes - IDs
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

#add timeformat 
timeformat = ET.Element(tag+'graph')
for timeformat in root:
    timeformat.set('timeformat', 'datetime')
    
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
date_default = day+"T00:00:00"
for elem in root.getiterator():
    if elem.text:
        elem.text= elem.text.replace('2000-Jan-01 00:00:00',date_default)

#retrieve information about carers and visits (which carer service which visit and when the carer is ending the visits serviced)
dict_carer={}
dict_carer_id={}
dict_carer_endtime={}
dict_carer_starttime={}
dict_carer_firstvisit_pos={}
dict_carer_firstvisit_id={}
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

print dict_carer

#setting up size and colors and endtime in the graph

#create palette of colors
n_carers = len(dict_carer.keys())
colors_dict = {}
for i in dict_carer.keys():
    colors_dict[i] = list(np.random.choice(range(256), size=3)) 
colors_dict['dropped'] = [0, 0, 0] #setting color of dropped visits to black

#set size and colors
for nodes in sh.findall(tag+'nodes'):
    for node in nodes.findall(tag+'node'):
        for attvalues in node.findall(tag+'attvalues'):
            attributes = {}            
            for att in attvalues.findall(tag+'attvalue'):
                attributes[att.attrib['for']] = att.attrib['value']  
        if attributes[type_id] == 'carer':
            child_size = ET.SubElement(node, ET.QName(nsm['viz'], "size"), value='0.0')
            child_col = ET.SubElement(node, ET.QName(nsm['viz'], "color"))
            # setting start and end time for carers
            size_c = attributes[work_relative] 
            child_size.set('value', str(float(size_c)+1))
            SAP_number = attributes[sap_number_id] 
            color = colors_dict[SAP_number] 
            child_col.set('r', str(color[0]))                          
            child_col.set('g', str(color[1]))
            child_col.set('b', str(color[2]))
            node.set('end', dict_carer_endtime[SAP_number])
            node.set('start', dict_carer_starttime[SAP_number])
            lon_el = ET.SubElement(attvalues, ET.QName(nsm[""],"attvalue"))
            lat_el = ET.SubElement(attvalues, ET.QName(nsm[""],"attvalue"))
	    lon_el.set('for' , lon)
            lat_el.set('for' , lat)
	    lon_el.set('value' ,str(dict_carer_firstvisit_pos[SAP_number][0] -0.001+0.002*random.uniform(0, 1)) )
            lat_el.set('value' ,str(dict_carer_firstvisit_pos[SAP_number][1] -0.001+0.002*random.uniform(0, 1)) )
        elif attributes[type_id]=='visit':
            child_size = ET.SubElement(node, ET.QName(nsm['viz'], "size"), value='0.0')
            child_col = ET.SubElement(node, ET.QName(nsm['viz'], "color"))
            size_c = '0.5' 
            child_size.set('value', size_c)        
            if assigned_carer_id in attributes.keys():    
                SAP_number = attributes[assigned_carer_id] 
                color = colors_dict[SAP_number] 
                child_col.set('r', str(color[0]))                          
                child_col.set('g', str(color[1]))
                child_col.set('b', str(color[2]))
                node.set('end', dict_carer_endtime[SAP_number])
            else:
                color = colors_dict['dropped'] 
                child_col.set('r', str(color[0]))                          
                child_col.set('g', str(color[1]))
                child_col.set('b', str(color[2]))   
        elif attributes[type_id]=='user':
            nodes.remove(node)

for edges in sh.findall(tag+'edges'):
    for edge in edges.findall(tag+'edge'):
        if 'u' in edge.attrib['source']:
            edges.remove(edge)
        if 'c' in edge.attrib['source']:
            carer_id = edge.attrib['source']
            carer_SAP = dict_carer_id[carer_id]
            if edge.attrib['target'] != dict_carer_firstvisit_id[carer_SAP]:
                edges.remove(edge)

#write final xml to file
tree.write('solution_modified.gexf')           
