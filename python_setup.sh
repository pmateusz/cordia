#!/bin/bash

if [ -d "clion-idea" ]; then
    echo "Environment is already set up for Python development"
    exit 1
fi

mv ".idea" "clion-idea"
if [ -d "pycharm-idea" ]; then
    mv "pycharm-idea" ".idea"
else
    rm -Rf ".idea"
fi