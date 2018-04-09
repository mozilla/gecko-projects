# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the repackage task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import copy

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.partners import get_partner_config_by_kind

transforms = TransformSequence()


@transforms.add
def chunk_partners(config, jobs):
    partner_configs = get_partner_config_by_kind(config, config.kind)

    for job in jobs:
        dep_job = job['dependent-task']
        for partner, partner_config in partner_configs.iteritems():
            for sub_partner, cfg in partner_config.iteritems():
                if dep_job.attributes["build_platform"] not in cfg.get("platforms", []):
                    continue
                for locale in cfg.get("locales", []):
                    repack_id = "{}-{}".format(sub_partner, locale)

                    partner_job = copy.deepcopy(job)  # don't overwrite dict values here
                    if 'extra' not in partner_job:
                        partner_job['extra'] = {}
                    partner_job['extra']['repack_id'] = repack_id

                    yield partner_job
