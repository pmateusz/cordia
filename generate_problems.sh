#!/usr/bin/env bash
areas=( C050 C060 C070 C080 C090 C100 C110 C120 C130 C140 C200 C210 C230 C240 C250 C260 C270 C280 C300 C310 C320 C330 C340 C350 C360 C370 C380 C390 )
for area in "${areas[@]}"
do
    ./rows_cli.py pull $area --output=./march-demo/problem_${area}_percentile60.json --from=3/19/2017 --to=4/2/2017  --duration-estimator=global_percentile
done