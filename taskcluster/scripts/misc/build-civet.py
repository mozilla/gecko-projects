#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import

import os
import sys
import subprocess
import taskcluster

# This script is for setting up civet, then building clang.
civet_revision = '7ac0d8ae71d3fb2eb4db8d3c173f46a426b0c2c8'

original_path = os.getcwd()

os.chdir(os.environ['GECKO_PATH'])
subprocess.call(['git', 'clone', 'https://github.com/tomrittervg/GSOC2020.git'])
os.chdir('GSOC2020')
subprocess.call(['git', 'checkout', civet_revision])
os.chdir(original_path)

build_clang = [os.environ['GECKO_PATH'] + '/taskcluster/scripts/misc/build-clang.sh']
subprocess.call(build_clang + sys.argv[1:])
