# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals


def loader(kind, path, config, params, loaded_tasks):
    """
    Generate inputs implementing beetmover jobs.  These depend on nightly build
    and signing jobs and transfer the artifacts to S3 after build and signing
    are completed.
    """
    if config.get('kind-dependencies', []) != ["repackage"]:
        raise Exception("Beetmover_repackage kinds must depend on repackage builds")
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

        beetmover_repackage_task = {'dependent-task': task}

        yield beetmover_repackage_task
