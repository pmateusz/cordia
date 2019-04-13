import collections
import copy
import datetime
import json
import sys
import logging
import os.path

import rows.load

from ortools.sat.python import cp_model


class Handler:

    def __init__(self, application):
        self.__application = application

    def __call__(self, command):
        problem_file = getattr(command, 'problem')
        output_file = getattr(command, 'output')
        solution_file = getattr(command, 'solution')
        num_carers = int(getattr(command, 'carers'))
        num_visits = int(getattr(command, 'visits'))
        multiple_carer_threshold = float(getattr(command, 'multiple_carer_share'))
        num_multiple_carer_visits = int(multiple_carer_threshold * num_visits)
        disruption = float(getattr(command, 'disruption'))

        def low_disruption(value):
            return int((1.0 - disruption) * value)

        def up_disruption(value):
            return int((1.0 + disruption) * value)

        solution = rows.load.load_schedule(solution_file)
        solution_date = solution.metadata.begin

        assert solution.metadata.begin == solution.metadata.end

        model = cp_model.CpModel()

        carer_ids = set()
        visit_by_carers = collections.defaultdict(set)
        for visit in solution.visits:
            if visit.carer:
                carer_ids.add(visit.carer.key)
                visit_by_carers[visit.visit.key].add(visit.carer.key)

        carer_vars = dict()
        for carer in carer_ids:
            carer_vars[carer] = model.NewBoolVar('c_{0}'.format(carer))

        visit_vars = dict()
        for visit in visit_by_carers:
            visit_vars[visit] = model.NewBoolVar('v_{0}'.format(visit))

        model.AddSumConstraint(visit_vars.values(), low_disruption(num_visits), up_disruption(num_visits))
        model.AddSumConstraint(carer_vars.values(), num_carers, num_carers)
        multiple_carer_visit_vars = [visit_vars[visit] for visit in visit_by_carers if len(visit_by_carers[visit]) == 2]
        model.AddSumConstraint(multiple_carer_visit_vars,
                               low_disruption(num_multiple_carer_visits),
                               up_disruption(num_multiple_carer_visits))

        for visit in visit_by_carers:
            for carer in visit_by_carers[visit]:
                model.Add(visit_vars[visit] <= carer_vars[carer])

        solver = cp_model.CpSolver()
        status = solver.Solve(model)
        if status != cp_model.FEASIBLE and status != cp_model.OPTIMAL:
            print('Failed to find a solution. The solver returned status: {0}'.format(status), file=sys.stderr)

        visits_to_keep = set()
        for visit_id, visit_variable in visit_vars.items():
            if solver.Value(visit_variable):
                visits_to_keep.add(visit_id)

        carers_to_keep = set()
        for carer_id, carer_variable in carer_vars.items():
            if solver.Value(carer_variable):
                carers_to_keep.add(carer_id)

        with open(problem_file) as input_file:
            problem_json = json.load(input_file)

        # remove visits
        visit_leaves_to_remove = []
        for visit_leaf in problem_json['visits']:
            visits_to_remove = []
            for visit in visit_leaf['visits']:
                keep = visit['key'] in visits_to_keep
                if not keep:
                    visits_to_remove.append(visit)
            for visit in visits_to_remove:
                visit_leaf['visits'].remove(visit)
            if not visit_leaf['visits']:
                visit_leaves_to_remove.append(visit_leaf)

        # remove service users with no visits
        for user in visit_leaves_to_remove:
            problem_json['visits'].remove(user)
            service_user_id = user['service_user']
            service_user_to_remove = None
            for service_user in problem_json['service_users']:
                if service_user['key'] == service_user_id:
                    service_user_to_remove = service_user
                    break
            assert service_user_to_remove
            problem_json['service_users'].remove(service_user_to_remove)

        # remove service users
        service_user_leaves_to_remove = []
        for service_user in problem_json['service_users']:
            service_user_id = service_user['key']
            has_visits = False
            for visit_leaf in problem_json['visits']:
                if visit_leaf['service_user'] == service_user_id and visit_leaf['visits']:
                    has_visits = True
                    break
            if not has_visits:
                service_user_leaves_to_remove.append(service_user)

        for service_user in service_user_leaves_to_remove:
            problem_json['service_users'].remove(service_user)

        # remove carers
        carers_to_remove = []
        for carer_leaf in problem_json['carers']:
            keep = carer_leaf['carer']['sap_number'] in carers_to_keep
            if not keep:
                carers_to_remove.append(carer_leaf)

        # remove diaries for existing carers
        for carer_leaf in carers_to_remove:
            problem_json['carers'].remove(carer_leaf)

        for carer_leaf in problem_json['carers']:
            diaries_to_remove = []
            for diary in carer_leaf['diaries']:
                diary_date = datetime.datetime.strptime(diary['date'], '%Y-%m-%d').date()
                if diary_date != solution_date:
                    diaries_to_remove.append(diary)

            for diary in diaries_to_remove:
                carer_leaf['diaries'].remove(diary)

        num_carers = len(carers_to_keep)
        num_visits = len(visits_to_keep)
        num_multiple_carer_visits = len([visit_vars[visit] for visit in visit_by_carers
                                         if visit in visits_to_keep and len(visit_by_carers[visit]) == 2])

        print('Generated problem with {0} carers, {1} visits and {2} multiple carer visits'
              .format(num_carers, num_visits, num_multiple_carer_visits))

        with open(output_file, 'w') as output_stream:
            json.dump(problem_json, output_stream, indent=2)

        no_multiple_problem = copy.deepcopy(problem_json)

        num_visits_converted = 0
        # make selected multiple carer visits single carer visits
        logging.warning('Some multiple carer visits will be converted to single carer visits')
        for visit_leaf in no_multiple_problem['visits']:
            for visit in visit_leaf['visits']:
                if visit['carer_count'] > 1:
                    visit['carer_count'] = 1
                    num_visits_converted += 1
        logging.warning('Converted %d multiple carer visits into single carer visits', num_visits_converted)

        print('Generated problem with {0} carers, {1} visits and 0 multiple carer visits'
              .format(num_carers, num_visits))

        filename, extension = os.path.splitext(output_file)
        no_multiple_output_file = filename + '_no_multiple_visits' + extension
        with open(no_multiple_output_file, 'w') as output_stream:
            json.dump(no_multiple_problem, output_stream, indent=2)
