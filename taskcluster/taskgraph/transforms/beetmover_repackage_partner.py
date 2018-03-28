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
from taskgraph.util.schema import validate_schema, Schema
from taskgraph.util.scriptworker import (get_beetmover_bucket_scope,
                                         get_beetmover_action_scope,
                                         get_phase)
from taskgraph.util.taskcluster import get_artifact_prefix
from taskgraph.transforms.task import task_description_schema
from voluptuous import Any, Required, Optional

import logging

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
def skip_for_indirect_dependencies(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        build_platform = dep_job.attributes.get("build_platform")
        if not build_platform:
            raise Exception("Cannot find build platform!")

        # Partner and EME free beetmover tasks have multiple upstreams defined
        # because some platforms don't run some parts of the sign -> repack ->
        # repack sign chain. We only want to run beetmover for the last part of
        # that chain that runs for any given platform.
        # For Linux, it is the eme-free/partner repack build tasks.
        # For Mac, it is repackage.
        # For Windows, it is repackage-signing.
        if "win" in build_platform:
            if "repackage" not in dep_job.label:
                continue
            elif "signing" not in dep_job.label:
                continue
        if "macosx" in build_platform:
            if "repackage" not in dep_job.label:
                continue

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

        treeherder = None
        if 'partner' not in config.kind:
            treeherder = job.get('treeherder', {})
            dep_th_platform = dep_job.task.get('extra', {}).get(
                'treeherder', {}).get('machine', {}).get('platform', '')
            treeherder.setdefault('platform',
                                  "{}/opt".format(dep_th_platform))

        label = dep_job.label.replace("repackage-signing-", "beetmover-")
        label = label.replace("repackage-", "beetmover-")
        label = label.replace("chunking-dummy-", "beetmover-")
        description = (
            "Beetmover submission for repack_id '{repack_id}' for build '"
            "{build_platform}/{build_type}'".format(
                repack_id=repack_id,
                build_platform=build_platform,
                build_type=attributes.get('build_type')
            )
        )

        dependencies = {}

        base_label = "release-partner-repack"
        if "eme" in repack_id:
            base_label = "release-eme-free-repack"
        dependencies["build"] = "{}-{}".format(base_label, build_platform)
        if "macosx" in build_platform or "win" in build_platform:
            dependencies["repackage"] = "{}-repackage-{}-{}".format(
                base_label, build_platform, repack_id
            )
        if "win" in build_platform:
            dependencies["repackage-signing"] = "{}-repackage-signing-{}-{}".format(
                base_label, build_platform, repack_id
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
            'shipping-phase': job.get('shipping-phase', phase),
            'shipping-product': job.get('shipping-product'),
            'extra': {
                'repack_id': repack_id,
            },
        }
        if treeherder:
            task['treeherder'] = treeherder

        yield task


def generate_upstream_artifacts(job, build_task_ref, repackage_task_ref,
                                repackage_signing_task_ref, platform, repack_id):

    upstream_artifacts = []
    artifact_prefix = get_artifact_prefix(job)

    if "linux" in platform:
        upstream_artifacts.append({
            "taskId": {"task-reference": build_task_ref},
            "taskType": "build",
            "paths": ["{}/{}/target.tar.bz2".format(artifact_prefix, repack_id)],
            "locale": repack_id,
        })
    elif "macosx" in platform:
        upstream_artifacts.append({
            "taskId": {"task-reference": repackage_task_ref},
            "taskType": "repackage",
            "paths": ["{}/{}/target.dmg".format(artifact_prefix, repack_id)],
            "locale": repack_id,
        })
    elif "win" in platform:
        upstream_artifacts.append({
            "taskId": {"task-reference": repackage_signing_task_ref},
            "taskType": "repackage",
            "paths": ["{}/{}/target.installer.exe".format(artifact_prefix, repack_id)],
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
        repackage_task = None
        repackage_signing_task = None

        for dependency in job["dependencies"].keys():
            if 'repackage-signing' in dependency:
                repackage_signing_task = dependency
            elif 'repackage' in dependency:
                repackage_task = dependency
            else:
                build_task = "build"

        build_task_ref = "<" + str(build_task) + ">"
        repackage_task_ref = "<" + str(repackage_task) + ">"
        repackage_signing_task_ref = "<" + str(repackage_signing_task) + ">"

        worker = {
            'implementation': 'beetmover',
            'release-properties': craft_release_properties(config, job),
            'upstream-artifacts': generate_upstream_artifacts(
                job, build_task_ref, repackage_task_ref,
                repackage_signing_task_ref, platform, repack_id
            ),
        }
        job["worker"] = worker

        yield job
