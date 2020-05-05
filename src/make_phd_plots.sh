PLOT_TOOL=/home/pmateusz/dev/cordia/rows_compare.py

echo 'Making plot of multistage distance formulation'
$PLOT_TOOL compare-trace /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/solutions/c350past_distv90b90e30m1m1m5.err.log --cost_function="Multistage Dist" --output distance_trace_350

echo 'Making plot of direct distance formulation'
$PLOT_TOOL compare-trace /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/solutions/c350past_1levelv90b90e30m1m20m1.err.log --cost_function="Direct" --output stage1_trace_350

echo 'Making comparison plot of 1 and 3 level distance formulation for a specific date'
$PLOT_TOOL contrast-trace /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/solutions/c350past_1levelv90b90e30m1m20m1.err.log /home/pmateusz/dev/cordia/simulations/current_review_simulations/solutions/c350past_distv90b90e30m1m1m5.err.log --cost_function="1 Level" --date 2017-10-1 --output contrast_trace_350
