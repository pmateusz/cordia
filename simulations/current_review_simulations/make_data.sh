#!/usr/bin/env bash

set -e

#/home/pmateusz/dev/cordia/rows_cli.py pull C350 --from=10/1/2017 --to=10/14/2017  -o C350_human_planners.json  --duration-estimator=fixed --resource-estimator=used
#/home/pmateusz/dev/cordia/rows_cli.py pull C350 --from=10/1/2017 --to=10/14/2017  -o C350_past.json  --duration-estimator=past --resource-estimator=used
#/home/pmateusz/dev/cordia/rows_cli.py pull C350 --from=10/1/2017 --to=10/14/2017  -o C350_forecast.json  --duration-estimator=forecast --resource-estimator=used

pushd /home/pmateusz/dev/cordia/simulations/current_review_simulations

mkdir -p benchmark/25

pushd benchmark/25

for date in 20171001 20171002 20171003 20171004 20171005 20171006 20171007 20171008 20171009 20171010 20171011 20171012 20171013 20171014
do
    /home/pmateusz/dev/cordia/rows_cli.py make-instance /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_distv90b90e30m1m1m5_$date.gexf --visits=25 --carers=3 --multiple-carer-share=0.2 --output=problem_$date.json
done

popd

mkdir -p benchmark/50

pushd benchmark/50

for date in 20171001 20171002 20171003 20171004 20171005 20171006 20171007 20171008 20171009 20171010 20171011 20171012 20171013 20171014
do
    /home/pmateusz/dev/cordia/rows_cli.py make-instance /home/pmateusz/dev/cordia/simulations/current_review_simulations/problems/C350_past.json /home/pmateusz/dev/cordia/simulations/current_review_simulations/cp_schedules/past/c350past_distv90b90e30m1m1m5_$date.gexf --visits=50 --carers=5 --multiple-carer-share=0.2 --output=problem_$date.json
done

popd

popd