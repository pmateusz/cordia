#!/usr/bin/env bash

pushd ~/dev/cordia/simulations/

echo 'Making shifts histograms'
../rows_compare.py show-working-hours ./human/shifts.csv --output working_hours_350

echo 'Making plot of 3 level reduction formulation'
../rows_compare.py compare-trace c350_forecasted_problem.json ./350_reduction/c350redv90b90e15m2m4m8.err.log --cost_function="3 Level Red" --output reduction_trace_350

echo 'Making plot of 3 level distance formulation'
../rows_compare.py compare-trace c350_forecasted_problem.json ./350_distance/c350dv90b90e15m2m4m8.err.log --cost_function="3 Level Dist" --output distance_trace_350

echo 'Making plot of 1 level distance formulation'
../rows_compare.py compare-trace c350_forecasted_problem.json ./350_1level/c3501levelv90b90e15m2m4m8.err.log --cost_function="1 Level" --output level1_trace_350

echo 'Making comparison plot of 1 and 3 level distance formulation'
../rows_compare.py contrast-trace c350_forecasted_problem.json ./350_1level/c3501levelv90b90e15m2m4m8.err.log ./350_distance/c350dv90b90e15m2m4m8.err.log --cost_function="1 Level" --date 2017-10-4 --output contrast_trace_350

echo 'Making distance comparison'
../rows_compare.py compare-distance --schedule_patterns "./350_distance/c350dv90b90e15m2m4m8_201710*.gexf" "./350_distance/second_stage_c350dv90b90e15m2m4m8_201710*.gexf" "./human/c350human_201710*.json" --labels "3 Level Constraint Programming" "2 Level Constraint Programming" "Human Planners" --output=compare_distance_350

echo 'Making workload comparison'
../rows_compare.py compare-workload c350_forecasted_problem.json "./350_reduction/c350redv90b90e15m2m4m8_*.gexf" "./350_distance/second_stage_c350dv90b90e15m2m4m8_201710*.gexf"

popd