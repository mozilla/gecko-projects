# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals


def loader(kind, path, config, params, loaded_tasks):
    """
    Generate inputs implementing PushApk jobs. These depend on signed multi-locales nightly builds.
    """
    if config.get('kind-dependencies', []) != ["build-signing"]:
        raise Exception("PushApk kinds must depend on build-signing")
    for task in loaded_tasks:
        if not task.attributes.get('nightly'):
            continue
        if task.kind not in config.get('kind-dependencies'):
            continue
        push_apk_task = {'dependent-task': task}

        yield push_apk_task
