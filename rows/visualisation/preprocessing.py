import dateutil.parser as parser
from datetime import datetime, timedelta
import xml.etree.ElementTree as ET
import matplotlib.pyplot as plt
import numpy as np

#add namespace
#registering namespaces
nsm = {"":  "http://www.gexf.net/1.1draft", "viz": "http://www.gexf.net/1.2draft/viz"}
for i in nsm.keys():
    ET.register_namespace(i,nsm[i])

tree = ET.parse('solution.gexf')
root = tree.getroot()
root.attrib.update({'xmlns:viz':'http://www.gexf.net/1.2draft/viz'})
tag = '{http://www.gexf.net/1.1draft}'

#convert to dynamic
sh= tree.find(tag+'graph') 
sh.set('mode','dynamic')

#add timeformat 
timeformat = ET.Element(tag+'graph')
for timeformat in root:
    timeformat.set('timeformat', 'datetime')
    
#set start attribute to nodes that are visits
Value=""
for eleme in sh.findall(tag+'nodes'):
    for ele in eleme.findall(tag+'node'):
        for el in ele.findall(tag+'attvalues'):
            for elm in el.findall(tag+'attvalue'):
                start_attrib = elm.attrib
                if start_attrib['for'] == "8":
                    Value = elm.get('value')
        if Value: 
            date1 = parser.parse(Value)
            ele.set('start', date1.isoformat())   
            Value=""

#set default
day = date1.isoformat()[0:7]    
date_default = day+"T00:00:00"
for elem in root.getiterator():
    if elem.text:
        elem.text= elem.text.replace('2000-Jan-01 00:00:00',date_default)

#retrieve information about carers and visits
dict_carer={'dropped':[]}
dict_carer_endtime={}
for nodes in sh.findall(tag+'nodes'):
    for node in nodes.findall(tag+'node'):
        if 'carer' in node.attrib['label']:
            for attvalues in node.findall(tag+'attvalues'):
                for elm in attvalues.findall(tag+'attvalue'):
                    if(elm.attrib['for']  == '11'):
                        SAP_number = elm.attrib['value'] 
                        dict_carer[SAP_number]=[]   
                        dict_carer_endtime[SAP_number]=day+'T00:00:00'            
        elif('visit' in node.attrib['label']):
            for attvalues in node.findall(tag+'attvalues'):
                for elm in attvalues.findall(tag+'attvalue'):
                    if(elm.attrib['for']  == '4'):
                        SAP_number = elm.attrib['value']
                    if(elm.attrib['for']  == '5'):
                        SAP_number = 'dropped'
                    if(elm.attrib['for']  == '8'):
                        t0 = elm.attrib['value']
                    if(elm.attrib['for']  == '9'):
                        dt = elm.attrib['value']
            if SAP_number != 'dropped':               
                FMT = '%H:%M:%S'
                parsed_time = datetime.strptime(t0[-8:], FMT)
                parsed_duration = datetime.strptime(dt, FMT)
                then = parsed_time + timedelta(hours=parsed_duration.hour,
                               minutes=parsed_duration.minute,
                               seconds=parsed_duration.second)
                result = then.strftime(FMT)
                tdelta = datetime.strptime(dict_carer_endtime[SAP_number][-8:], FMT) - datetime.strptime(result, FMT)
                if  tdelta.total_seconds() < 0:           
                    dict_carer_endtime[SAP_number] = day+'T'+result
            id_visit = node.attrib['label']                        
            dict_carer[SAP_number].append(id_visit)
                  
for i in dict_carer.keys():
    visits = dict_carer[i]

##setting up size and colors and endtime in the graph
#create palette of colors

n_carers = len(dict_carer.keys())
colors_dict = {}
for i in dict_carer.keys():
    colors_dict[i] = list(np.random.choice(range(256), size=3)) 

#set size and colors
for nodes in sh.findall(tag+'nodes'):
    for node in nodes.findall(tag+'node'):
        child_size = ET.SubElement(node, ET.QName(nsm['viz'], "size"), value='0.0')
        child_col = ET.SubElement(node, ET.QName(nsm['viz'], "color"), value='0.0')
        if 'carer' in node.attrib['label']:
            # setting start and end time for carers
            node.set('start', date_default)
            for attvalues in node.findall(tag+'attvalues'):
                for elm in attvalues.findall(tag+'attvalue'):
                    if(elm.attrib['for']  == '12'):
                        size_c = elm.attrib['value']
                        child_size.set('value', size_c)
                    if(elm.attrib['for']  == '11'):
                        SAP_number = elm.attrib['value'] 
                        color = colors_dict[SAP_number] 
                        child_col.set('r', str(color[0]))                          
                        child_col.set('g', str(color[1]))
                        child_col.set('b', str(color[2]))
            node.set('end', dict_carer_endtime[SAP_number])
        elif('visit' in node.attrib['label']):
            for attvalues in node.findall(tag+'attvalues'):
                size_c = '0.5' 
                child_size.set('value', size_c)        
                for elm in attvalues.findall(tag+'attvalue'):
                        if(elm.attrib['for']  == '4'):
                            SAP_number = elm.attrib['value'] 
                            color = colors_dict[SAP_number] 
                            child_col.set('r', str(color[0]))                          
                            child_col.set('g', str(color[1]))
                            child_col.set('b', str(color[2]))
            if SAP_number != 'dropped':
                node.set('end', dict_carer_endtime[SAP_number])

#write final xml to file
tree.write('solution_modified.gexf')           
