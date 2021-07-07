#!/bin/sh

# Travis CI uses this script to test code quality.

# Exit on any nonzero return code.
set -e

complexity --version
# Get the list of files to check. Only check library files and exclude tests.
# Run complexity with a threshold of 8.
find ../libraries/ \( -name '*.c' ! -name *tests*.c \) -type f | \
xargs complexity -n 1.9 -s 20 --scores --threshold=8 #--horrid-threshold=8
