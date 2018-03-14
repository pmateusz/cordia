# Robust Optimization for Workforce Scheduling (ROWS)
This document aims at providing a comprehensive manual for setting up a machine for development or deployment of the [ROWS](https://github.com/pmateusz/cordia) project.

The documented in opened by the [Prerequisites](#prerequisites) section which provides high level requirements on the operating system and libaries that must be installed on the host machine. The [Tutorial](#tutorial) section is a step by step manual that guides through the installation process and configuration of the ROWS project. The document is concluded with the [Examples](#examples) section which contains a sequence of common actions with the Rows Command Line Interface tool (rows_cli).

## Prerequisites
* GCC 6 or higher - for compiling source code written in C++ 11
* Python 3.5 or higher - for interpreting the Python scripts
* CMake 3.0 or higher - for generaitng the make files
* System locale set to `en-US` - for initializing the database driver
* User account in SQL Server - for accessing the database

### Operating System
The recommended operating systems for development are Debian Stretch and Ubuntu Trusty. As of this writing are no known compatibility issues that could prevent using or developing the project in a different environment, though you do it at your own risk.

A DEB package of the [Google Operations Research Tools 6.6](https://github.com/google/or-tools/releases/tag/v6.6), release of November 2017, is provided for Debian Stretch and Ubuntu Trusty in a personal repositories [pmateusz/debian-stretch](https://github.com/pmateusz/debian-stretch) and [pmateusz/ubuntu](https://github.com/pmateusz/ubuntu). The packages are provided with no warranty and you use them at your own risk. If you decide to use another operating system, you may need to build and install Google Operations Research Tools yourself.

## Tutorial
1. Install support for the secure transport in the APT package manager.
    ```shell
    apt-get update
    apt-get install --assume-yes wget apt-transport-https ca-certificates gnupg2 
    ```

1. Add the `https://pmateusz.github.io/debian-stretch` package source to the list of approved package sources in the APT package manager configuration.
    ```shell
    wget -qO - https://pmateusz.github.io/debian-stretch/archive.key | sudo apt-key add -
    echo "deb https://pmateusz.github.io/debian-stretch stretch main" | sudo tee -a /etc/apt/sources.list
    ```
    For Ubuntu Trusty use the command variant below.
    ```shell
    wget -qO - https://pmateusz.github.io/ubuntu/archive.key | sudo apt-key add -
    echo "deb https://pmateusz.github.io/ubuntu trusty main" | sudo tee -a /etc/apt/sources.list
    ```

    If you are using a different operating system you may need to generate the unofficial packages yourself if reuse of Ubuntu Trusty or Debian Stretch packages is impossible.

1. Install the following packages.
    ```shell
    sudo apt-get update
    sudo apt-get install --assume-yes \
    build-essential \
    cmake \
    g++-6 \
    git \
    libboost-all-dev \
    libbz2-dev \
    libglog-dev \
    libgtest-dev \
    liblua5.2-dev \
    libortools-dev \
    libsparsehash-dev \
    libtbb-dev \
    libxml2-dev \
    libzip-dev \
    locales \
    lua5.2 \
    pkg-config \
    python3-pip \
    unixodbc-dev
    ```

1. Install Gurobi 7.5.1.
    > Gurobi symbols are reuquired for the successful compilation of executables that depend on the     Google Operations Research toolbox. The current version of the Rows project does not use Gurobi.
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
    echo  'deb [arch=amd64] https://packages.microsoft.com/debian/8/prod jessie main' >     /etc/apt/sources.list.d/mssql-release.list
    curl https://packages.microsoft.com/keys/microsoft.asc | sudo apt-key add -
    sudo apt update
    sudo apt install msodbcsql mssql-tools
    wget "http://security.debian.org/debian-security/pool/updates/main/o/openssl/libssl1.0.0_1.0.1t-1    +deb8u7_amd64.deb"
    sudo dpkg -i libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb
    rm libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb
    export PATH="$PATH:/opt/mssql-tools/bin"
    ```
    > The `libssl1.0.0_1.0.1t-1+deb8u7_amd64.deb` package is installed as a workaround for the issue described in the blog [MSSQL ODBC Client on Debian 9 Stretch](https://emacstragic.net/2017/11/06/mssql-odbc-client-on-debian-9-stretch/).

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

1. Test the Microsoft ODBC Driver for SQL Server works by printing help output of the `sqlcmd` utility.
    ```shell
    sqlcmd
    ```

1. Clone the Rows project repository.
    ```shell
    git clone https://github.com/pmateusz/cordia.git
    ```

    > Remaining commands should be executed from the project root directory unless stated otherwise.
    > The project should be cloned in to the directory `~/dev/cordia` until the issue     https://github.com/pmateusz/cordia/issues/60 is fixed.

1. Install the packages required by the Python component.
    ```shell
    pip3 install -r requirements.txt
    ```

1. Generate make files.
    ```shell
    mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    ```

1. Remove the gflags library if it was installed by other packages.
    ```shell
    apt-get remove libgflags-dev
    ```
    This step is required to avoid runtime problems after the library is linked dynamically. The     gflags project will be checked out and compiled during the build.

1. Compile the source code.
    ```shell
    cd build
    cmake --build . --target build_all
    ```

1. Generate a traffic map for pedestrians.
    ```shell
    mkdir data && cd data
    wget http://download.geofabrik.de/europe/great-britain/scotland-latest.osm.pbf
    ../build/external/osrm-install/bin/osrm-extract -p ../build/external/osrm-install/share/osrm/profiles/foot.lua scotland-latest.osm.pbf
    ../build/external/osrm-install/bin/osrm-partition scotland-latest.osrm
    ../build/external/osrm-install/bin/osrm-customize scotland-latest.osrm
    ```

1. Create the `.dev` file that contains password to the database.

1. Apply workarounds for known issues: https://github.com/pmateusz/cordia/issues/60
    ```shell
    mkdir -p data/cordia/data/cordia
    touch data/cordia/data/cordia/location_cache.json
    echo "{}" >> settings.json
    ```

### Examples

Print help.
```shell
./rows_cli.py --help
```

Print help for a specific command.
```shell
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
