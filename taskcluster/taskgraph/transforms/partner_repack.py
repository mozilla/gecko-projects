# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partner repack task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.partners import get_partner_config_by_kind, check_if_partners_enabled
from taskgraph.util.taskcluster import get_artifact_prefix


transforms = TransformSequence()

import logging
log = logging.getLogger(__name__)

transforms.add(check_if_partners_enabled)


@transforms.add
def resolve_properties(config, tasks):
    for task in tasks:
        for property in ("REPACK_MANIFESTS_URL", ):
            property = "worker.env.{}".format(property)
            resolve_keyed_by(task, property, property, **config.params)
            yield task


@transforms.add
def make_label(config, tasks):
    for task in tasks:
        task['label'] = "{}-{}".format(config.kind, task['name'])
        yield task


@transforms.add
def add_command(config, tasks):
    for task in tasks:
        artifact_prefix = get_artifact_prefix(task)
        partner_configs = get_partner_config_by_kind(
            config, config.kind
        )
        build_task = None
        for dep in task.get("dependencies", {}).keys():
            if "build" in dep:
                build_task = dep
        if not build_task:
            raise Exception("Couldn't find build task")

        if not task["worker"].get("artifacts"):
            task["worker"]["artifacts"] = []

        repack_ids = []
        for partner, partner_config in partner_configs.iteritems():
            # TODO clean up configs? Some have a {} as the config
            for sub_partner, cfg in partner_config.iteritems():
                if task["attributes"]["build_platform"] not in cfg.get("platforms", []):
                    continue
                for locale in cfg.get("locales", []):
                    repack_ids.append("{}-{}".format(sub_partner, locale))

        if 'mac' in task['attributes']['build_platform']:
            # TODO
            # - get_taskcluster_artifact_prefix ?
            # - get taskId from deps ?
            # - s,eme-free,$partner, ?
            # - We could potentially add the URL(s) to an env var, and have
            #   the script do the downloading, which would allow for retries
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/YfqwST9zRo2-QddTuetDQA"\
                           "/runs/0/artifacts/public/build/target.dmg > eme-free.dmg"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/target.dmg".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.dmg".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp eme-free.dmg {}.dmg".format(repack_id)

        elif 'win32' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/aSYhH66QRDyYwhyjmt2wGQ"\
                           "/runs/0/artifacts/public/build/setup.exe > eme-free.exe"
            download_cmd += " && curl -L https://queue.taskcluster.net/v1/task"\
                            "/aSYhH66QRDyYwhyjmt2wGQ/runs/0/artifacts/public/build/target.zip "\
                            "> eme-free.zip"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/setup.exe".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.exe".format(repack_id),
                    "type": "file"
                })
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/target.zip".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.zip".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp eme-free.exe {}.exe".format(repack_id)
                download_cmd += " && cp eme-free.zip {}.zip".format(repack_id)

        elif 'win64' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/QKLcYMFQTCiMWz1rlRlSoA"\
                           "/runs/0/artifacts/public/build/setup.exe > eme-free.exe"
            download_cmd += " && curl -L https://queue.taskcluster.net/v1/task"\
                            "/QKLcYMFQTCiMWz1rlRlSoA/runs/0/artifacts/public/build/target.zip "\
                            "> eme-free.zip"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/setup.exe".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.exe".format(repack_id),
                    "type": "file"
                })
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/target.zip".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.zip".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp eme-free.exe {}.exe".format(repack_id)
                download_cmd += " && cp eme-free.zip {}.zip".format(repack_id)

        elif 'linux-' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/Q8txUznsRke5OQ-CVZuB1Q"\
                           "/runs/0/artifacts/public/build/target.tar.bz2 > eme-free.tar.bz2"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/target.tar.bz2".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.tar.bz2".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp eme-free.tar.bz2 {}.tar.bz2".format(repack_id)

        elif 'linux64-' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/FkBd2xC_SSeBG4_ihZG5Zw"\
                           "/runs/0/artifacts/public/build/target.tar.bz2 > eme-free.tar.bz2"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "{}/{}/target.tar.bz2".format(artifact_prefix, repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.tar.bz2".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp eme-free.tar.bz2 {}.tar.bz2".format(repack_id)

        task["run"]["command"] = " ".join([
            "cd", "/builds/worker/checkouts/gecko", "&&", download_cmd
        ])
        yield task
