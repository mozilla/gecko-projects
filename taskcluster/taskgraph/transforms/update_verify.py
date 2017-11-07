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
        release_config = get_release_config(config)
        release_tag = "{}_{}_RELEASE".format(task["extra"]["product"].upper(), release_config["version"].replace(".", "_"))

        for this_chunk in range(1, total_chunks+1):
            chunked = deepcopy(task)
            chunked["label"] = "release-update-verify-{}-{}/{}".format(chunked["name"], this_chunk, total_chunks)
            if not chunked["worker"].get("env"):
                chunked["worker"]["env"] = {}
            chunked["worker"]["env"]["CHANNEL"] = release_config["update_verify_channel"]
            chunked["worker"]["env"]["VERIFY_CONFIG"] = release_config["update_verify_configs"][platform]
            chunked["worker"]["command"] = [
                "/bin/bash",
                "-c",
                "hg clone {} tools && cd tools && hg up -r {} && cd .. && tools/scripts/release/updates/chunked-verify.sh UNUSED UNUSED {} {}".format(
                    release_config["build_tools_repo"],
                    release_tag,
                    this_chunk,
                    total_chunks,
                )
            ]
            yield chunked
