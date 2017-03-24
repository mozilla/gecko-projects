# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals


import logging
logger = logging.getLogger(__name__)


def loader(kind, path, config, params, loaded_tasks):
    """
    Generate tasks implementing repackage jobs.  These depend on build jobs
    and generate the package in the correct format after being signed by the
    build job
    """

    if (config.get('kind-dependencies', []) != ['build-signing']):
        raise Exception("Repackage signing tasks must depend on build-signing tasks")
    for task in loaded_tasks:
        if not task.attributes.get('nightly'):
            continue
        if task.kind not in config.get('kind-dependencies'):
            continue
        build_platform = task.attributes.get('build_platform')
        build_type = task.attributes.get('build_type')
        if not build_platform or not build_type:
            continue
        platform = "{}/{}".format(build_platform, build_type)
        only_platforms = config.get('only-for-build-platforms')
        if only_platforms and platform not in only_platforms:
            continue

        repackage_task = {'dependent-task': task}

        yield repackage_task
