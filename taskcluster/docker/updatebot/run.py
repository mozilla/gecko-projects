#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function

import sys
sys.path.append('/builds/worker/checkouts/gecko/third_party/python')
sys.path.append('.')

import os
import stat
import shutil
import requests
import subprocess
import taskcluster


def get_secret(name):
    secret = None
    if 'TASK_ID' in os.environ:
        secrets_url = 'http://taskcluster/secrets/v1/secret/project/updatebot/' + name
        res = requests.get(secrets_url)
        res.raise_for_status()
        secret = res.json()
        secret = secret['secret'] if 'secret' in secret else None
    else:
        secrets = taskcluster.Secrets(taskcluster.optionsFromEnvironment())
        secret = secrets.get('project/civet/' + name)['secret']
    return secret[name]


# revision = '30e6fb8be964c930c0d5d01b7761c1e970596e8f'

original_path = os.getcwd()

bugzilla_api_key = get_secret('bugzilla-api-key')
# phabricator_token = get_secret('phabricator-token')
try_sshkey = get_secret('try-sshkey')
# database_password = get_secret('database_password')

# Checkout =================================================

os.chdir("/builds/worker")
subprocess.check_call(['git', 'clone',
                       'https://github.com/mozilla-services/third-party-library-update.git'])
os.chdir('third-party-library-update')
# subprocess.check_call(['git', 'checkout', revision])
shutil.copyfile("apikey.py.example", "apikey.py")
subprocess.check_call(["sed", "-i", "s/<foobar>/" + bugzilla_api_key + "/", "apikey.py"])

# from fileBug import fileBug
# fileBug("Core", "ImageLib", "Testing From Automation", "Test Description")

# Set Up SSH =============================================
sshkey = open("id_rsa", "w")
sshkey.write(try_sshkey)
sshkey.close()
os.chmod("id_rsa", stat.S_IRWXU)

# Vendor =================================================
from components import find_library_metadata, vendor, find_release_version, \
    file_bug, commit, submit_to_try, commentOnBug

library = "dav1d"
library = find_library_metadata("dav1d")

os.chdir("/builds/worker/checkouts/gecko")

vendor(library)

new_release_version = find_release_version(library)
if not new_release_version:
    print("Could not find a new release version string")
    sys.exit(0)

bug_id = file_bug(library, new_release_version)

commit(library, bug_id, new_release_version)

try_run = submit_to_try(library)

commentOnBug(bug_id, try_run)
