# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partner repack task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.taskcluster import get_taskcluster_artifact_prefix


transforms = TransformSequence()


@transforms.add
def resolve_properties(config, tasks):
    for task in tasks:
        for property in ("REPACK_MANIFESTS_URL", ):
            property = "worker.env.{}".format(property)
            resolve_keyed_by(task, property, property, **config.params)
            yield task


@transforms.add
def add_command(config, tasks):
    for task in tasks:
        build_task = None
        for dep in task.get("dependencies", {}).keys():
            if "build" in dep:
                build_task = dep
        if not build_task:
            raise Exception("Couldn't find build task")

        task["run"]["command"] = " ".join([
            "cd", "/builds/worker/checkouts/gecko", "&&",
            "curl -L {}target.dmg > partner1.dmg".format(
                get_taskcluster_artifact_prefix("<{}>".format(build_task))
            )
        ])

        yield task
