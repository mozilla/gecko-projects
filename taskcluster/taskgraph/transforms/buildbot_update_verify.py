# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from copy import deepcopy

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.scriptworker import get_release_config

transforms = TransformSequence()


@transforms.add
def add_command(config, tasks):
    for task in tasks:
        total_chunks = task["extra"]["chunks"]
        platform = task["attributes"]["build_platform"]
        product = task["extra"]["product"]
        buildername = "release-{branch}_" + product + "_" + platform + \
            "_update_verify"
        release_config = get_release_config(config)

        for this_chunk in range(1, total_chunks+1):
            chunked = deepcopy(task)
            chunked["scopes"] = [
                "project:releng:buildbot-bridge:builder-name:{}".format(
                    buildername
                )
            ]
            chunked["label"] = "release-update-verify-{}-{}/{}".format(
                chunked["name"], this_chunk, total_chunks
            )
            chunked["run"]["buildername"] = buildername
            if not chunked["run"].get("properties"):
                chunked["run"]["properties"] = {}
            chunked["run"]["properties"]["NO_BBCONFIG"] = "1"
            chunked["run"]["properties"]["CHANNEL"] = \
                release_config["update_verify_channel"]
            chunked["run"]["properties"]["VERIFY_CONFIG"] = \
                release_config["update_verify_configs"][platform]
            chunked["run"]["properties"]["THIS_CHUNK"] = str(this_chunk)
            chunked["run"]["properties"]["TOTAL_CHUNKS"] = str(total_chunks)
            yield chunked
