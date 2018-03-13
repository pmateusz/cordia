# Robust Optimization for Workforce Scheduling

This document aims at providing comprehensive manual for setting up a development machine for contributing to the [ROWS](https://github.com/pmateusz/cordia) project.

## Prerequisites
* GCC 6 or higher - to compile code written in C++ 11
* Python 3.5 or higher - to run the Python scripts
* CMake 3.0 or higher - to generate make files
* System locale `en-US` - for the database driver to work
* Required libraries libaries - the complete list is available in the upcoming section

### Operating System
The recommended operating system for development are Debian Stretch and Ubuntu Trusty. There are no known compatibility issues that could prevent using or developing the project in a different environment, though you do it at your own risk.

We provide a DEB package of the [Google Operations Research Tools 6.6](https://github.com/google/or-tools/releases/tag/v6.6), release of November 2017, for Debian Stretch and Ubuntu Trusty. If you decide to use another operating system, you may need to build and install Google Operations Research Tools yourself.


## Tutorial
1. Install support for secure transport in the package manager.
```shell
apt-get update
apt-get install --assume-yes wget apt-transport-https ca-certificates gnupg2 
```

1. Add the `https://pmateusz.github.io/debian-stretch` source to the package repository
```shell
wget -qO - https://pmateusz.github.io/debian-stretch/archive.key | sudo apt-key add -
echo "deb https://pmateusz.github.io/debian-stretch stretch main" | sudo tee -a /etc/apt/sources.list
```

For Ubuntu Trusty use the command below.
```
wget -qO - https://pmateusz.github.io/ubuntu/archive.key | sudo apt-key add -
echo "deb https://pmateusz.github.io/ubuntu stretch main" | sudo tee -a /etc/apt/sources.list
```

If you are using a different operating system you may need to generate missing packages yourself if reusing existing packages is impossible.

1. Install the following packages.
```shell
sudo apt-get update
sudo apt-get install --assume-yes g++-6 \
build-essential \
git \
pkg-config \
libbz2-dev \
libxml2-dev \
libzip-dev \
libboost-all-dev \
lua5.2 \
liblua5.2-dev \
libtbb-dev \
libglog-dev \
libgtest-dev \
libsparsehash-dev \
libortools-dev \
cmake \
locales \
python3-pip \
unixodbc-dev
```

1. Install Gurobi 7.5.1.

Gurobi symbols are reuquired for the successful compilation of executables that depend on the Google Operations Research toolbox. The current version of the Rows project does not use Gurobi.

```shell
wget http://packages.gurobi.com/7.5/gurobi7.5.1_linux64.tar.gz --quiet -Ogurobi7.5.1.tar.gz
tar -xzf gurobi7.5.1.tar.gz
rm gurobi7.5.1.tar.gz
sudo mv gurobi751 /opt/
pushd .
cd /opt/gurobi751/linux64/lib && sudo ln -s libgurobi75.so libgurobi.so
popd
```

1. Install the Microsoft ODBC Driver for SQL Server.
```shell
echo  'deb [arch=amd64] https://packages.microsoft.com/debian/8/prod jessie main' > /etc/apt/sources.list.d/mssql-release.list
curl https://packages.microsoft.com/keys/microsoft.asc | sudo apt-key add -
sudo apt update
sudo apt install msodbcsql mssql-tools
wget "http://security.debian.org/debian-security/pool/updates/main/o/openssl/libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb"
sudo dpkg -i libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb
export PATH="$PATH:/opt/mssql-tools/bin"
```

1. Ensure that the locale in your system is `en_US` otherwise the Microsoft ODBC Driver for SQL Server may not work correctly.
```shell
locale
```

If the LANG is not equal to `en-US.UTF-8 UTF-8` then execute the following.

```shell
echo 'LANG="en_US.UTF-8 UTF-8"'>/etc/default/locale && \
dpkg-reconfigure --frontend=noninteractive locales && \
update-locale LANG=en_US.UTF-8
```

1. Ensure the Microsoft ODBC Driver for SQL Server works by printing help output of the `sqlcmd` utility.
```shell
sqlcmd
```

1. Clone the Rows project.
```shell
git clone git@github.com:pmateusz/cordia.git
```

> Remaining commands should be executed from the project root directory.

1. Install tools required by the Python component of the Rows project.
```shell
pip3 install -r requirements.txt
```

1. Generate make files.

```shell
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=RELEASE
```

1. Compile the project.
```shell
cmake --build . --target build_all
```

1. Generate a traffic map for pedestrians.
```shell
cd .. && mkdir data && cd data
wget http://download.geofabrik.de/europe/great-britain/scotland-latest.osm.pbf
../build/external/osrm-install/bin/osrm-extract -p \ ../build/external/osrm-install/share/osrm/profiles/foot.lua scotland-latest.osm.pbf
../build/external/osrm-install/bin/osrm-partition scotland-latest.osrm
../build/external/osrm-install/bin/osrm-customize scotland-latest.osrm
```

1. Run tests.
```
find . -iname "*.py" | xargs python3 -m pytest
cd build && make test
```

1. Create `.dev` file that contains password to the database.

### Examples

Print help.
```shell
./rows_cli.py --help
```

Print help for a specific command.
```
./rows_cli.py solve --help
```

Attempt to create a problem file for an unknown area.
```shell
./rows_cli.py pull A -f 2/1/2017 -t 2/14/2017
```

Create a problem file for `C050` area between the 1st of Februray 2017 and the 14th of February 2017.
```shell
./rows_cli.py pull C050 -f 2/1/2017 -t 2/7/2017
```

Solve a scheduling problem.
> Type `stop` and press enter to pause calculations.
```shell
./rows_cli.py solve problem.json
```

Start calculations from the last solution.
```shell
./rows_cli.py solve problem.json --start solution.gexf 
```

1. Workarounds
```shell
mkdir -p data/cordia/data/cordia
touch data/cordia/data/cordia/location_cache.json
```

```shell
apt-get remove libgflags-dev
```