#!/usr/bin/env bash

../build/rows-main  --problem=../september-pilot/c270_october_problem.json --maps=../data/scotland-latest.osrm --console-format=log --preopt-noprogress-time-limit=00:02:00 --opt-noprogress-time-limit=00:04:00 --postopt-noprogress-time-limit=00:08:00 --break-time-window=00:30:00 --visit-time-window=00:30:00 --begin-end-shift-time-extension=00:15:00 --output-prefix=c270redv30b30e15 --solve-all --formula=3level-reduction 1> c270redv30b30e15.log 2> c270redv30b30e15.err.log
