# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals
import copy

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.partials import get_previsous_versions
from taskgraph.util.scriptworker import get_release_config
from taskgraph.util.schema import (
    resolve_keyed_by,
)

import logging
logger = logging.getLogger(__name__)

transforms = TransformSequence()


@transforms.add
def add_previous_versions(config, jobs):
    release_history = config.params.get('release_history')
    # If no release history, then don't generate extra parameters
    if not release_history:
        for job in jobs:
            yield job
    else:
        previous_versions = get_previsous_versions(release_history)
        extra_params = []
        for previous_version in previous_versions:
            extra_params.append("previous-version={}".format(previous_version))

        for job in jobs:
            job["run"].setdefault("options", [])
            job["run"]["options"].extend(extra_params)
            yield job


@transforms.add
def handle_keyed_by(config, jobs):
    """Resolve fields that can be keyed by project, etc."""
    fields = [
        "run.config",
    ]
    for job in jobs:
        job = copy.deepcopy(job)  # don't overwrite dict values here
        for field in fields:
            resolve_keyed_by(item=job, field=field, item_name=job['name'], project=config.params['project'])
        yield job


@transforms.add
def interpolate(config, jobs):
    release_config = get_release_config(config)
    for job in jobs:
        mh_options = list(job["run"]["options"])
        job["run"]["options"] = [
            option.format(version=release_config["version"])
            for option in mh_options
        ]
        yield job
