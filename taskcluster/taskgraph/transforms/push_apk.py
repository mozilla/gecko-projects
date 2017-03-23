# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the push_apk task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import re

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import validate_schema
from voluptuous import Schema, Required

REQUIRED_ARCHITECTURES = ('android-x86', 'android-api-15')

PLATFORM_REGEX = re.compile(r'signing-android-(\S+)-nightly')

# See https://github.com/mozilla-releng/pushapkscript#aurora-beta-release-vs-alpha-beta-production
GOOGLE_PLAY_TRACT_PER_PROJECT = {
    'mozilla-aurora': 'beta',
    'mozilla-beta': 'production',
    'mozilla-release': 'production',
    'date': 'alpha',
    'jamun': 'alpha',
}

BASE_PROJECT_SCOPE = 'project:releng:googleplay'
CHANNEL_PER_PROJECT = {
    'mozilla-aurora': 'aurora',
    'mozilla-beta': 'beta',
    'mozilla-release': 'release',
    'date': 'aurora',
    'jamun': 'beta',
}

transforms = TransformSequence()

push_apk_description_schema = Schema({
    # the dependent task (object) for this beetmover job, used to inform beetmover.
    Required('dependent-task'): object,
})


class MissingDependentTask(Exception):
    def __init__(self, base_message, given_dependencies):
        Exception.__init__('''
            {}.

            1 dependent job per architectures is required.
            Required architectures: {}.
            Given dependencies: {}.
        '''.format(base_message, REQUIRED_ARCHITECTURES, given_dependencies))


@transforms.add
def validate_jobs_schema(config, jobs):
    for job in jobs:
        label = job.get('dependent-task', object).__dict__.get('label', '?no-label?')
        yield validate_schema(
            push_apk_description_schema, job,
            "In PushApk ({!r} kind) task for {!r}:".format(config.kind, label)
        )


@transforms.add
def filter_out_non_android_jobs(_, jobs):
    # Linux builds are also a part of the kind-dependencies
    return (job for job in jobs if 'android' in job.get('dependent-task').label)


@transforms.add
def concatenate_dependent_jobs(_, jobs):
    return {
        'dependent_tasks': [job.get('dependent-task') for job in jobs],
        'label': 'push-apk/opt',
    }


@transforms.add
def validate_dependent_tasks(_, job):
    check_every_architecture_is_present_in_dependent_tasks(job['dependent_tasks'])
    return job


def check_every_architecture_is_present_in_dependent_tasks(dependent_tasks):
    if len(dependent_tasks) != len(REQUIRED_ARCHITECTURES):
        raise MissingDependentTask('PushApk does not have enough dependent tasks', dependent_tasks)

    dependencies_labels = [task.label for task in dependent_tasks]

    is_this_given_architecture_present = {
        architecture: any(architecture in label for label in dependencies_labels)
        for architecture in REQUIRED_ARCHITECTURES
    }
    are_all_achitectures_present = all(is_this_given_architecture_present.values())

    if not are_all_achitectures_present:
        raise MissingDependentTask(
            'PushApk has the right number of dependent tasks, but one of them is still missing. \
Please check whether one task is declared twice or one of them is not required anymore',
            dependent_tasks
        )


@transforms.add
def make_task_description(config, job):
    dependent_tasks = job['dependent_tasks']
    dependencies = generate_dependencies(dependent_tasks)

    project = config.params['project']

    task = {
        'label': job['label'],
        'description': 'PushApk',
        'attributes': {
            'build_platform': 'android-nightly',
            'nightly': True,
        },
        'worker-type': 'scriptworker-prov-v1/pushapk-v1-dev',
        'worker': {
            'implementation': 'push-apk',
            'upstream-artifacts': generate_upstream_artifacts(dependencies),
            # TODO unhardcode that line
            'dry-run': True,
            'google-play-track': generate_google_play_track(project),
        },
        'scopes': generate_scopes(project),
        'dependencies': dependencies,
        'treeherder': {
            'symbol': 'pub(gp)',
            'platform': 'Android/opt',
            'tier': 2,
            'kind': 'other',
        },
        # Force this job to only run in a release-like context
        'run-on-projects': ['release', 'date', 'jamun'],
    }

    yield task


def generate_dependencies(dependent_tasks):
    dependencies = {}
    for task in dependent_tasks:
        platform = PLATFORM_REGEX.match(task.label).group(1)
        task_kind = '{}-{}'.format(task.kind, platform)
        dependencies[task_kind] = task.label
    return dependencies


def generate_upstream_artifacts(dependencies):
    return [{
        'taskId': {'task-reference': '<{}>'.format(task_kind)},
        'taskType': 'signing',
        'paths': ['public/build/target.apk'],
    } for task_kind in dependencies.keys()]


def generate_google_play_track(project):
    return _lookup_project_in_dict(project, GOOGLE_PLAY_TRACT_PER_PROJECT)


def generate_scopes(project):
    channel = _lookup_project_in_dict(project, CHANNEL_PER_PROJECT)
    return ['{}:{}'.format(BASE_PROJECT_SCOPE, channel)]


def _lookup_project_in_dict(project, dictionary):
    try:
        return dictionary[project]
    except KeyError:
        return 'WRONG_PROJECT'
