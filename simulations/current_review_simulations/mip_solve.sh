#!/usr/bin/env bash

set -e

pushd /home/pmateusz/dev/cordia/simulations/current_review_simulations

#pushd benchmark/25

#for date in 20171001 20171002 20171003 20171004 20171005 20171006 20171007 20171008 20171009 20171010 20171011 20171012 20171013 20171014
#do
#echo $date
#/home/pmateusz/dev/cordia/build/rows-mip-solver --problem=problem_$date\_v25m0c3.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --time-limit=08:00:00 --output=solutions/solution_$date\_v25m0c3_mip.json 1> ./solutions/problem_$date\_v25m0c3_mip.log 2> ./solutions/problem_$date\_v25m0c3_mip.err.log
#/home/pmateusz/dev/cordia/build/rows-mip-solver --problem=problem_$date\_v25m5c3.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --time-limit=08:00:00 --output=solutions/solution_$date\_v25m5c3_mip.json 1> ./solutions/problem_$date\_v25m5c3_mip.log 2> ./solutions/problem_$date\_v25m5c3_mip.err.log
#done
#
#popd

for date in 20171002 20171003 20171004 20171005 20171006 20171007 20171008 20171009 20171010 20171011 20171012 20171013 20171014 # 20171001
do
echo $date

mkdir -p solutions

pushd /home/pmateusz/dev/cordia/simulations/current_review_simulations/benchmark/50
#/home/pmateusz/dev/cordia/build/rows-mip-solver --problem=problem_$date\_v50m0c5.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --time-limit=08:00:00 --output=./solutions/solution_$date\_v50m0c10_mip.gexf 1> ./solutions/problem_$date\_v50m0c5_mip.log 2> ./solutions/problem_$date\_v50m0c5_mip.err.log
/home/pmateusz/dev/cordia/build/rows-mip-solver --problem=problem_$date\_v50m10c5.json --maps=/home/pmateusz/dev/cordia/data/scotland-latest.osrm --time-limit=08:00:00 --output=./solutions/solution_$date\_v50m10c5_mip.gexf 1> ./solutions/problem_$date\_v50m10c5_mip.log 2> ./solutions/problem_$date\_v50m10c5_mip.err.log
popd
done