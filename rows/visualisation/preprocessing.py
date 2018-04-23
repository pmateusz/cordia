import dateutil.parser as parser
import xml.etree.ElementTree as ET
import matplotlib.pyplot as plt
import numpy as np

#add namespace
ET.register_namespace('',"http://www.gexf.net/1.1draft")
tree = ET.parse('solution.gexf')
root = tree.getroot()
root.attrib.update({'xmlns:viz':'http://www.gexf.net/1.2draft/viz'})

#convert to dynamic
sh= tree.find('{http://www.gexf.net/1.1draft}graph') 
sh.set('mode','dynamic')

#add timeformat 
timeformat = ET.Element('{http://www.gexf.net/1.1draft}graph')
for timeformat in root:
    timeformat.set('timeformat', 'datetime')
    
#set start attribute to nodes
Value=""
for eleme in sh.findall('{http://www.gexf.net/1.1draft}nodes'):
    for ele in eleme.findall('{http://www.gexf.net/1.1draft}node'):
        for el in ele.findall('{http://www.gexf.net/1.1draft}attvalues'):
            for elm in el.findall('{http://www.gexf.net/1.1draft}attvalue'):
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

#set node size & COLOR
for eleme in sh.findall('{http://www.gexf.net/1.1draft}nodes'):
    for ele in eleme.findall('{http://www.gexf.net/1.1draft}node'):
        child_size = ET.SubElement(ele,'{viz}size')
	    child_col = ET.SubElement(ele,'{viz}color', value='0.0')
        if 'carer' in ele.attrib['label']:
            for el in ele.findall('{http://www.gexf.net/1.1draft}attvalues'):
                for elm in el.findall('{http://www.gexf.net/1.1draft}attvalue'):
                    if(elm.attrib['for']  == '12'):
                       size_c = elm.attrib['value'] 
                       child_size.set('value', size_c)
		    if(elm.attrib['for']  == '11'):
		       color = list(np.random.choice(range(256), size=3))
	               child_col.set('r', str(color[0])) 
                       child_col.set('g', str(color[1]))
                       child_col.set('b', str(color[2])) 
        elif('user' in ele.attrib['label'] or 'visit' in ele.attrib['label']):
            for el in ele.findall('{http://www.gexf.net/1.1draft}attvalues'):
                for elm in el.findall('{http://www.gexf.net/1.1draft}attvalue'):
                    if(elm.attrib['for']  == '6'):
                      size_c = elm.attrib['value'] 
                      child_size.set('value', size_c)  
		    if(elm.attrib['for']  == '7'):
		      color = list(np.random.choice(range(256), size=3))
                      child_col.set('r', str(color[0]))                          
                      child_col.set('g', str(color[1]))
                      child_col.set('b', str(color[2]))
              
        
tree.write('solution_modified.gexf')           
