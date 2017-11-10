# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.scriptworker import get_release_config

transforms = TransformSequence()


@transforms.add
def add_command(config, tasks):
    for task in tasks:
        release_config = get_release_config(config)
        release_tag = "{}_{}_RELEASE".format(
            task["extra"]["product"].upper(),
            release_config["version"].replace(".", "_")
        )
        #final_verify_configs = release_config["update_verify_configs"].values()

        if not task["worker"].get("env"):
            task["worker"]["env"] = {}
        task["worker"]["command"] = [
            "/bin/bash",
            "-c",
            "hg clone {} tools && cd tools && hg up -r {} && cd release && ".format(
                "FIXME",
                release_tag,
            ) +
            "./final-verification.sh FIXME"
        ]
        yield task
