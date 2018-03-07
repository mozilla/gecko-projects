# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the signing task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.signed_artifacts import generate_specifications_of_artifacts_to_sign


transforms = TransformSequence()


@transforms.add
def define_upstream_artifacts(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        build_platform = dep_job.attributes.get('build_platform')

        # TODO: this should get passed in. for eme it's probably just eme-free
        repack_ids = ('partner1', 'partner2')
        if "eme" in config.kind:
            repack_ids = ('emefree',)

        # Windows and Linux partner repacks have no internal signing to be done
        if 'win' in build_platform or 'linux' in build_platform:
            job['upstream-artifacts'] = []
            yield job
            continue

        artifacts_specifications = generate_specifications_of_artifacts_to_sign(
            build_platform,
            dep_job.attributes.get('nightly'),
            keep_locale_template=True,
            kind=config.kind,
        )
        job['upstream-artifacts'] = [{
            'taskId': {'task-reference': '<release-partner-repack>'},
            'taskType': 'build',
            'paths': [
                path_template.format(locale=repack_id)
                for repack_id in repack_ids
                for path_template in spec['artifacts']
            ],
            'formats': spec['formats'],
        } for spec in artifacts_specifications]

        yield job
