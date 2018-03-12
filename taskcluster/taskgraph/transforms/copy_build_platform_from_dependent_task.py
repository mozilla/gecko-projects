# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the repackage task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence

transforms = TransformSequence()


@transforms.add
def copy_build_platform(config, jobs):
    for job in jobs:
        job.setdefault('attributes', {})
        job['attributes']['build_platform'] = \
            job['dependent-task'].attributes.get('build_platform')
        job['attributes']['build_type'] = job['dependent-task'].attributes.get('build_type')
        yield job
