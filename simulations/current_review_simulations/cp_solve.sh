#!/usr/bin/env bash

set -e

pushd /home/pmateusz/dev/cordia/simulations/current_review_simulations

#mkdir -p benchmark/25/solutions
#
#for date in 20171001 20171002 20171003 20171004 20171005 20171006 20171007 20171008 20171009 20171010 20171011 20171012 20171013 20171014
#do
#echo $date 25
#/home/pmateusz/dev/cordia/build/rows-main --problem=./benchmark/25/problem_$date\_v25m0c3.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --first-stage=none --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:03:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output=./benchmark/25/solutions/solution_$date\_v25m0c3.gexf 1> ./benchmark/25/solutions/problem_$date\_v25m0c3.log 2> ./benchmark/25/solutions/problem_$date\_v25m0c3.err.log
#/home/pmateusz/dev/cordia/build/rows-main --problem=./benchmark/25/problem_$date\_v25m5c3.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --first-stage=soft-time-windows --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:03:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output=./benchmark/25/solutions/solution_$date\_windows_v25m5c3.gexf 1> ./benchmark/25/solutions/problem_$date\_windows_v25m5c3.log 2> ./benchmark/25/solutions/problem_$date\_windows_v25m5c3.err.log
#/home/pmateusz/dev/cordia/build/rows-main --problem=./benchmark/25/problem_$date\_v25m5c3.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --first-stage=teams --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:03:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output=./benchmark/25/solutions/solution_$date\_teams_v25m5c3.gexf 1> ./benchmark/25/solutions/problem_$date\_teams_v25m5c3.log 2> ./benchmark/25/solutions/problem_$date\_teams_v25m5c3.err.log
#done
#popd

#pushd /home/pmateusz/dev/cordia/simulations/current_review_simulations
#
#mkdir -p benchmark/50/solutions
#
#for date in 20171001 20171002 20171003 20171004 20171005 20171006 20171007 20171008 20171009 20171010 20171011 20171012 20171013 20171014
#do
#echo $date 50
#/home/pmateusz/dev/cordia/build/rows-main --problem=./benchmark/50/problem_$date\_v50m0c5.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm  --first-stage=none --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:03:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output=./benchmark/50/solutions/solution_$date\_v50m0c5.gexf 1> ./benchmark/50/solutions/problem_$date\_v50m0c5.log 2> ./benchmark/50/solutions/problem_$date\_v50m0c5.err.log
#/home/pmateusz/dev/cordia/build/rows-main --problem=./benchmark/50/problem_$date\_v50m10c5.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm  --first-stage=soft-time-windows --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:03:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output=./benchmark/50/solutions/solution_$date\_windows_v50m10c5.gexf 1> ./benchmark/50/solutions/problem_$date\_windows_v50m10c5.log 2> ./benchmark/50/solutions/problem_$date\_windows_v50m10c5.err.log
#/home/pmateusz/dev/cordia/build/rows-main --problem=./benchmark/50/problem_$date\_v50m10c5.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm  --first-stage=teams --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:03:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output=./benchmark/50/solutions/solution_$date\_teams_v50m10c5.gexf 1> ./benchmark/50/solutions/problem_$date\_teams_v50m10c5.log 2> ./benchmark/50/solutions/problem_$date\_teams_v50m10c5.err.log
#done
#popd

#/home/pmateusz/dev/cordia/build/rows-main --problem=/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:05:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output-prefix=c350past_redv90b90e30m1m1m5 --solve-all --third-stage=vehicle-reduction 1> c350past_redv90b90e30m1m1m5.log 2> c350past_redv90b90e30m1m1m5.err.log
#/home/pmateusz/dev/cordia/build/rows-main --problem=/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:05:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output-prefix=c350past_distv90b90e30m1m1m5 --solve-all --third-stage=distance 1> c350past_distv90b90e30m1m1m5.log 2> c350past_distv90b90e30m1m1m5.err.log
#/home/pmateusz/dev/cordia/build/rows-main --problem=/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:20:00 --postopt-noprogress-time-limit=00:01:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output-prefix=c350past_1levelv90b90e30m1m20m1 --solve-all --first-stage=none --third-stage=none 1> c350past_1levelv90b90e30m1m20m1.log 2> c350past_1levelv90b90e30m1m20m1.err.log

for date in 2017-10-01 2017-10-02 2017-10-03 2017-10-04 2017-10-05 2017-10-06 2017-10-07 2017-10-08 2017-10-09 2017-10-10 2017-10-11 2017-10-12 2017-10-13 2017-10-14
do
local_date=$date
/home/pmateusz/dev/cordia/build/rows-main --problem=/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --history=/home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_history.json --console-format=log --preopt-noprogress-time-limit=00:01:00 --opt-noprogress-time-limit=00:01:00 --postopt-noprogress-time-limit=00:01:00 --break-time-window=00:90:00 --visit-time-window=00:90:00 --begin-end-shift-time-extension=00:30:00 --output-prefix=c350past_riskinessv90b90e30m1m1m5 --scheduling-date=$local_date --third-stage=delay-riskiness-reduction 1> c350past_riskinessv90b90e30m1m1m1_$local_date.log 2> c350past_riskinessv90b90e30m1m1m1_$local_date.err.log
done