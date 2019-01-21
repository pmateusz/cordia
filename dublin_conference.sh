#!/usr/bin/env bash

#./build/rows-main  --problem=./c100_forecast_october2018_problem_removed_unhandled.json --maps=./data/scotland-latest.osrm \
#--scheduling-date=2018-10-04 --console-format=log --preopt-noprogress-time-limit=00:02:00 --opt-noprogress-time-limit=00:04:00 --postopt-noprogress-time-limit=00:08:00 --break-time-window=00:45:00 --visit-time-window=00:45:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c100forecastv30b30e15 --formula=3level-reduction 1> c100redv30b30e15.log 2> c100redv30b30e15.err.log

./build/rows-main  --problem=./c100_forecast_october2018_problem_mean_removed_unhandled.json --maps=./data/scotland-latest.osrm \
--scheduling-date=2018-10-04 --console-format=log --preopt-noprogress-time-limit=00:02:00 --opt-noprogress-time-limit=00:04:00 --postopt-noprogress-time-limit=00:08:00 --break-time-window=00:45:00 --visit-time-window=00:45:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c100forecast_mean_v30b30e15 --formula=3level-reduction 1> c100redvmean45b45e15.log 2> c100redv45b45e15.err.log
