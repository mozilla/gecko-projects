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
def make_signing_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']

        job['label'] = dep_job.label.replace("build-", "signing-")

        artifacts = []
        if 'android' in dep_job.attributes.get('build_platform'):
            artifacts = ['public/build/target.apk', 'public/build/en-US/target.apk']
        else:
            artifacts = ['public/build/target.tar.bz2']
        unsigned_artifacts = []
        for artifact in artifacts:
            url = {"task-reference": ARTIFACT_URL.format('build', artifact)}
            unsigned_artifacts.append(url)

        job['unsigned-artifacts'] = unsigned_artifacts
        job['signing-format'] = "gpg" if "linux" in dep_job.label else "jar"

        yield job
