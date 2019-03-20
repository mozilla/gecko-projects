# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import hashlib
import json
import os
import shutil

from mozboot.util import get_state_dir

from ..cli import BaseTryParser
from ..push import push_to_try, history_path, build


class AgainParser(BaseTryParser):
    name = 'again'
    arguments = [
        [['--index'],
         {'default': 0,
          'type': int,
          'help': "Index of entry in the history to re-push, "
                  "where '0' is the most recent (default 0). "
                  "Use --list to display indices.",
          }],
        [['--list'],
         {'default': False,
          'action': 'store_true',
          'dest': 'list_configs',
          'help': "Display history and exit",
          }],
        [['--purge'],
         {'default': False,
          'action': 'store_true',
          'help': "Remove all history and exit",
          }],
    ]
    common_groups = ['push']


def migrate_old_history():
    """Try to move existing history files from the old locations
    to the new one.
    """
    old_history_dir = os.path.join(get_state_dir(), 'history')
    topsrcdir_hash = hashlib.sha256(os.path.abspath(build.topsrcdir)).hexdigest()
    old_history_paths = filter(os.path.isfile, [
        os.path.join(old_history_dir, topsrcdir_hash, 'try_task_configs.json'),
        os.path.join(old_history_dir, 'try_task_configs.json'),
    ])

    for path in old_history_paths:
        if os.path.isfile(history_path):
            break

        history_dir = os.path.dirname(history_path)
        if not os.path.isdir(history_dir):
            os.makedirs(history_dir)

        shutil.move(path, history_path)

    if os.path.isdir(old_history_dir):
        shutil.rmtree(old_history_dir)


def run(index=0, purge=False, list_configs=False, message='{msg}', **pushargs):
    # TODO: Remove after January 1st, 2020.
    migrate_old_history()

    if purge:
        os.remove(history_path)
        return

    if not os.path.isfile(history_path):
        print("error: history file not found: {}".format(history_path))
        return 1

    with open(history_path, 'r') as fh:
        history = fh.readlines()

    if list_configs:
        for i, data in enumerate(history):
            msg, config = json.loads(data)
            print('{}. {}'.format(i, msg))
        return

    msg, try_task_config = json.loads(history[index])
    return push_to_try('again', message.format(msg=msg),
                       try_task_config=try_task_config, **pushargs)
