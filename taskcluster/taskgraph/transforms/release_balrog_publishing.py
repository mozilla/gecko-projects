# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Add from parameters.yml into Balrog publishing tasks.
"""

from __future__ import absolute_import, print_function, unicode_literals

import os

from taskgraph.transforms.base import TransformSequence

transforms = TransformSequence()


@transforms.add
def add_release_eta(config, jobs):
    for job in jobs:
        if os.environ.get('RELEASE_ETA'):
            job['run']['release-eta'] = os.environ['RELEASE_ETA']

        yield job
