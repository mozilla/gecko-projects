# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the repackage task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import validate_schema, Schema
# from taskgraph.util.scriptworker import get_signing_cert_scope
from taskgraph.transforms.task import task_description_schema
from voluptuous import Any, Required, Optional

transforms = TransformSequence()


# Voluptuous uses marker objects as dictionary *keys*, but they are not
# comparable, so we cast all of the keys back to regular strings
task_description_schema = {str(k): v for k, v in task_description_schema.schema.iteritems()}

# shortcut for a string where task references are allowed
taskref_or_string = Any(
    basestring,
    {Required('task-reference'): basestring})

packaging_description_schema = Schema({
    # the dependant task (object) for this  job, used to inform signing.
    Required('dependent-task'): object,

    # depname is used in taskref's to identify the taskID of the unsigned things
    Required('depname', default='build'): basestring,

    # unique label to describe this signing task, defaults to {dep.label}-signing
    Optional('label'): basestring,

    # treeherder is allowed here to override any defaults we use for signing.  See
    # taskcluster/taskgraph/transforms/task.py for the schema details, and the
    # below transforms for defaults of various values.
    Optional('treeherder'): task_description_schema['treeherder'],

    # Routes specific to this task, if defined
    Optional('routes'): [basestring],
})


# comment out adding routes until talking to mshal
# @transforms.add
# def add_repackage_routes(config, jobs):
#    """Add routes corresponding to the routes of the build task
#       this corresponds to, with .repackage inserted, for all gecko.v2 routes"""
#
#    for job in jobs:
#        dep_job = job['dependent-task']
#
#        job['routes'] = []
#        for dep_route in dep_job.task.get('routes', []):
#            if not dep_route.startswith('index.gecko.v2'):
#                continue
#            branch = dep_route.split(".")[3]
#            rest = ".".join(dep_route.split(".")[5:])
#            job['routes'].append(
#                'index.gecko.v2.{}.repackage-nightly.{}'.format(branch, rest))
#        yield job

@transforms.add
def make_repackage_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']

        label = dep_job.label.replace("signing-", "repackage-")
        job['label'] = label

        yield job


@transforms.add
def validate(config, jobs):
    for job in jobs:
        label = job.get('dependent-task', object).__dict__.get('label', '?no-label?')
        yield validate_schema(
            packaging_description_schema, job,
            "In packaging ({!r} kind) task for {!r}:".format(config.kind, label))


@transforms.add
def make_task_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']

        treeherder = job.get('treeherder', {})
        treeherder.setdefault('symbol', 'tc(Nr)')
        dep_th_platform = dep_job.task.get('extra', {}).get(
            'treeherder', {}).get('machine', {}).get('platform', '')
        treeherder.setdefault('platform', "{}/opt".format(dep_th_platform))
        treeherder.setdefault('tier', 1)
        treeherder.setdefault('kind', 'build')

        label = job.get('label', "{}-repackage".format(dep_job.label))

        attributes = {
            'nightly': dep_job.attributes.get('nightly', False),
            'build_platform': dep_job.attributes.get('build_platform'),
            'build_type': dep_job.attributes.get('build_type'),
        }

        command = ['/home/worker/bin/run-task',
                   # Various caches/volumes are default owned by root:root.
                   '--chown-recursive', '/home/worker/workspace',
                   '--chown-recursive', '/home/worker/tooltool-cache',
                   '--vcs-checkout', '/home/worker/workspace/build/src',
                   '--tools-checkout', '/home/worker/workspace/build/tools',
                   '--',
                   '/home/worker/workspace/build/src/taskcluster/scripts/builder/repackage.sh'
                   ]

        dependencies = {dep_job.attributes.get('kind'): dep_job.label}
        input_string = 'https://queue.taskcluster.net/v1/task/<build-signing>/artifacts/' + \
            'public/build/target.tar.gz'
        signed_input = {'task-reference': input_string}
        level = config.params['level']

        task = {
            'label': label,
            'description': "{} Repackage".format(
                dep_job.task["metadata"]["description"]),
            'worker-type': "aws-provisioner-v1/gecko-1-b-macosx64",
            'dependencies': dependencies,
            'attributes': attributes,
            'run-on-projects': dep_job.attributes.get('run_on_projects'),
            'treeherder': treeherder,
            'routes': job.get('routes', []),
            'scopes':
            ['docker-worker:relengapi-proxy:tooltool.download.internal',
             'secrets:get:project/taskcluster/gecko/hgfingerprint',
             'docker-worker:relengapi-proxy:tooltool.download.public',
             'project:releng:signing:format:dmg'],
            'worker': {'implementation': 'docker-worker',
                       'docker-image': {"in-tree": "desktop-build"},
                       'caches': [{
                                   'type': 'persistent',
                                   'name': 'tooltool-cache',
                                   'mount-point': '/home/worker/tooltool-cache',
                                 }, {
                                   'type': 'persistent',
                                   'name': 'level-%s-checkouts-v1' % level,
                                   'mount-point': '/home/worker/checkouts',
                                 }],
                       'artifacts': [{
                                       'type': 'file',
                                       'path': '/home/worker/workspace/build/upload/target.dmg',
                                       'name': 'public/build/target.dmg',
                                     }],
                       'env': {
                               'SIGNED_INPUT': signed_input,
                               'MOZ_BUILD_DATE': config.params['moz_build_date'],
                               'MH_BUILD_POOL': 'taskcluster',
                               'HG_STORE_PATH': '/home/worker/checkouts/hg-store',
                               'GECKO_HEAD_REV': config.params['head_rev'],
                               'MH_BRANCH': config.params['project'],
                               'MOZ_SCM_LEVEL': level,
                               'MOZHARNESS_ACTIONS': 'download_input setup repackage',
                               'NEED_XVFB': 'true',
                               'GECKO_BASE_REPOSITORY': config.params['base_repository'],
                               'TOOLTOOL_CACHE': '/home/worker/tooltool-cache',
                               'GECKO_HEAD_REPOSITORY': config.params['head_repository'],
                               'MOZHARNESS_CONFIG': 'repackage/osx_signed.py',
                               'USE_SCCACHE': '1',
                               'MOZHARNESS_SCRIPT': 'mozharness/scripts/repackage.py'
                               },
                       'command': command,
                       'chain-of-trust': True,
                       'relengapi-proxy': True,
                       'max-run-time': 3600
               }
        }
        yield task
