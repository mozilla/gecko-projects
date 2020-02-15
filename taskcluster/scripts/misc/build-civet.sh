#!/bin/bash
set -x -e -v

# This script is for setting up civet, then building clang.

ORIGPWD="$PWD"

cd $GECKO_PATH
git clone https://github.com/tomrittervg/GSOC2020.git
cd GSOC2020/
git checkout 7ac0d8ae71d3fb2eb4db8d3c173f46a426b0c2c8
cd $ORIGPWD

$GECKO_PATH/taskcluster/scripts/misc/build-clang.sh "$@"