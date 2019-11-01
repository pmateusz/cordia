import os
import os.path
import datetime
import logging
import pandas as pd
import functools
import json
import operator

from distutils.util import strtobool
from rows.visualisation.problem import load_solution
from rows.model.schedule import Schedule
import rows.console
import rows.location_finder
import rows.model.area
import rows.model.carer
import rows.model.json
import rows.model.location
import rows.model.metadata
import rows.model.past_visit
import rows.model.problem
import rows.model.schedule
import rows.model.service_user
import rows.model.visit
import rows.settings
import rows.sql_data_source
import rows.routing_server

def get_visits_info(area_code, date_schedule):

    __script_file = os.path.realpath(__file__)
    __install_directory = os.path.dirname(os.path.dirname(os.path.dirname(__script_file)))
    settings = rows.settings.Settings(__install_directory)
    settings.reload()
    console = rows.console.Console()
    user_tag_finder = rows.location_finder.UserLocationFinder(settings)
    location_cache = rows.location_finder.FileSystemCache(settings)
    location_finder = rows.location_finder.MultiModeLocationFinder(location_cache, user_tag_finder, timeout=5.0)
    data_source = rows.sql_data_source.SqlDataSource(settings, console, location_finder)
    
    schedule = data_source.get_past_schedule(rows.model.area.Area(code=area_code), date_schedule)
        
    visits = []
    for visit in schedule.visits:
        visits.append(visit)
    
    return visits

def get_travel_time(session,location_finder,source,destination):
    source_loc = location_finder.find(source)
    if not source_loc:
        logging.error('Failed to resolve location of %s', source)
        return 0
    destination_loc = location_finder.find(destination)
    if not destination_loc:
        logging.error('Failed to resolve location of %s', destination)
        return 0
    distance = session.distance(source_loc, destination_loc)
    if distance is None:
        logging.error('Distance cannot be estimated between %s and %s', source_loc, destination_loc)
        return 0
    travel_time = datetime.timedelta(seconds=distance)
    
    return travel_time

def get_histogram(writer, sheet, utilisation_hist, utilisation_hist_labels):

    df_hist_util = pd.DataFrame(utilisation_hist, index=utilisation_hist_labels)
    df_hist_util.to_excel(writer, sheet_name=sheet)

    workbook = writer.book
    chart = workbook.add_chart({'type': 'column', 'subtype': 'stacked'})

    for col_num in range(1, len(utilisation_hist[0]) + 1):
        chart.add_series({
            'name':       [sheet, 0, col_num],
            'categories': [sheet, 1, 0, len(utilisation_hist), 0],
            'values':     [sheet, 1, col_num, len(utilisation_hist), col_num],
            'gap':        4,
         })

    # Configure the chart axes. 
    chart.set_x_axis({'name': 'Carer SAP number', 'num_font':  {'rotation': 90}})
    chart.set_y_axis({'name': 'Utilisation time (seconds)', 'major_gridlines': {'visible': False}})

    # Insert the chart into the worksheet.
    worksheet = writer.sheets[sheet]
    worksheet.insert_chart('H4', chart)

def convert_timedelta(td_seconds):
    days, reminder = divmod(td_seconds, 86400)
    hours, remainder = divmod(reminder, 3600)
    minutes, seconds = divmod(remainder, 60)
    return datetime.timedelta(days = days, hours = hours, minutes = minutes, seconds = seconds)

def convert_to_time_from_seconds(td_seconds):
    if td_seconds/3600 >= 24:
        raise ValueError('Number of seconds ' + str(td_seconds) + ' greater than a day')
    sign = 1
    if td_seconds<0:
        sign = -1
        td_seconds = td_seconds*sign
    hours, remainder = divmod(td_seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    return datetime.datetime(2018, 1,1, sign*hours, minutes, seconds).time()

def convert_to_seconds(time):
    hours = time.hour
    minutes = time.minute
    secs = time.second
    return secs+minutes*60+hours*3600

def load_problem(problem_file):
    with open(problem_file, 'r') as input_stream:
        problem_json = json.load(input_stream)
        return rows.model.problem.Problem.from_json(problem_json)

def generate_MSExcel(problem_file,input_file,output_file):

    zero_duration = datetime.datetime(2018,1,1,0,0,0).time()

    # Create a Pandas Excel writer using XlsxWriter as the engine.
    filename, file_extension = os.path.splitext(input_file)
    writer = pd.ExcelWriter(filename+'.xlsx', engine='xlsxwriter')
    
    # load info from gexf solution file
    carers,visits_fromOPT = load_solution(input_file)
    
    for i in range(0,len(visits_fromOPT)):
        if not strtobool(visits_fromOPT[i].dropped):
            date_time = datetime.datetime.strptime(visits_fromOPT[0].start_time, '%Y-%b-%d %H:%M:%S')
            break

    #load info about schedules from problem file
    problem = load_problem(problem_file)
    area = problem.metadata.area.code

    schedule_date = date_time.date()
    carer_dairies = {
                carer_shift.carer.sap_number:
                    next((diary for diary in carer_shift.diaries if diary.date == schedule_date), None)
                for carer_shift in problem.carers}

    visits_fromDB = get_visits_info(area, date_time.date())

    # start server for computing walking distance
    __script_file = os.path.realpath(__file__)
    __install_directory = os.path.dirname(os.path.dirname(os.path.dirname(__script_file)))
    settings = rows.settings.Settings(__install_directory)
    settings.reload()
    location_finder = rows.location_finder.UserLocationFinder(settings)
    location_finder.reload()
    with rows.routing_server.RoutingServer() as session:

        # Get the xlsxwriter objects from the dataframe writer object.
        workbook  = writer.book

        opt_utilisation_hist = []
        opt_utilisation_hist_labels = []
        man_utilisation_hist = []
        man_utilisation_hist_labels = []

        cancelled_visits_inDB = []

        ##################################################
        # printing results of optimisation
        ##################################################
        row_id = 6
        dropped_carers = []
        dropped_visits = []
        for visit in visits_fromOPT:
            if strtobool(visit.dropped):
                dropped_visits.append(visit.id)

        dict_dataframe_opt={'Carer SAP': [],
                            'Service User ID':[],
                            'Actual start time' : [],
                            'Actual duration' : [],
                            'Planned start time' : [],
                            'Planned duration' : [],
                            'Scheduled start time' : [],
                            'Scheduled duration' : [],
                            'Travel time' : []}
        for carer in carers:        

            partial_travel_time = 0
            partial_work_time = 0    
            if not strtobool(carer.dropped):
                carer.sort_visits()
                for i in range(0,len(carer.visits)):
                    visit = carer.visits[i]
                    if i==0:
                        source = int(visit.user)
                    found_flag = False
                    for real_visit in visits_fromDB:
                        if real_visit.visit.key == int(visit.id):
                            found_flag = True
                            dict_dataframe_opt['Carer SAP'].append(int(carer.sap_number))
                            dict_dataframe_opt['Service User ID'].append(int(visit.user))
                            dict_dataframe_opt['Actual start time'].append(real_visit.check_in.time())
                            dict_dataframe_opt['Actual duration'].append(convert_to_time_from_seconds((real_visit.check_out - real_visit.check_in).seconds))
                            dict_dataframe_opt['Planned start time'].append(real_visit.time)
                            dict_dataframe_opt['Planned duration'].append(convert_to_time_from_seconds(int(real_visit.duration)))
                            dict_dataframe_opt['Scheduled start time'].append(datetime.datetime.strptime(visit.start_time,'%Y-%b-%d %H:%M:%S').time())
                            dict_dataframe_opt['Scheduled duration'].append(datetime.datetime.strptime(visit.duration,'%H:%M:%S').time())
                            partial_work_time =  partial_work_time + convert_to_seconds(datetime.datetime.strptime(visit.duration,'%H:%M:%S').time())
                            if i>0:
                                destination = int(visit.user)
                                travel_time =  get_travel_time(session, location_finder, source,destination)
                                dict_dataframe_opt['Travel time'].append(convert_to_time_from_seconds(travel_time.seconds))
                                partial_travel_time = partial_travel_time + travel_time.seconds
                                source = destination
                            else:
                                dict_dataframe_opt['Travel time'].append(zero_duration)
                            break
                    if not found_flag:
                        cancelled_visits_inDB.append(visit.id)
                        dict_dataframe_opt['Carer SAP'].append(int(carer.sap_number))
                        dict_dataframe_opt['Service User ID'].append(int(visit.user))
                        dict_dataframe_opt['Actual start time'].append(zero_duration)
                        dict_dataframe_opt['Actual duration'].append(zero_duration)
                        dict_dataframe_opt['Planned start time'].append(zero_duration)
                        dict_dataframe_opt['Planned duration'].append(zero_duration)
                        dict_dataframe_opt['Scheduled start time'].append(datetime.datetime.strptime(visit.start_time,'%Y-%b-%d %H:%M:%S').time())
                        dict_dataframe_opt['Scheduled duration'].append(datetime.datetime.strptime(visit.duration,'%H:%M:%S').time())
                        partial_work_time =  partial_work_time + convert_to_seconds(datetime.datetime.strptime(visit.duration,'%H:%M:%S').time())
                        if i>0:
                            destination = int(visit.user)
                            travel_time =  get_travel_time(session, location_finder, source,destination)
                            dict_dataframe_opt['Travel time'].append(convert_to_time_from_seconds(travel_time.seconds))
                            partial_travel_time = partial_travel_time + travel_time.seconds
                            source = destination
                        else:
                            dict_dataframe_opt['Travel time'].append(zero_duration)
                            
                sign = 1.0
                if carer.work_idle_time[0] == '-':
                    sign = -1.0
                    carer.work_idle_time = carer.work_idle_time[1:]
                time_to_print = datetime.datetime.strptime(carer.work_idle_time,'%H:%M:%S').time()

                partial_idle_time =  convert_to_seconds(time_to_print)
                opt_utilisation_hist_labels.append(int(carer.sap_number))
                opt_utilisation_hist.append({'service time': partial_work_time, 'travel time': partial_travel_time, 'idle time': sign*partial_idle_time}) 
            else:
                dropped_carers.append(carer.sap_number)

        # writing to Excel
        dataframe_opt = pd.DataFrame(dict_dataframe_opt, columns=['Carer SAP', 'Service User ID', 'Actual start time', 'Actual duration', 'Planned start time', 'Planned duration', 'Scheduled start time', 'Scheduled duration', 'Travel time'])
        dataframe_opt.to_excel(writer,'Optimiser', index=False, startrow=row_id)
        worksheet_opt = writer.sheets['Optimiser']

        ##################################################
        # printing results of manual schedule
        ##################################################
        carers_dict={}
        extra_carers=[]
        for visit in visits_fromDB:
            if visit.carer in carers_dict.keys():
                carers_dict[visit.carer].append(visit)
            else:
                carers_dict[visit.carer]=[visit]
            
        row_id = 6
        dict_dataframe_man={'Carer SAP': [],
                            'Service User ID':[],
                            'Actual start time' : [],
                            'Actual duration' : [],
                            'Planned start time' : [],
                            'Planned duration' : [],
                            'Travel time' : []}

        for carer in carers_dict.keys():

            visits = carers_dict[carer]
            visits.sort(key=lambda x: x.time, reverse=False)
            partial_travel_time = 0
            partial_work_time = 0

            for i in range(0,len(visits)):
                visit = visits[i]   
                if i==0:
                    source = int(visit.visit.service_user)   
                dict_dataframe_man['Carer SAP'].append(int(carer.sap_number))
                dict_dataframe_man['Service User ID'].append(int(visit.visit.service_user))
                dict_dataframe_man['Actual start time'].append(visit.check_in.time())
                dict_dataframe_man['Actual duration'].append(convert_to_time_from_seconds((visit.check_out-visit.check_in).seconds))
                dict_dataframe_man['Planned start time'].append(visit.time)
                dict_dataframe_man['Planned duration'].append(convert_to_time_from_seconds(int(visit.duration)))
                partial_work_time =  partial_work_time + (visit.check_out-visit.check_in).seconds
                if i>0:
                    destination = int(visit.visit.service_user)
                    travel_time =  get_travel_time(session, location_finder,source,destination)
                    dict_dataframe_man['Travel time'].append(convert_to_time_from_seconds(travel_time.seconds))
                    partial_travel_time = partial_travel_time + travel_time.seconds
                    source = destination
                else:
                    dict_dataframe_man['Travel time'].append(zero_duration)

            if carer.sap_number not in carer_dairies.keys():
                available_time = datetime.timedelta(seconds = 0)
                extra_carers.append(carer.sap_number)
            elif carer_dairies[carer.sap_number] is None:
                available_time = datetime.timedelta(seconds = 0)
                extra_carers.append(carer.sap_number)
            else:
                available_time = functools.reduce(operator.add, (event.duration
                                                                for event in
                                                                carer_dairies[carer.sap_number].events))
            carer_overtime = False
            if available_time.total_seconds()<(partial_work_time+partial_travel_time):
                carer_overtime=True
                partial_idle_time = (partial_work_time+partial_travel_time) - available_time.total_seconds()
                man_utilisation_hist.append({'service time': partial_work_time, 'travel time': partial_travel_time, 'idle time': -1*partial_idle_time}) 
            else:
                partial_idle_time = available_time.total_seconds()  - (partial_work_time+partial_travel_time) 
                man_utilisation_hist.append({'service time': partial_work_time, 'travel time': partial_travel_time, 'idle time': partial_idle_time}) 
            man_utilisation_hist_labels.append(int(carer.sap_number))

        # writing to Excel
        dataframe_man = pd.DataFrame(dict_dataframe_man, columns=['Carer SAP', 'Service User ID', 'Actual start time', 'Actual duration', 'Planned start time', 'Planned duration', 'Travel time'])
        dataframe_man.to_excel(writer,'Human Planner', index=False, startrow=row_id)
        worksheet_man = writer.sheets['Human Planner']

    #write incipit of files
    worksheet_opt.write(0, 0, "ROWS solution for " + date_time.date().strftime("%Y-%m-%d") + ", area " + area)
    worksheet_man.write(0, 0, "Human Planner solution for " + date_time.date().strftime("%Y-%m-%d")+ ", area " + area)

    total_walk_time_opt = sum(item['travel time'] for item in opt_utilisation_hist)
    total_walk_time_man = sum(item['travel time'] for item in man_utilisation_hist)
    td1 = convert_timedelta(int(total_walk_time_opt))
    td2 = convert_timedelta(int(total_walk_time_man))
    worksheet_opt.write(2, 0, "Total walking time: " + str(td1.days) + " days and "+ convert_to_time_from_seconds(td1.seconds).strftime('%H:%M:%S'))
    worksheet_man.write(2, 0, "Total walking time: " + str(td2.days) + " days and "+ convert_to_time_from_seconds(td2.seconds).strftime('%H:%M:%S'))

    total_idle_time_opt = sum(item['idle time'] for item in opt_utilisation_hist)
    total_idle_time_man = sum(item['idle time'] for item in man_utilisation_hist)
    td1 = convert_timedelta(int(total_idle_time_opt))
    td2 = convert_timedelta(int(total_idle_time_man))
    worksheet_opt.write(3, 0, "Total idle time: " + str(td1.days) + " days and "+ convert_to_time_from_seconds(td1.seconds).strftime('%H:%M:%S'))
    worksheet_man.write(3, 0, "Total idle time: " + str(td2.days) + " days and "+ convert_to_time_from_seconds(td2.seconds).strftime('%H:%M:%S'))
    
    total_service_time_opt = sum(item['service time'] for item in opt_utilisation_hist)
    total_service_time_man = sum(item['service time'] for item in man_utilisation_hist)
    td1 = convert_timedelta(int(total_service_time_opt))
    td2 = convert_timedelta(int(total_service_time_man))
    worksheet_opt.write(4, 0, "Total service time: " + str(td1.days) + " days and "+ convert_to_time_from_seconds(td1.seconds).strftime('%H:%M:%S'))
    worksheet_man.write(4, 0, "Total service time: " + str(td2.days) + " days and "+ convert_to_time_from_seconds(td2.seconds).strftime('%H:%M:%S'))

    worksheet_opt.write(2, 4, "Number of carers working: " + str(len(carers)-len(dropped_carers)) + ", dropped carers: " + ', '.join(dropped_carers) )
    worksheet_opt.write(3, 4, "Number of visits serviced: " + str(sum(len(carer.visits) for carer in carers))+", dropped visits:" +','.join(dropped_visits) )
    worksheet_man.write(2, 4, "Number of carers working: " + str(len(carers_dict.keys())) + ", extra carers: " + ', '.join(extra_carers) )
    worksheet_man.write(3, 4, "Number of visits serviced: " + str(len(visits_fromDB)) +", cancelled visits:" +','.join(cancelled_visits_inDB))
    
    # add histograms
    get_histogram(writer, 'Histogram Optimiser',  opt_utilisation_hist, opt_utilisation_hist_labels)
    get_histogram(writer, 'Histogram Human Planner', man_utilisation_hist, man_utilisation_hist_labels)

    writer.save()
