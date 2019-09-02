#!/usr/bin/env bash

pushd /home/pmateusz/dev/forecast

./src/main/main.py prepare .data/visits_070_2017.csv --output=test.hdf
./src/main/main.py cluster .data/visits_070_2017.hdf --table=a
./src/main/main.py compute-residuals .data/clusters_visits_070_2017.hdf
./src/main/main.py investigate .data/clusters_visits_070_2017.hdf --client=9096892 --cluster=4
./src/main/main.py plot-residuals .data/residuals.hdf

popd