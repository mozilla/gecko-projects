# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.transforms.beetmover import craft_release_properties
from taskgraph.util.attributes import copy_attributes_from_dependent_job
from taskgraph.util.partials import (get_balrog_platform_name,
                                     get_partials_artifacts,
                                     get_partials_artifact_map)
from taskgraph.util.schema import validate_schema, Schema
from taskgraph.util.scriptworker import (get_beetmover_bucket_scope,
                                         get_beetmover_action_scope,
                                         get_phase)
from taskgraph.util.treeherder import join_symbol
from taskgraph.transforms.task import task_description_schema
from voluptuous import Any, Required, Optional

import logging
import re

logger = logging.getLogger(__name__)


# Voluptuous uses marker objects as dictionary *keys*, but they are not
# comparable, so we cast all of the keys back to regular strings
task_description_schema = {str(k): v for k, v in task_description_schema.schema.iteritems()}

transforms = TransformSequence()

# shortcut for a string where task references are allowed
taskref_or_string = Any(
    basestring,
    {Required('task-reference'): basestring})

beetmover_description_schema = Schema({
    # the dependent task (object) for this beetmover job, used to inform beetmover.
    Required('dependent-task'): object,

    # depname is used in taskref's to identify the taskID of the unsigned things
    Required('depname', default='build'): basestring,

    # unique label to describe this beetmover task, defaults to {dep.label}-beetmover
    Optional('label'): basestring,

    # treeherder is allowed here to override any defaults we use for beetmover.  See
    # taskcluster/taskgraph/transforms/task.py for the schema details, and the
    # below transforms for defaults of various values.
    Optional('treeherder'): task_description_schema['treeherder'],

    Optional('extra'): object,
    Optional('shipping-phase'): task_description_schema['shipping-phase'],
    Optional('shipping-product'): task_description_schema['shipping-product'],
})


@transforms.add
def validate(config, jobs):
    for job in jobs:
        label = job.get('dependent-task', object).__dict__.get('label', '?no-label?')
        validate_schema(
            beetmover_description_schema, job,
            "In beetmover ({!r} kind) task for {!r}:".format(config.kind, label))
        yield job


@transforms.add
def make_task_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        repack_id = dep_job.task.get('extra', {}).get('repack_id')
        if not repack_id:
            raise Exception("Cannot find repack id!")

        attributes = dep_job.attributes
        build_platform = attributes.get("build_platform")
        if not build_platform:
            raise Exception("Cannot find build platform!")

        treeherder = job.get('treeherder', {})
        treeherder.setdefault('symbol', join_symbol('BMR-Pr', repack_id))
        dep_th_platform = dep_job.task.get('extra', {}).get(
            'treeherder', {}).get('machine', {}).get('platform', '')
        treeherder.setdefault('platform',
                              "{}/opt".format(dep_th_platform))
        treeherder.setdefault('tier', 1)
        treeherder.setdefault('kind', 'build')
        label = dep_job.label.replace("repackage-signing-", "beetmover-")
        description = (
            "Beetmover submission for repack_id '{repack_id}' for build '"
            "{build_platform}/{build_type}'".format(
                repack_id=repack_id,
                build_platform=build_platform,
                build_type=attributes.get('build_type')
            )
        )

        dependent_kind = str(dep_job.kind)
        dependencies = {dependent_kind: dep_job.label}

        if "eme" in repack_id:
            dependencies["build"] = "release-eme-free-repack-{}".format(build_platform)
            if "macosx" in build_platform:
                dependencies["signing"] = "release-eme-free-repack-signing-{}".format(
                    build_platform
                )
            if "win" in build_platform:
                dependencies["repackage"] = "release-eme-free-repack-repackage-{}-{}".format(
                    build_platform, repack_id
                )
            if "macosx" in build_platform or "win" in build_platform:
                dependencies["repackage-signing"] = "release-eme-free-repack-"\
                                                    "repackage-signing-{}-{}".format(
                    build_platform, repack_id
                )
            build_name = "release-eme-free-repack"
            signing_name = "release-eme-free-repack-signing"
            repackage_name = "release-eme-free-repack-repackage"
        else:
            dependencies["build"] = "release-partner-repack-repack-{}".format(build_platform)
            dependencies["signing"] = "release-partner-repack-repack-"\
                                      "signing-{}".format(build_platform)
            dependencies["repackage"] = "release-partner-repack-repack-repackage-{}-{}".format(
                build_platform, repack_id
            )
            dependencies["repackage-signing"] = "release-partner-repack-repack-"\
                                                "repackage-signing-{}-{}".format(
                build_platform, repack_id
            )

        attributes = copy_attributes_from_dependent_job(dep_job)

        bucket_scope = get_beetmover_bucket_scope(config)
        action_scope = get_beetmover_action_scope(config)
        phase = get_phase(config)

        task = {
            'label': label,
            'description': description,
            'worker-type': 'scriptworker-prov-v1/beetmoverworker-v1',
            'scopes': [bucket_scope, action_scope],
            'dependencies': dependencies,
            'attributes': attributes,
            'run-on-projects': dep_job.attributes.get('run_on_projects'),
            'treeherder': treeherder,
            'shipping-phase': job.get('shipping-phase', phase),
            'shipping-product': job.get('shipping-product'),
            'extra': {
                'repack_id': repack_id,
            },
        }

        yield task


def generate_upstream_artifacts(build_task_ref, build_signing_task_ref,
                                repackage_task_ref, repackage_signing_task_ref,
                                platform, repack_id):

    upstream_artifacts = []

    if "linux" in platform:
        upstream_artifacts.append({
            "taskId": {"task-reference": build_task_ref},
            "taskType": "build",
            "paths": ["public/build/{}/target.tar.bz2".format(repack_id)],
            "locale": repack_id,
        })
    elif "macosx" in platform:
        upstream_artifacts.append({
            "taskId": {"task-reference": repackage_task_ref},
            "taskType": "repackage",
            "paths": ["public/build/{}/target.dmg".format(repack_id)],
            "locale": repack_id,
        })
    elif "win" in platform:
        upstream_artifacts.append({
            "taskId": {"task-reference": repackage_signing_task_ref},
            "taskType": "repackage",
            "paths": ["public/build/{}/target.installer.exe".format(repack_id)],
            "locale": repack_id,
        })

    if not upstream_artifacts:
        raise Exception("Couldn't find any upstream artifacts.")

    return upstream_artifacts


@transforms.add
def make_task_worker(config, jobs):
    for job in jobs:
        platform = job["attributes"]["build_platform"]
        repack_id = job["extra"]["repack_id"]
        build_task = None
        build_signing_task = None
        repackage_task = None
        repackage_signing_task = None

        for dependency in job["dependencies"].keys():
            if 'repackage-signing' in dependency:
                repackage_signing_task = dependency
            elif 'repackage' in dependency:
                repackage_task = dependency
            elif 'signing' in dependency:
                # catches build-signing and nightly-l10n-signing
                build_signing_task = dependency
            else:
                build_task = "build"

        build_task_ref = "<" + str(build_task) + ">"
        build_signing_task_ref = "<" + str(build_signing_task) + ">"
        repackage_task_ref = "<" + str(repackage_task) + ">"
        repackage_signing_task_ref = "<" + str(repackage_signing_task) + ">"

        worker = {
            'implementation': 'beetmover',
            'release-properties': craft_release_properties(config, job),
            'upstream-artifacts': generate_upstream_artifacts(
                build_task_ref, build_signing_task_ref, repackage_task_ref,
                repackage_signing_task_ref, platform, repack_id
            ),
        }
        job["worker"] = worker

        yield job
