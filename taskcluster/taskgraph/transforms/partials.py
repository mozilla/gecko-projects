# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partials task into an actual task description.
"""
from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.attributes import copy_attributes_from_dependent_job
from taskgraph.util.schema import validate_schema, Schema
from taskgraph.util.partials import get_friendly_platform_name, get_builds
from taskgraph.transforms.task import task_description_schema
from voluptuous import Any, Required, Optional

import json
import logging
logger = logging.getLogger(__name__)

transforms = TransformSequence()

_TC_ARTIFACT_LOCATION = \
        'https://queue.taskcluster.net/v1/task/{task_id}/artifacts/public/build/{postfix}'


def _generate_taskcluster_prefix(task_id, postfix='', locale=None):
    if locale:
        postfix = '{}/{}'.format(locale, postfix)

    return _TC_ARTIFACT_LOCATION.format(task_id=task_id, postfix=postfix)


def _generate_task_output_files(filenames, locale=None):
    locale_output_path = '{}/'.format(locale) if locale else ''

    data = list()
    for filename in filenames:
        data.append({
            'type': 'file',
            'path': '/home/worker/workspace/build/artifacts/{}{}'.format(locale_output_path, filename),
            'name': 'public/build/{}{}'.format(locale_output_path, filename)
        })
    return data


@transforms.add
def make_task_description(config, jobs):
    # If no balrog release history, then don't generate partials
    if not config.params.get('release_history'):
        return
    for job in jobs:
        dep_job = job['dependent-task']

        treeherder = job.get('treeherder', {})
        treeherder.setdefault('symbol', 'p(N)')  # TODO

        label = job.get('label', "partials-{}".format(dep_job.label))
        dep_th_platform = dep_job.task.get('extra', {}).get(
            'treeherder', {}).get('machine', {}).get('platform', '')

        treeherder.setdefault('platform',
                              "{}/opt".format(dep_th_platform))
        treeherder.setdefault('kind', 'build')  # TODO: update
        treeherder.setdefault('tier', 1)

        dependent_kind = str(dep_job.kind)
        dependencies = {dependent_kind: dep_job.label}
        signing_dependencies = dep_job.dependencies
        # This is so we get the build task etc in our dependencies to
        # have better beetmover support.
        dependencies.update(signing_dependencies)

        attributes = copy_attributes_from_dependent_job(dep_job)
        locale = dep_job.attributes.get('locale')
        if locale:
            # locale = 'en-US'
            attributes['locale'] = locale
            treeherder['symbol'] = "p({})".format(locale)

        build_locale = locale or 'en-US'

        builds = get_builds(config.params['release_history'], dep_th_platform,
                            build_locale)

        # If the list is empty there's no available history for this platform
        # and locale combination, so we can't build any partials.
        if not builds:
            continue

        signing_task = None
        for dependency in dependencies.keys():
            if 'signing' in dependency:
                signing_task = dependency
        signing_task_ref = '<{}>'.format(signing_task)

        extra = {'funsize': { 'partials': list()}}
        update_number = 1
        artifact_path = "{}/{}".format(_generate_taskcluster_prefix(signing_task_ref, locale=locale), 'target.complete.mar')
        for build in builds:
            extra['funsize']['partials'].append({
                'locale': build_locale,
                'from_mar': builds[build]['mar_url'],
                'to_mar': {'task-reference': artifact_path},
                'platform': get_friendly_platform_name(dep_th_platform),
                'branch': config.params['project'],
                'update_number': update_number,
                'dest_mar': build,
            })
            update_number += 1

        worker = {
            'artifacts': _generate_task_output_files(builds.keys(), build_locale),
        }

        run = {
            'using': 'mozharness',
            'script': 'mozharness/scripts/funsize.py',
            'config': ['funsize.py'],
            'job-script': 'taskcluster/scripts/builder/funsize.sh',
            'actions': [],
        }
        level = config.params['level']

        task = {
            'label': label,
            'description': "{} Partials".format(
                dep_job.task["metadata"]["description"]),  # TODO reformat
            'worker-type': 'aws-provisioner-v1/gecko-%s-b-linux' % level,
            'dependencies': dependencies,
            'attributes': attributes,
            'run-on-projects': dep_job.attributes.get('run_on_projects'),
            'treeherder': treeherder,
            'extra': extra,
            'worker': worker,
            'run': run,
        }

        yield task


@transforms.add
def make_task_worker(config, jobs):
    for job in jobs:
        locale = job['attributes'].get('locale', 'en-US')

        repackage_signing_task = 'repackage-signing'  # default
        for dependency in job['dependencies'].keys():
            if 'repackage-signing' in dependency:
                repackage_signing_task = dependency

        task_ref = '<' + str(repackage_signing_task) + '>'

        worker = {
            'docker-image': {'in-tree': 'funsize-update-generator'},
            'os': 'linux',
            'max-run-time': 3600,
        }

        job["worker"].update(worker)

        yield job
