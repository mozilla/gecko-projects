# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the signing task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.treeherder import join_symbol

ARTIFACT_URL = 'https://queue.taskcluster.net/v1/task/<{}>/artifacts/{}'

transforms = TransformSequence()


@transforms.add
def make_signing_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']

        if 'android' in dep_job.attributes.get('build_platform'):
            job_specs = [
                {
                    'artifacts': ['public/build/target.apk',
                                  'public/build/en-US/target.apk'],
                    'format': 'jar',
                },
            ]
        else:
            job_specs = [
                {
                    'artifacts': ['public/build/target.tar.bz2',
                                  'public/build/target.checksums'],
                    'format': 'gpg',
                }, {
                    'artifacts': ['public/build/target.complete.mar'],
                    'format': 'mar',
                }
            ]
        unsigned_artifacts = []
        for spec in job_specs:
            fmt = spec["format"]
            for artifact in spec["artifacts"]:
                url = {"task-reference": ARTIFACT_URL.format('build', artifact)}
                unsigned_artifacts.append(url)

            job['unsigned-artifacts'] = unsigned_artifacts
            job['signing-format'] = fmt

            label = dep_job.label.replace("build-", "signing-{}-".format(fmt))
            job['label'] = label

            # add the format character to the TH symbol
            symbol = 'Ns{}'.format(fmt.title()[:1])
            group = 'tc'

            job['treeherder'] = {
                'symbol': join_symbol(group, symbol),
            }
            yield job
