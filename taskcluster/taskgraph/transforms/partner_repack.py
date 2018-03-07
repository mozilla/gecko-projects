# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partner repack task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by


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
    for task in tasks:
        build_task = None
        for dep in task.get("dependencies", {}).keys():
            if "build" in dep:
                build_task = dep
        if not build_task:
            raise Exception("Couldn't find build task")

        if 'mac' in task['attributes']['build_platform']:
            task["run"]["command"] = " ".join([
                "cd", "/builds/worker/checkouts/gecko", "&&",
                "curl -L https://queue.taskcluster.net/v1/task/YfqwST9zRo2-QddTuetDQA/runs/0/artifacts/public/build/target.dmg > partner1.dmg && "
                "cp partner1.dmg partner2.dmg && "
                "cp partner1.dmg emefree.dmg"
            ])
        elif 'win32' in task['attributes']['build_platform']:
            task["run"]["command"] = " ".join([
                "cd", "/builds/worker/checkouts/gecko", "&&",
                "curl -L https://queue.taskcluster.net/v1/task/aSYhH66QRDyYwhyjmt2wGQ/runs/0/artifacts/public/build/setup.exe > partner1.exe && "
                "cp partner1.exe partner2.exe && "
                "cp partner1.exe emefree.exe &&"
                "curl -L https://queue.taskcluster.net/v1/task/aSYhH66QRDyYwhyjmt2wGQ/runs/0/artifacts/public/build/target.zip > partner1.zip && "
                "cp partner1.zip partner2.zip && "
                "cp partner1.zip emefree.zip"
            ])
        elif 'win64' in task['attributes']['build_platform']:
            task["run"]["command"] = " ".join([
                "cd", "/builds/worker/checkouts/gecko", "&&",
                "curl -L https://queue.taskcluster.net/v1/task/QKLcYMFQTCiMWz1rlRlSoA/runs/0/artifacts/public/build/setup.exe > partner1.exe && "
                "cp partner1.exe partner2.exe && "
                "cp partner1.exe emefree.exe &&"
                "curl -L https://queue.taskcluster.net/v1/task/QKLcYMFQTCiMWz1rlRlSoA/runs/0/artifacts/public/build/target.zip > partner1.zip && "
                "cp partner1.zip partner2.zip && "
                "cp partner1.zip emefree.zip"
            ])
        elif 'linux-' in task['attributes']['build_platform']:
            task["run"]["command"] = " ".join([
                "cd", "/builds/worker/checkouts/gecko", "&&",
                "curl -L https://queue.taskcluster.net/v1/task/Q8txUznsRke5OQ-CVZuB1Q/runs/0/artifacts/public/build/target.tar.bz2 > target.tar.bz2 && "
                "cp partner1.tar.bz2 partner2.tar.bz2 && "
                "cp partner1.tar.bz2 emefree.tar.bz2"
            ])
        elif 'linux64-' in task['attributes']['build_platform']:
            task["run"]["command"] = " ".join([
                "cd", "/builds/worker/checkouts/gecko", "&&",
                "curl -L https://queue.taskcluster.net/v1/task/FkBd2xC_SSeBG4_ihZG5Zw/runs/0/artifacts/public/build/target.tar.bz2 > target.tar.bz2 && "
                "cp partner1.tar.bz2 partner2.tar.bz2 && "
                "cp partner1.tar.bz2 emefree.tar.bz2"
            ])

        yield task
