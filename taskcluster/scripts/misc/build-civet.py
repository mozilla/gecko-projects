#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function

import os
import sys
import shutil
import requests
import subprocess
import taskcluster

# This script is for setting up civet, then building clang.
civet_revision = '9def689af50bd35ffd28701b5c392205300bfe89'

original_path = os.getcwd()

# This secret MUST be in RSA format! See https://stackoverflow.com/q/54994641
sshkey = None
if 'TASK_ID' in os.environ:
    secrets_url = 'http://taskcluster/secrets/v1/secret/project/civet/github-deploy-key'
    res = requests.get(secrets_url)
    res.raise_for_status()
    secret = res.json()
    sshkey = secret['secret'] if 'secret' in secret else None
else:
    secrets = taskcluster.Secrets(taskcluster.optionsFromEnvironment())
    sshkey = secrets.get('project/civet/github-deploy-key')['secret']

key = sshkey['sshkey'].replace("\\n", "\n") + "\n"

f = open('civet-key', 'w')
f.write(key)
f.close()
os.chmod('civet-key', 0o600)

keypath = os.path.join(original_path, 'civet-key')
keyenv = {'GIT_SSH_COMMAND': 'ssh -o "StrictHostKeyChecking no" -i ' + keypath}

os.chdir(os.environ['GECKO_PATH'])
subprocess.check_call(['git', 'clone', 'git@github.com:mozilla-services/civet.git'], env=keyenv)
os.chdir('civet')
subprocess.check_call(['git', 'checkout', civet_revision])
os.chdir(original_path)

build_clang = [os.environ['GECKO_PATH'] + '/taskcluster/scripts/misc/build-clang.sh']
subprocess.check_call(build_clang + sys.argv[1:])

destdir = "/builds/worker/private-artifacts/"
os.mkdir(destdir)
shutil.move("/builds/worker/artifacts/clang-tidy.tar.xz", destdir + "clang-tidy.tar.xz")
