# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the signing task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence

ARTIFACT_URL = 'https://queue.taskcluster.net/v1/task/<{}>/artifacts/{}'

transforms = TransformSequence()


@transforms.add
def make_task_description(config, tasks):
    for task in tasks:
        task['label'] = task['build-label'].replace("build-", "signing-")
        task['description'] = task['description'].replace("build-", "signing-")

        artifacts = []
        if 'android' in task['build-platform']:
            artifacts = ['public/build/target.apk', 'public/build/en-US/target.apk']
        else:
            artifacts = ['public/build/target.tar.bz2']
        unsigned_artifacts = []
        for artifact in artifacts:
            url = {"task-reference": ARTIFACT_URL.format('build', artifact)}
            unsigned_artifacts.append(url)

        task['worker-type'] = "scriptworker-prov-v1/signing-linux-v1"
        task['worker'] = {'implementation': 'scriptworker-signing',
                          'unsigned-artifacts': unsigned_artifacts}

        signing_format = "gpg" if "linux" in task['label'] else "jar"
        signing_format_scope = "project:releng:signing:format:" + signing_format
        task['scopes'] = ["project:releng:signing:cert:nightly-signing",
                          signing_format_scope]

        task['dependencies'] = {'build': task['build-label']}
        attributes = task.setdefault('attributes', {})
        attributes['nightly'] = True
        attributes['build_platform'] = task['build-platform']
        attributes['build_type'] = task['build-type']
        task['run-on-projects'] = task['build-run-on-projects']

        # delete stuff that's not part of a task description
        del task['build-label']
        del task['build-type']
        del task['build-platform']
        del task['build-run-on-projects']

        yield task
