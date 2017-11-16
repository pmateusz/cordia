# Robust Optimization for Workforce Scheduling [![Build Status](https://travis-ci.com/pmateusz/cordia.svg?token=BGZD8zC9w6stvzyuFQhA&branch=master)](https://travis-ci.com/pmateusz/cordia)

This is the source code repository for the Robust Optimization for Workforce Scheduling Project.

## Quick Start

### Building from Source

Install required dependencies.
```shell
sudo apt install build-essential git cmake pkg-config \
libbz2-dev libxml2-dev libzip-dev libboost-all-dev \
lua5.2 liblua5.2-dev libtbb-dev
```

Compile the C++ components.
```shell
mkdir -p build
cd build
cmake ..
cmake --build .
```

Run tests of the Python components.
```shell
find . -iname "*.py" | xargs python3 -m pytest
```

Run checkstyle of the Python components.
```shell
find rows* -iname "*.py" | xargs python3 -m pylint
```


## Contribute

The project is supported by a continuous integration pipeline hosted by [Travis CI](https://travis-ci.com/pmateusz/cordia). Review the [.travis.yml](.travis.yml) file
for the most up to date instructions about development environment set up.

## Project Documentation
Refer to [the project wiki pages](https://github.com/pmateusz/cordia/wiki) for more information on this subject.
