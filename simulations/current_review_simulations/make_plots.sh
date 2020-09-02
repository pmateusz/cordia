#!/usr/bin/env bash

PLOT_TOOL=/home/pmateusz/dev/cordia/rows_compare.py

set -e
image_ext=png

pushd /home/pmateusz/dev/cordia/simulations/current_review_simulations

echo 'Making plot of multistage distance formulation'
$PLOT_TOOL compare-trace ./problems/C350_past.json ./cp_schedules/past/c350past_distv90b90e30m1m1m5.err.log --cost_function="Multistage Dist" --output distance_trace_350

echo 'Making plot of direct distance formulation'
$PLOT_TOOL compare-trace ./problems/C350_past.json ./cp_schedules/past/c350past_1levelv90b90e30m1m20m1.err.log --cost_function="Direct" --arrows=True --output stage1_trace_350

echo 'Making comparison plot of 1 and 3 level distance formulation for a specific date'
$PLOT_TOOL contrast-trace ./problems/C350_past.json ./cp_schedules/past/c350past_1levelv90b90e30m1m20m1.err.log ./cp_schedules/past/c350past_distv90b90e30m1m1m5.err.log --cost_function="1 Level" --date 2017-10-1 --output contrast_trace_350

echo 'Making distance comparison'
$PLOT_TOOL compare-distance --problem ./problems/C350_past.json --schedule_patterns "/home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/second_stage_c350past_distv90b90e30m1m1m5_201710*.gexf" "/home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_distv90b90e30m1m1m5_201710*.gexf" --labels "2nd Stage" "3rd Stage" --output=compare_distance_350

$PLOT_TOOL compare-box-plots "/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json" "/home/pmateusz/dev/cordia/simulations/current_review_simulations/solutions/c350past_1levelv90b90e30m1m20m1.err.log" --output boxplot_stage1
$PLOT_TOOL compare-box-plots "/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json" "/home/pmateusz/dev/cordia/simulations/current_review_simulations/solutions/c350past_distv90b90e30m1m1m5.err.log" --output boxplot_distance_trace

$PLOT_TOOL compare-third-stage-summary --output third_stage_summary

mkdir -p plots
mv stage1_trace_350.$image_ext plots/
mv contrast_trace_350_2017-10-01.$image_ext plots/
mv distance_trace_350.$image_ext plots/
mv boxplot_stage1.$image_ext plots/
mv boxplot_distance_trace.$image_ext plots/
mv third_stage_summary.$image_ext plots/

$PLOT_TOOL compare-cost
$PLOT_TOOL compare-quality
$PLOT_TOOL compare-benchmark-table


popd