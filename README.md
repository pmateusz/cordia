# Robust Optimization for Workforce Scheduling [![Build Status](https://travis-ci.com/pmateusz/cordia.svg?token=BGZD8zC9w6stvzyuFQhA&branch=master)](https://travis-ci.com/pmateusz/cordia)

This is the source code repository for the Robust Optimization for Workforce Scheduling Project.

## [Setup a Development Machine](DEVELOPMENT.md)

## Quick Start

### Building from Source

Install required dependencies.
```shell
sudo apt install build-essential git cmake pkg-config \
libbz2-dev libxml2-dev libzip-dev libboost-all-dev \
lua5.2 liblua5.2-dev libtbb-dev
```

Install Microsoft ODBC Driver for SQL Server
```shell
echo  'deb [arch=amd64] https://packages.microsoft.com/debian/8/prod jessie main' > /etc/apt/sources.list.d/mssql-release.list
curl https://packages.microsoft.com/keys/microsoft.asc | sudo apt-key add -
sudo apt update
sudo apt install msodbcsql mssql-tools
wget "http://security.debian.org/debian-security/pool/updates/main/o/openssl/libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb"
sudo apt install ./libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb
```

Change the system locale to en-US.

Test the connection using the `sqlcmd` utility.

```shell
sqlcmd -S 130.159.46.87 -U dev -N -C -l 5
```

Run tests of the Python components.
```shell
find . -iname "*.py" | xargs python3 -m pytest
```

Run checkstyle of the Python components.
```shell
find rows* -iname "*.py" | xargs python3 -m pylint
```

## Generate a traffic map
Download a map of a certain area. The map should saved in the Open Street Map Protocol Buffer (osm.pbf). Most recent maps extracted from the Open Street Map project are available at `http://download.geofabrik.de`.  

```shell
cd data && wget http://download.geofabrik.de/europe/great-britain/scotland-latest.osm.pbf
```

Build a suitable traffic profile for the OSRM backend. The OSRM backend is distributed with a collection of default configurations for a car, a bicycle and a pedestrian profile. The profiles are saved in a root installation directory under the share/osrm/profiles path.  

```shell
../build/external/osrm-install/bin/osrm-extract -p ../build/external/osrm-install/share/osrm/profiles/foot.lua scotland-latest.osm.pbf
../build/external/osrm-install/bin/osrm-partition scotland-latest.osrm
../build/external/osrm-install/bin/osrm-customize scotland-latest.osrm
```

Verify that the configuration file is not corrupted. 

```shell
../build/external/osrm-install/bin/osrm-routed --algorithm=MLD scotland-latest.osrm
```

For more information refer to https://github.com/Project-OSRM/osrm-backend/wiki/Running-OSRM.

## Help

Output a problem
```shell
> ./rows_cli.py pull merchant_city --from=2/1/2017 --to=2/2/2017 --output=problem.json
```

Run a simulation as the part of control group
```shell
sudo cgcreate -t pmateusz:pmateusz -a pmateusz:pmateusz  -g memory,cpu:simulations 
cgexec -g 'memory,cpu:simulations' ./rows_cli.py solve test_problem.json --schedule-date=2/1/2017
```

## Contribute

The project is supported by a continuous integration pipeline hosted by [Travis CI](https://travis-ci.com/pmateusz/cordia). Review the [.travis.yml](.travis.yml) file
for the most up to date instructions about development environment set up.


### CLion Configuration

Solve the scheduling problem for a given day.
```shell
rows-main --problem=test_used_fixed_problem.json --maps=./data/cars/scotland-latest.osrm --solution=past_solution.json --scheduling-date=2017-02-01 --output=test_solution.gexf
```

Pull the solution for a given day.
```shell
rows_cli.py solution C240 --schedule-date=2/1/2017
```

Pull the problem definition for a given day.
```shell
rows_cli.py pull C240 --from=2/1/2017 --to=2/14/2017 --resource-estimator=used
rows_cli.py pull C070 -f 2/1/2017 -t 2/14/2017  -o test_problem.json  --duration-estimator=global_percentile --resource-estimator=used
```



## Project Documentation
Refer to [the project wiki pages](https://github.com/pmateusz/cordia/wiki) for more information on this subject.