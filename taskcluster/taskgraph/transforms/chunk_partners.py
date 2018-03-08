# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the repackage task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import copy

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.scriptworker import get_release_config

transforms = TransformSequence()


@transforms.add
def split_partners(config, jobs):
    release_config = get_release_config(config)

    for job in jobs:
        dep_job = job['dependent-task']
        for partner, config in release_config["partner_config"].iteritems():
            if dep_job.attributes["build_platform"] not in config["platforms"]:
                continue
            for locale in config["locales"]:
                repack_id = "{}-{}".format(partner, locale)

            partner_job = copy.deepcopy(job)  # don't overwrite dict values here
            if 'extra' not in partner_job:
                partner_job['extra'] = {}
            partner_job['extra']['repack_id'] = repack_id

            treeherder = job.get('treeherder', {})
            treeherder['symbol'] = 'REMOVEME-Rpk({})'.format(repack_id)
            dep_th_platform = dep_job.task.get('extra', {}).get(
                'treeherder', {}).get('machine', {}).get('platform', '')
            treeherder['platform'] = "{}/opt".format(dep_th_platform)
            treeherder['tier'] = 1
            treeherder['kind'] = 'build'
            partner_job['treeherder'] = treeherder

            yield partner_job
