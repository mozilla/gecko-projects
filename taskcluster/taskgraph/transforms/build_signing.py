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
def add_signed_routes(config, jobs):
    """Add routes corresponding to the routes of the build task
       this corresponds to, with .signed inserted, for all gecko.v2 routes"""

    for job in jobs:
        dep_job = job['dependent-task']

        job['routes'] = []
        if dep_job.attributes.get('nightly'):
            for dep_route in dep_job.task.get('routes', []):
                if not dep_route.startswith('index.gecko.v2'):
                    continue
                branch = dep_route.split(".")[3]
                rest = ".".join(dep_route.split(".")[4:])
                job['routes'].append(
                    'index.gecko.v2.{}.signed-nightly.{}'.format(branch, rest))
        if 'EME-free' in dep_job.label:
            for dep_route in dep_job.task.get('routes', []):
                if not dep_route.startswith('index.releases.v1'):
                    continue
                branch = dep_route.split(".")[3]
                rest = ".".join(dep_route.split(".")[4:])
                job['routes'].append(
                    'index.releases.v1.{}.signed-eme-free.{}'.format(branch, rest))

        yield job


@transforms.add
def define_upstream_artifacts(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        build_platform = dep_job.attributes.get('build_platform')

        # Windows and Linux partner repacks have no internal signing to be done
        if 'partner-repack' in config.kind:
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
                # TODO: what should this be set to? what does it imply?
                'taskType': 'build',
                'paths': [
                    path_template.format(locale=repack_id)
                    # TODO: this should get passed in. for eme it's probably just eme-free
                    for repack_id in ('partner1',)
                    for path_template in spec['artifacts']
                ],
                'formats': spec['formats'],
            } for spec in artifacts_specifications]

        else:
            artifacts_specifications = generate_specifications_of_artifacts_to_sign(
                build_platform,
                dep_job.attributes.get('nightly'),
                keep_locale_template=False,
                kind=config.kind,
            )

            if 'android' in build_platform:
                # We're in the job that creates both multilocale and en-US APKs
                artifacts_specifications[0]['artifacts'].append('public/build/en-US/target.apk')

            job['upstream-artifacts'] = [{
                'taskId': {'task-reference': '<build>'},
                'taskType': 'build',
                'paths': spec['artifacts'],
                'formats': spec['formats'],
            } for spec in artifacts_specifications]

        yield job
