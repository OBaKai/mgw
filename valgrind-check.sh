#!/bin/bash

PROJECT_ROOT_PATH=`pwd`
PROJECT_PATH=$(pwd)
export LD_LIBRARY_PATH=$PROJECT_PATH/deps/lib/x86:$PROJECT_PATH/install/lib

valgrind --leak-check=full --show-leak-kinds=all \
--log-file=./valgrind_report.log --show-reachable=no \
--track-origins=yes \
$PROJECT_ROOT_PATH/install/bin/mgw -c $PROJECT_ROOT_PATH/install/bin/mgw-config.json