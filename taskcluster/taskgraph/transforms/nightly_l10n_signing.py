# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the signing task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.treeherder import join_symbol

ARTIFACT_URL = 'https://queue.taskcluster.net/v1/task/<{}>/artifacts/public/build/{}'

transforms = TransformSequence()


@transforms.add
def add_signing_artifacts(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        dep_platform = dep_job.attributes.get('build_platform')

        job['unsigned-artifacts'] = []
        extension = '.apk' if 'android' in dep_platform else '.tar.bz2'
        for locale in dep_job.attributes.get('chunk_locales', []):
            filename = '{}/target{}'.format(locale, extension)
            job['unsigned-artifacts'].append({
                'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                      filename)
                })
            if 'tar.bz2' == filename[-7:]:
                # Add the checksums file to be signed for linux
                checksums_file = filename[:-7] + "checksums"
                job['unsigned-artifacts'].append({
                    'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                          checksums_file)
                    })
        yield job


@transforms.add
def make_signing_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']

        job['label'] = dep_job.label.replace("nightly-l10n-", "signing-l10n-")

        job['depname'] = 'unsigned-repack'
        job['signing-format'] = "gpg" if "linux" in dep_job.label else "jar"

        # add the chunk number to the TH symbol
        symbol = 'Ns{}'.format(dep_job.attributes.get('l10n_chunk'))
        group = 'tc-L10n'

        job['treeherder'] = {
            'symbol': join_symbol(group, symbol),
        }
        yield job
