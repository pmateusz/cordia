#!/usr/bin/env bash

pushd ~/dev/cordia/simulations/review_simulations

#echo 'Making shifts histograms'
#../rows_compare.py show-working-hours ./human/shifts.csv --output working_hours_350

#echo 'Making plot of multistage reduction formulation'
#../../rows_compare.py compare-trace /home/pmateusz/dev/cordia/simulations/review_simulations/problems/C350_forecast.json /home/pmateusz/dev/cordia/simulations/review_simulations/cp_schedules/forecast/c350redv90b90e15m2m4m8.err.log --cost_function="Multistage Red" --output reduction_trace_350

echo 'Making plot of multistage distance formulation'
../../rows_compare.py compare-trace /home/pmateusz/dev/cordia/simulations/review_simulations/problems/C350_forecast.json /home/pmateusz/dev/cordia/simulations/review_simulations/cp_schedules/forecast/c350distv90b90e15m2m4m8.err.log --cost_function="Multistage Dist" --output distance_trace_350

#echo 'Making plot of direct distance formulation'
../../rows_compare.py compare-trace /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_forecast.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_1levelv90b90e30m1m20m1.err.log --cost_function="Direct" --arrows=True --output level1_trace_350

echo 'Making comparison plot of 1 and 3 level distance formulation for a specific date'
../../rows_compare.py contrast-trace /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_forecast.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_1levelv90b90e30m1m20m1.err.log /home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_distv90b90e30m1m1m5.err.log --cost_function="1 Level" --date 2017-10-1 --output contrast_trace_350

#echo 'Making distance comparison'
#../../rows_compare.py compare-distance --problem /home/pmateusz/dev/cordia/simulations/review_simulations/problems/C350_forecast.json --schedule_patterns "/home/pmateusz/dev/cordia/simulations/review_simulations/planner_schedules/C350_planners_201710*.json" "/home/pmateusz/dev/cordia/simulations/review_simulations/cp_schedules/forecast/second_stage_c350distv90b90e15m2m4m8_201710*.gexf" "/home/pmateusz/dev/cordia/simulations/review_simulations/cp_schedules/forecast/c350distv90b90e15m2m4m8_201710*.gexf" --labels "Human Planners" "2nd Stage" "3rd Stage" --output=compare_distance_350

echo 'Making distance comparison'
../../rows_compare.py compare-distance --problem /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json --schedule_patterns "/home/pmateusz/dev/cordia/simulations/current_review_simulations/planner_schedules/C350_planners_201710*.json" "/home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/second_stage_c350past_distv90b90e30m1m1m5_201710*.gexf" "/home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_distv90b90e30m1m1m5_201710*.gexf" --labels "2nd Stage" "3rd Stage" --output=compare_distance_350

#echo 'Making workload comparison for the reduced objective function'
#../../rows_compare.py compare-workload /home/pmateusz/dev/cordia/simulations/review_simulations/problems/C350_forecast.json "/home/pmateusz/dev/cordia/simulations/review_simulations/cp_schedules/forecast/c350redv90b90e15m2m4m8_201710*.gexf" "/home/pmateusz/dev/cordia/simulations/review_simulations/cp_schedules/forecast/second_stage_c350distv90b90e15m2m4m8_201710*.gexf"

#echo 'Making workload comparison for the distance objective function'
#../rows_compare.py compare-workload c350_forecasted_problem.json "./350_distance/c350dv90b90e15m2m4m8_*.gexf" "./350_distance/second_stage_c350dv90b90e15m2m4m8_*.gexf"

#echo 'Compare quality in Benchmark'
#../../rows_compare.py compare-benchmark /home/pmateusz/dev/cordia/simulations/review_simulations/benchmark.csv

mkdir -p plots
#cp benchmark_boxplot_25_0.pdf ./plots/st_benchmark_boxplot_25_0.pdf
#cp benchmark_boxplot_25_5.pdf ./plots/st_benchmark_boxplot_25_5.pdf
#cp benchmark_boxplot_50_0.pdf ./plots/st_benchmark_boxplot_50_0.pdf
#cp benchmark_boxplot_50_10.pdf ./plots/st_benchmark_boxplot_50_10.pdf
#cp distance_trace_350.pdf ./plots/st_distance_trace_350.pdf
#cp compare_reduction_350.pdf ./plots/st_compare_reduction_350.pdf
#mv contrast_workforce_2017-10-04_combined.png ./plots/st_contrast_workforce_2017-10-04_combined.png
#cp contrast_trace_350_2017-10-04.pdf ./plots/st_contrast_trace_350_2017-10-04.pdf
#cp contrast_trace_350_first_stage_2017-10-04.pdf ./plots/st_contrast_trace_350_first_stage_2017-10-04.pdf
#cp contrast_trace_350_oscillations_2017-10-04.pdf ./plots/st_contrast_trace_350_oscillations_2017-10-04.pdf
#cp stage1_trace_350.pdf ./plots/st_stage1_trace_350.pdf

# use initial results
#cp ../initial_simulations/contrast_workforce_2017-10-04_combined.pdf ./plots/st_contrast_workforce_2017-10-04_combined.pdf
rm *.png
popd