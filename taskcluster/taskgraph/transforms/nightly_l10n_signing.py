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
def make_signing_description(config, jobs):
    for job in jobs:
        job['depname'] = 'unsigned-repack'

        dep_job = job['dependent-task']
        dep_platform = dep_job.attributes.get('build_platform')

        job['unsigned-artifacts'] = []
        if 'android' in dep_platform:
            job_specs = [
                {
                    'extensions': ['.apk'],
                    'format': 'jar',
                },
            ]
        else:
            job_specs = [
                {
                    'extensions': ['.tar.bz2', '.checksums'],
                    'format': 'gpg',
                }, {
                    'extensions': ['complete.mar'],
                    'format': 'mar',
                }
            ]
        for spec in job_specs:
            fmt = spec['format']
            for locale in dep_job.attributes.get('chunk_locales', []):
                for ext in spec['extensions']:
                    filename = '{}/target{}'.format(locale, ext)
                    job['unsigned-artifacts'].append({
                        'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                              filename)
                        })

            job['signing-format'] = fmt

            label = dep_job.label.replace("nightly-l10n-",
                                          "signing-l10n-{}-".format(fmt))
            job['label'] = label

            # add the chunk number to the TH symbol
            symbol = 'Ns{}{}'.format(dep_job.attributes.get('l10n_chunk'),
                                     fmt.title()[:1])
            group = 'tc-L10n'

            job['treeherder'] = {
                'symbol': join_symbol(group, symbol),
            }
            yield job
