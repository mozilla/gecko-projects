# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import urlparse

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.scriptworker import get_release_config

transforms = TransformSequence()


@transforms.add
def add_command(config, tasks):
    keyed_by_args = [
        "channel",
        "archive-prefix",
        "previous-archive-prefix",
        "aus-server",
        "include-version",
        "mar-channel-id-override",
        "last-watershed",
    ]
    optional_args = [
        "updater-platform",
    ]

    for task in tasks:
        release_config = get_release_config(config)
        task["description"] = "generate update verify config for {}".format(
            task["attributes"]["build_platform"]
        )

        command = [
            "cd", "/builds/worker/checkouts/gecko", "&&"
            "./mach", "python",
            "testing/mozharness/scripts/release/update-verify-config-creator.py",
            "--config", "internal_pypi.py",
            "--product", task["extra"]["product"],
            "--stage-product", task["shipping-product"],
            "--app-name", task["extra"]["app-name"],
            "--platform", task["extra"]["platform"],
            "--to-version", release_config["version"],
            "--to-app-version", release_config["appVersion"],
            "--to-build-number", str(release_config["build_number"]),
            "--to-buildid", config.params["moz_build_date"],
            "--to-revision", config.params["head_rev"],
            "--output-file", "update-verify.cfg",
        ]

        repo_path = urlparse.urlsplit(config.params["head_repository"]).path.lstrip("/")
        command.extend(["--repo-path", repo_path])

        if release_config.get("partial_versions"):
            for partial in release_config["partial_versions"].split(","):
                command.extend(["--partial-version", partial.split("build")[0]])

        for arg in optional_args:
            if task["extra"].get(arg):
                command.append("--{}".format(arg))
                command.append(task["extra"][arg])

        for arg in keyed_by_args:
            thing = "extra.{}".format(arg)
            resolve_keyed_by(task, thing, thing, **config.params)
            # ignore things that resolved to null
            if task["extra"].get(arg):
                command.append("--{}".format(arg))
                command.append(task["extra"][arg])

        task["run"]["command"] = " ".join(command)

        yield task
