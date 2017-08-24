# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.attributes import copy_attributes_from_dependent_job
from taskgraph.util.schema import validate_schema, Schema
from taskgraph.util.scriptworker import (get_beetmover_bucket_scope,
                                         get_beetmover_action_scope)
from taskgraph.transforms.task import task_description_schema
from taskgraph.util.partials import (get_friendly_platform_name,
                                     get_partials_artifacts,
                                     get_partials_artifact_map)
from voluptuous import Any, Required, Optional

import logging
logger = logging.getLogger(__name__)

# Until bug 1331141 is fixed, if you are adding any new artifacts here that
# need to be transfered to S3, please be aware you also need to follow-up
# with a beetmover patch in https://github.com/mozilla-releng/beetmoverscript/.
# See example in bug 1348286
UPSTREAM_ARTIFACT_SIGNED_REPACKAGE_PATHS = {
    'macosx64-nightly': ['target.complete.mar'],
    'macosx64-nightly-l10n': ['target.complete.mar'],
    'win64-nightly': ['target.complete.mar', 'target.installer.exe'],
    'win64-nightly-l10n': ['target.complete.mar', 'target.installer.exe'],
    'win32-nightly': [
        'target.complete.mar',
        'target.installer.exe',
        'target.stub-installer.exe'
    ],
    'win32-nightly-l10n': [
        'target.complete.mar',
        'target.installer.exe',
        'target.stub-installer.exe'
    ],
}

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

    # locale is passed only for l10n beetmoving
    Optional('locale'): basestring,
})


@transforms.add
def validate(config, jobs):
    for job in jobs:
        label = job.get('dependent-task', object).__dict__.get('label', '?no-label?')
        yield validate_schema(
            beetmover_description_schema, job,
            "In beetmover ({!r} kind) task for {!r}:".format(config.kind, label))


@transforms.add
def make_task_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        attributes = dep_job.attributes

        treeherder = job.get('treeherder', {})
        treeherder.setdefault('symbol', 'pBM(N)')
        dep_th_platform = dep_job.task.get('extra', {}).get(
            'treeherder', {}).get('machine', {}).get('platform', '')
        treeherder.setdefault('platform',
                              "{}/opt".format(dep_th_platform))
        treeherder.setdefault('tier', 1)
        treeherder.setdefault('kind', 'build')
        label = job['label']
        description = (
            "Beetmover submission for locale '{locale}' for partials build '"
            "{build_platform}/{build_type}'".format(
                locale=attributes.get('locale', 'en-US'),
                build_platform=attributes.get('build_platform'),
                build_type=attributes.get('build_type')
            )
        )

        dependent_kind = str(dep_job.kind)
        dependencies = {dependent_kind: dep_job.label}

        build_name = "build"
        if job.get('locale'):
            build_name = "unsigned-repack"
        build_dependencies = {"build":
                              dep_job.dependencies[build_name]
                              }
        dependencies.update(build_dependencies)

        attributes = copy_attributes_from_dependent_job(dep_job)
        locale = dep_job.attributes.get('locale')
        if locale:
            attributes['locale'] = locale
            treeherder['symbol'] = 'pBM({})'.format(attributes.get('locale'))

        bucket_scope = get_beetmover_bucket_scope(config)
        action_scope = get_beetmover_action_scope(config)

        task = {
            'label': label,
            'description': description,
            'worker-type': 'scriptworker-prov-v1/beetmoverworker-v1',
            'scopes': [bucket_scope, action_scope],
            'dependencies': dependencies,
            'attributes': attributes,
            'run-on-projects': dep_job.attributes.get('run_on_projects'),
            'treeherder': treeherder,
        }

        yield task


def generate_upstream_artifacts(release_history, platform, locale=None):
    artifact_prefix = 'public/build'
    if locale:
        artifact_prefix = 'public/build/{}'.format(locale)

    artifacts = get_partials_artifacts(release_history, platform, locale)

    upstream_artifacts = [{
        'taskId': {'task-reference': '<partials-signing>'},
        'taskType': 'signing',
        'paths': ["{}/{}".format(artifact_prefix, p)
                  for p in artifacts],
        'locale': locale or 'en-US',
    },
        {
        'taskId': {'task-reference': '<build>'},
        'taskType': 'build',
        'paths': ['public/build/balrog_props.json'],
        'locale': locale or 'en-US'
    }]

    return upstream_artifacts


@transforms.add
def make_task_worker(config, jobs):
    for job in jobs:
        locale = job["attributes"].get("locale")
        platform = job["attributes"]["build_platform"]

        platform = get_friendly_platform_name(platform)
        upstream_artifacts = generate_upstream_artifacts(
            config.params.get('release_history'), platform, locale
        )

        worker = {'implementation': 'beetmover',
                  'upstream-artifacts': upstream_artifacts}
        if locale:
            worker["locale"] = locale
        job["worker"] = worker

        extra = list()

        artifact_map = get_partials_artifact_map(
            config.params.get('release_history'), platform, locale)
        for artifact in artifact_map:
            extra.append({
                'locale': locale,
                'artifact_name': artifact,
                'buildid': artifact_map[artifact],
                'platform': platform,
            })

        job['extra'] = {'partials': extra}

        yield job
