#!/bin/bash

if [ -d "pycharm-idea" ]; then
    echo "Environment is already set up for C++ development"
    exit 1
fi

mv ".idea" "pycharm-idea"
if [ -d "clion-idea" ]; then
    mv "clion-idea" ".idea"
else
    rm -Rf ".idea"
fi
