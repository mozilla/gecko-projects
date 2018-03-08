# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the signing task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.scriptworker import get_release_config
from taskgraph.util.signed_artifacts import generate_specifications_of_artifacts_to_sign


transforms = TransformSequence()


@transforms.add
def define_upstream_artifacts(config, jobs):
    release_config = get_release_config(config)

    for job in jobs:
        dep_job = job['dependent-task']
        build_platform = dep_job.attributes.get('build_platform')

        repack_ids = []
        if "eme" in config.kind:
            repack_ids.append("eme-free")
        else:
            for partner, cfg in release_config["partner_config"].iteritems():
                if build_platform not in cfg["platforms"]:
                    continue
                for locale in cfg["locales"]:
                    repack_ids.append("{}-{}".format(partner, locale))

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
            'taskId': {'task-reference': '<{}>'.format(job['depname'])},
            'taskType': 'build',
            'paths': [
                path_template.format(locale=repack_id)
                for repack_id in repack_ids
                for path_template in spec['artifacts']
            ],
            'formats': spec['formats'],
        } for spec in artifacts_specifications]

        yield job
