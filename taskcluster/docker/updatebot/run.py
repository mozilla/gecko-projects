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

# Bump this number when you need to cause a commit for the job to re-run: 1

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
phabricator_token = get_secret('phabricator-token')
try_sshkey = get_secret('try-sshkey')
database_config = get_secret('database-password')

# Checkout =================================================

os.chdir("/builds/worker")
subprocess.check_call(['git', 'clone',
                       'https://github.com/mozilla-services/updatebot.git'])
os.chdir('updatebot')
# subprocess.check_call(['git', 'checkout', revision])
shutil.copyfile("apis/apikey.py.example", "apis/apikey.py")
subprocess.check_call(["sed", "-i", "s/<foobar>/" + bugzilla_api_key + "/", "apis/apikey.py"])

# Set Up SSH =============================================
sshkey = open("id_rsa", "w")
sshkey.write(try_sshkey)
sshkey.close()
os.chmod("id_rsa", stat.S_IRWXU)

# Set Up Phabricator =====================================
arcrc = open("/home/worker/.arcrc", "w")
towrite = """
{
  "hosts": {
    "https://phabricator.services.mozilla.com/api/": {
      "token": "TOKENHERE"
    }
  }
}
""".replace("TOKENHERE", phabricator_token)
arcrc.write(towrite)
arcrc.close()
os.chmod("/home/worker/.arcrc", stat.S_IRWXU)


# Vendor =================================================
os.chdir("/builds/worker/checkouts/gecko")

from automation import run
run(database_config)
