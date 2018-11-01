#!/usr/bin/env bash

../build/rows-main  --problem=../september-pilot/c380_october_problem_compare.json --maps=../data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:02:00 --opt-noprogress-time-limit=00:04:00 --postopt-noprogress-time-limit=00:08:00 --break-time-window=00:60:00 --visit-time-window=00:60:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c380redv60b60e15 --scheduling-date=2018-10-02 --formula=3level-reduction --output=c380redv60b60e15_20181002.gexf 1> c380redv60b60e15.log 2> c380redv60b60e15.err.log
../build/rows-main  --problem=../september-pilot/c380_october_problem_compare.json --maps=../data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:02:00 --opt-noprogress-time-limit=00:04:00 --postopt-noprogress-time-limit=00:08:00 --break-time-window=00:60:00 --visit-time-window=00:60:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c380redv60b60e15 --scheduling-date=2018-10-03 --formula=3level-reduction --output=c380redv60b60e15_20181003.gexf 1> c380redv60b60e15.log 2> c380redv60b60e15.err.log
../build/rows-main  --problem=../september-pilot/c380_october_problem_compare.json --maps=../data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:02:00 --opt-noprogress-time-limit=00:04:00 --postopt-noprogress-time-limit=00:08:00 --break-time-window=00:60:00 --visit-time-window=00:60:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c380redv60b60e15 --scheduling-date=2018-10-04 --formula=3level-reduction --output=c380redv60b60e15_20181004.gexf 1> c380redv60b60e15.log 2> c380redv60b60e15.err.log
