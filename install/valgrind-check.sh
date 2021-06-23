#!/bin/bash
valgrind --leak-check=full --show-leak-kinds=all --log-file=./valgrind_report.log --show-reachable=no --track-origins=yes ./mgw.out
