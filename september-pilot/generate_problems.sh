#!/usr/bin/env bash

#./rows_cli.py pull C100 --from=10/01/2018 --to=10/14/2018 --output c100_october_problem.json --duration-estimator=forecast --resource-estimator=planned
#./rows_cli.py pull C100 --from=10/01/2018 --to=10/14/2018 --output c100_october_problem_compare.json --duration-estimator=forecast --resource-estimator=used
./rows_cli.py pull C270 --from=10/01/2018 --to=10/14/2018 --output c270_october_problem_compare.json --duration-estimator=forecast --resource-estimator=used
./rows_cli.py pull C380 --from=10/01/2018 --to=10/14/2018 --output c380_october_problem_compare.json --duration-estimator=forecast --resource-estimator=used
#./rows_cli.py pull C380 --from=10/01/2018 --to=10/14/2018 --output c380_october_problem.json --duration-estimator=forecast --resource-estimator=planned
#./rows_cli.py pull C270 --from=10/01/2018 --to=10/14/2018 --output c270_october_problem.json --duration-estimator=forecast --resource-estimator=planned