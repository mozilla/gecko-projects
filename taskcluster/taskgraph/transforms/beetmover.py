# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import (
    validate_schema,
    TransformSequence
)
from taskgraph.transforms.task import task_description_schema
from voluptuous import Schema, Any, Required, Optional


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
    # to do change to build or signed-build
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

        treeherder = job.get('treeherder', {})
        treeherder.setdefault('symbol', 'tc(BM)')
        dep_th_platform = dep_job.task.get('extra', {}).get(
            'treeherder', {}).get('machine', {}).get('platform', '')
        treeherder.setdefault('platform',
                              "{}/opt".format(dep_th_platform))
        treeherder.setdefault('tier', 2)
        treeherder.setdefault('kind', 'build')

        label = job.get('label', "beetmover-{}".format(dep_job.label))
        # if dependent task is build, taskid_to_beetmove == taskid_of_manifest
        # and update_manifest = False
        # if dependent task is signed, the taskid_to_beetmove is the signed
        # build and update_manifest = True
        dependent_kind = str(job['dependent-task'].kind)
        # both artifacts are the build artifacts if not the signing task
        taskid_of_manifest = "<" + str(dependent_kind) + ">"
        taskid_to_beetmove = taskid_of_manifest
        update_manifest = False
        dependencies = {job['dependent-task'].kind: dep_job.label}
        # taskid_of_manifest always refers to the unsigned task
        if "signing" in dependent_kind:
            if len(dep_job.dependencies) > 1:
                raise NotImplementedError(
                    "can't beetmove a signing task with multiple dependencies")
            dep_name = dep_job.dependencies.keys()[0]
            taskid_of_manifest = "<" + str(dep_name) + ">"
            update_manifest = True
            signing_dependencies = dep_job.dependencies
            dependencies.update(signing_dependencies)

        worker = {'implementation': 'beetmover',
                  'taskid_to_beetmove': {"task-reference":
                                         taskid_to_beetmove},
                  'taskid_of_manifest': {"task-reference":
                                         taskid_of_manifest},
                  'update_manifest': update_manifest}
        if job.get('locale'):
            worker['locale'] = job['locale']

        task = {
            'label': label,
            'description': "{} Beetmover".format(
                dep_job.task["metadata"]["description"]),
            # do we have to define worker type somewhere?
            'worker-type': 'scriptworker-prov-v1/beetmoverworker-v1',
            'worker': worker,
            'scopes': [],
            'dependencies': dependencies,
            'attributes': {
                'nightly': dep_job.attributes.get('nightly', False),
                'build_platform': dep_job.attributes.get('build_platform'),
                'build_type': dep_job.attributes.get('build_type'),
            },
            'run-on-projects': dep_job.attributes.get('run_on_projects'),
            'treeherder': treeherder,
        }

        yield task
