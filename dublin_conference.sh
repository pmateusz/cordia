#!/usr/bin/env bash

#./build/rows-main  --problem=./c100_forecast_october2018_problem_removed_unhandled.json --maps=./data/scotland-latest.osrm \
#--scheduling-date=2018-10-04 --console-format=log --preopt-noprogress-time-limit=00:04:00 --opt-noprogress-time-limit=00:15:00 --postopt-noprogress-time-limit=00:10:00 --break-time-window=00:45:00 --visit-time-window=00:45:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c100forecastv30b30e15 --formula=3level-reduction 1> c100redv30b30e15.log 2> c100redv30b30e15.err.log

./build/rows-main  --problem=./c100_october2018_problem_removed_unhandled.json --maps=./data/scotland-latest.osrm \
--scheduling-date=2018-10-04 --console-format=log --preopt-noprogress-time-limit=00:04:00 --opt-noprogress-time-limit=00:15:00 --postopt-noprogress-time-limit=00:10:00 --break-time-window=00:45:00 --visit-time-window=00:45:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c100_v30b30e15 --formula=3level-reduction  --output=c100_solution_planned.gexf 1> c100planned.log 2> c100planned.err.log
