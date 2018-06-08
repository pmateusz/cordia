import xml.etree.ElementTree as ET
import pandas as pd
import time
import datetime
from distutils.util import strtobool

from rows.visualisation.problem import load_problem

def generate_stats(input_file,output_file):

    carers,visits = load_problem(input_file)

    sap_number = [carer.sap_number for carer in carers]
    serv_t = [carer.work_service_time for carer in carers]
    trav_t = [carer.work_travel_time for carer in carers]
    idle_t = [carer.work_idle_time for carer in carers]

    serv_t_sec = []
    trav_t_sec = []
    idle_t_sec = []

    for carer in carers:
        
        tme = time.strptime(carer.work_service_time,'%H:%M:%S')
        secs = datetime.timedelta(hours=tme.tm_hour, minutes=tme.tm_min, seconds=tme.tm_sec).total_seconds()  
        serv_t_sec.append(secs)

        tme = time.strptime(carer.work_travel_time,'%H:%M:%S')
        secs = datetime.timedelta(hours=tme.tm_hour, minutes=tme.tm_min, seconds=tme.tm_sec).total_seconds() 
        trav_t_sec.append(secs)

        tme = time.strptime(carer.work_idle_time,'%H:%M:%S')
        secs = datetime.timedelta(hours=tme.tm_hour, minutes=tme.tm_min, seconds=tme.tm_sec).total_seconds() 
        idle_t_sec.append(secs)

    carers_dict= dict(zip(sap_number, zip(serv_t, serv_t_sec, trav_t, trav_t_sec, idle_t, idle_t_sec)))
    df = pd.DataFrame.from_dict(carers_dict, orient='index')
    df.columns = ['service','service (s)', 'travel', 'travel (s)', 'idle', 'idle(s)']
    df.index.name = 'carer'

    writer = pd.ExcelWriter(output_file, engine='xlsxwriter', datetime_format='hh:mm:ss', date_format='mmmm dd yyyy')
    df.to_excel(writer, sheet_name='Sheet1')
    workbook  = writer.book
    worksheet = writer.sheets['Sheet1']
    format_time = workbook.add_format({'num_format': "h:mm:ss"})
    worksheet.set_column('B:B', None, format_time)
    worksheet.set_column('D:D', None, format_time)
    worksheet.set_column('F:F', None, format_time)

    #histogram
    chart = workbook.add_chart({'type': 'column', 'subtype': 'stacked'})
    for col_num in range(2, len(df.columns) + 1,2):
        chart.add_series({
            'name':       ['Sheet1', 0, col_num],
            'categories': ['Sheet1', 1, 0, len(carers), 0],
            'values':     ['Sheet1', 1, col_num, len(carers), col_num],
            'gap':        4,
            })
    chart.set_y_axis({'major_gridlines': {'visible': False}})
    worksheet.insert_chart('K2', chart)

    stime = [datetime.datetime.strptime(visit.start_time,'%Y-%b-%d %H:%M:%S').time().strftime('%H:%M:%S') for visit in visits]
    dropped = [visit.dropped for visit in visits]

    df1= pd.DataFrame(list(zip(stime, dropped)))
    df1.columns = ['stime', 'dropped']
    v=df1.stime.str[:2].value_counts()
    total_visits = dict(v)
    
    hours = df1.stime.str[:2]
    drop = df1.dropped
    counter = {}

    cc=0
    for visit in visits:
        if strtobool(visit.dropped):
            cc = cc+1
            
    for i in range(0,len(hours)):
        if not hours[i] in counter.keys():
            counter[hours[i]] = 0
        if strtobool(drop[i]):
                counter[hours[i]] = counter[hours[i]]+1

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

                  
    
                      
      
