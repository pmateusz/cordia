import xml.etree.ElementTree as ET
import pandas as pd
from datetime import datetime, timedelta

ET.register_namespace('',"http://www.gexf.net/1.1draft")
tree = ET.parse('solution_modified.gexf')
root = tree.getroot()
root.attrib.update({'xmlns:viz':'http://www.gexf.net/1.2draft/viz'})

#convert to dynamic
sh= tree.find('{http://www.gexf.net/1.1draft}graph') 

carers=[]
serv_t=[]
trav_t=[]
idle_t=[]
stime=[]
dropped=[]
count=0
l=[]
flag = False
for eleme in sh.findall('{http://www.gexf.net/1.1draft}nodes'):
    for ele in eleme.findall('{http://www.gexf.net/1.1draft}node'):
         if 'carer' in ele.attrib['label']:
            for el in ele.findall('{http://www.gexf.net/1.1draft}attvalues'):
                for elm in el.findall('{http://www.gexf.net/1.1draft}attvalue'):
                    if(elm.attrib['for']  == '11'):
                        carer = elm.attrib['value']
                        carers.append(elm.attrib['value'])
                    if(elm.attrib['for']  == '15'):
                        service = elm.attrib['value']
                        dt = datetime.strptime(service,'%H:%M:%S')
                        tme = dt.time()
                        delta = timedelta(hours=tme.hour, minutes=tme.minute, seconds=tme.second)
                        serv_t.append(delta)                        
                    if(elm.attrib['for']  == '16'):
                        travel = elm.attrib['value']
                        dt1 = datetime.strptime(travel,'%H:%M:%S')
                        tme1 = dt1.time()
                        delta1 = timedelta(hours=tme1.hour, minutes=tme1.minute, seconds=tme1.second)
                        trav_t.append(delta1)
                    if(elm.attrib['for']  == '17'):
                        idle = elm.attrib['value']
                        if "-" in idle:
                         idle_t.append("00:00:00")
                        else:
                         dt2 = datetime.strptime(idle,'%H:%M:%S')
                         tme2 = dt2.time()
                         delta2 = timedelta(hours=tme2.hour, minutes=tme2.minute, seconds=tme2.second)
                         idle_t.append(delta2)
         if 'visit' in ele.attrib['label']:
           for el in ele.findall('{http://www.gexf.net/1.1draft}attvalues'):
              for elm in el.findall('{http://www.gexf.net/1.1draft}attvalue'):
                 start_attrib = elm.attrib
                 if start_attrib['for'] == "8":
                     Value = elm.get('value')
                     Val = Value[-8:]
                     stime.append(Val)
                 if start_attrib['for'] == "5":
                    flag = True
                    l.append(count)
                    Dropped = elm.get('value')
                    dropped.append(Dropped)
           if flag != True:
               dropped.append(False) 
           else:
               flag = False

carers_dict= dict(zip(carers, zip(serv_t, trav_t, idle_t)))
df = pd.DataFrame.from_dict(carers_dict, orient='index')
df.columns = ['serv_t', 'trav_t', 'idle_t']
col = ['serv_t', 'trav_t', 'idle_t']
df.index.name = 'carer'
writer = pd.ExcelWriter('stackedbars.xlsx', engine='xlsxwriter', datetime_format='hh:mm:ss', date_format='hh:mm:ss')
df.to_excel(writer, sheet_name='Sheet1')
workbook  = writer.book
worksheet = writer.sheets['Sheet1']
format1 = workbook.add_format({'num_format': "h:mm:ss"})
worksheet.set_column('B:B', None, format1)
worksheet.set_column('C:C', None, format1)
worksheet.set_column('D:D', None, format1)
chart = workbook.add_chart({'type': 'column', 'subtype': 'stacked'})
for col_num in range(1, len(col) + 1):
    chart.add_series({
            'name':       ['Sheet1', 0, col_num],
            'categories': ['Sheet1', 1, 0, len(carers), 0],
            'values':     ['Sheet1', 1, col_num, len(carers), col_num],
            'gap':        4,
            })
chart.set_y_axis({'major_gridlines': {'visible': False}})
worksheet.insert_chart('K2', chart)

df1= pd.DataFrame(list(zip(stime, dropped)))
df1.columns = ['stime', 'dropped']
visits=df1.stime.str[:2].value_counts()
total_visits = dict(visits)
    
hours = df1.stime.str[:2]
drop = df1.dropped
counter = {}

for i in range(0,len(hours)):
    if hours[i] in counter.keys():
        if drop[i]:
            counter[hours[i]] = counter[hours[i]]+1
    else:
        counter[hours[i]] = 0

visits_dict = {key:(total_visits[key], counter[key]) for key in total_visits}
df2= pd.DataFrame.from_dict(visits_dict, orient='index')
df2.columns = ['total visits', 'dropped visits']
col2 = ['total visits', 'dropped visits']
index=df2.index.values.tolist()
df2.index.name = 'hour'
df2.sort_index(inplace=True)
df2.to_excel(writer, sheet_name='Sheet2')
workbook  = writer.book
worksheet = writer.sheets['Sheet2']
chart1 = workbook.add_chart({'type': 'column'})
for coln in range(1, len(col2) + 1):
     chart1.add_series({
        'name':       ['Sheet2', 0, coln],
        'categories': ['Sheet2', 1, 0, len(index), 0],
        'values':     ['Sheet2', 1, coln, len(index), coln],
        'gap':        300,
    })
chart1.set_y_axis({'major_gridlines': {'visible': False}})
worksheet.insert_chart('K2', chart1)

writer.save()

                  
    
                      
      
