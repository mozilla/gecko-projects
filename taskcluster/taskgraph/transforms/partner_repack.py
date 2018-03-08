# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partner repack task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.scriptworker import get_release_config


transforms = TransformSequence()


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
    release_config = get_release_config(config)

    for task in tasks:
        build_task = None
        for dep in task.get("dependencies", {}).keys():
            if "build" in dep:
                build_task = dep
        if not build_task:
            raise Exception("Couldn't find build task")

        if not task["worker"].get("artifacts"):
            task["worker"]["artifacts"] = []

        repack_ids = []
        for partner, cfg in release_config["partner_config"].iteritems():
            if task["attributes"]["build_platform"] not in cfg["platforms"]:
                continue
            for locale in cfg["locales"]:
                repack_ids.append("{}-{}".format(partner, locale))

        if 'mac' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/YfqwST9zRo2-QddTuetDQA"\
                           "/runs/0/artifacts/public/build/target.dmg > emefree.dmg"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/target.dmg".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.dmg".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp emefree.dmg {}.dmg".format(repack_id)

        elif 'win32' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/aSYhH66QRDyYwhyjmt2wGQ"\
                           "/runs/0/artifacts/public/build/setup.exe > emefree.exe"
            download_cmd += " && curl -L https://queue.taskcluster.net/v1/task"\
                            "/aSYhH66QRDyYwhyjmt2wGQ/runs/0/artifacts/public/build/target.zip "\
                            "> emefree.zip"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/setup.exe".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.exe".format(repack_id),
                    "type": "file"
                })
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/target.zip".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.zip".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp emefree.exe {}.exe".format(repack_id)
                download_cmd += " && cp emefree.zip {}.zip".format(repack_id)

        elif 'win64' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/QKLcYMFQTCiMWz1rlRlSoA"\
                           "/runs/0/artifacts/public/build/setup.exe > emefree.exe"
            download_cmd += " && curl -L https://queue.taskcluster.net/v1/task"\
                            "/QKLcYMFQTCiMWz1rlRlSoA/runs/0/artifacts/public/build/target.zip "\
                            "> emefree.zip"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/setup.exe".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.exe".format(repack_id),
                    "type": "file"
                })
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/target.zip".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.zip".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp emefree.exe {}.exe".format(repack_id)
                download_cmd += " && cp emefree.zip {}.zip".format(repack_id)

        elif 'linux-' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/Q8txUznsRke5OQ-CVZuB1Q"\
                           "/runs/0/artifacts/public/build/target.tar.bz2 > emefree.tar.bz2"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/target.tar.bz2".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.tar.bz2".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp emefree.tar.bz2 {}.tar.bz2".format(repack_id)

        elif 'linux64-' in task['attributes']['build_platform']:
            download_cmd = "curl -L https://queue.taskcluster.net/v1/task/FkBd2xC_SSeBG4_ihZG5Zw"\
                           "/runs/0/artifacts/public/build/target.tar.bz2 > emefree.tar.bz2"
            for repack_id in repack_ids:
                task["worker"]["artifacts"].append({
                    "name": "public/build/{}/target.tar.bz2".format(repack_id),
                    "path": "/builds/worker/checkouts/gecko/{}.tar.bz2".format(repack_id),
                    "type": "file"
                })
                download_cmd += " && cp emefree.tar.bz2 {}.tar.bz2".format(repack_id)

        task["run"]["command"] = " ".join([
            "cd", "/builds/worker/checkouts/gecko", "&&", download_cmd
        ])
        yield task
