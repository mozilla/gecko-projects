# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Support for running toolchain-building jobs via dedicated scripts
"""

from __future__ import absolute_import, print_function, unicode_literals

import hashlib

from taskgraph.util.schema import Schema
from voluptuous import Optional, Required, Any

from taskgraph.transforms.job import run_job_using
from taskgraph.transforms.job.common import (
    docker_worker_add_tc_vcs_cache,
    docker_worker_add_gecko_vcs_env_vars,
    docker_worker_add_public_artifacts,
    docker_worker_add_tooltool,
    support_vcs_checkout,
)
from taskgraph.util.hash import hash_paths
from taskgraph import GECKO


TOOLCHAIN_INDEX = 'gecko.cache.level-{level}.toolchains.v1.{name}.{digest}'

toolchain_run_schema = Schema({
    Required('using'): 'toolchain-script',

    # the script (in taskcluster/scripts/misc) to run
    Required('script'): basestring,

    # If not false, tooltool downloads will be enabled via relengAPIProxy
    # for either just public files, or all files.  Not supported on Windows
    Required('tooltool-downloads', default=False): Any(
        False,
        'public',
        'internal',
    ),

    # Paths/patterns pointing to files that influence the outcome of a
    # toolchain build.
    Optional('resources'): [basestring],

    # Path to the artifact produced by the toolchain job
    Required('toolchain-artifact'): basestring,

    # An alias that can be used instead of the real toolchain job name in
    # the toolchains list for build jobs.
    Optional('toolchain-alias'): basestring,
})


def add_optimizations(config, run, taskdesc):
    files = list(run.get('resources', []))
    # This file
    files.append('taskcluster/taskgraph/transforms/job/toolchain.py')
    # The script
    files.append('taskcluster/scripts/misc/{}'.format(run['script']))
    # Tooltool manifest if any is defined:
    tooltool_manifest = taskdesc['worker']['env'].get('TOOLTOOL_MANIFEST')
    if tooltool_manifest:
        files.append(tooltool_manifest)

    digest = hash_paths(GECKO, files)

    # If the task has dependencies, we need those dependencies to influence
    # the index path. So take the digest from the files above, add the list
    # of its dependencies, and hash the aggregate.
    # If the task has no dependencies, just use the digest from above.
    deps = taskdesc['dependencies']
    if deps:
        data = [digest] + sorted(deps.values())
        digest = hashlib.sha256('\n'.join(data)).hexdigest()

    label = taskdesc['label']
    subs = {
        'name': label.replace('%s-' % config.kind, ''),
        'digest': digest,
    }

    optimizations = taskdesc.setdefault('optimizations', [])

    # We'll try to find a cached version of the toolchain at levels above
    # and including the current level, starting at the highest level.
    for level in reversed(range(int(config.params['level']), 4)):
        subs['level'] = level
        optimizations.append(['index-search', TOOLCHAIN_INDEX.format(**subs)])

    # ... and cache at the lowest level.
    taskdesc.setdefault('routes', []).append(
        'index.{}'.format(TOOLCHAIN_INDEX.format(**subs)))


@run_job_using("docker-worker", "toolchain-script", schema=toolchain_run_schema)
def docker_worker_toolchain(config, job, taskdesc):
    run = job['run']
    taskdesc['run-on-projects'] = ['trunk', 'try']

    worker = taskdesc['worker']
    worker['artifacts'] = []
    worker['chain-of-trust'] = True

    docker_worker_add_public_artifacts(config, job, taskdesc)
    docker_worker_add_tc_vcs_cache(config, job, taskdesc)
    docker_worker_add_gecko_vcs_env_vars(config, job, taskdesc)
    support_vcs_checkout(config, job, taskdesc)

    env = worker['env']
    env.update({
        'MOZ_BUILD_DATE': config.params['moz_build_date'],
        'MOZ_SCM_LEVEL': config.params['level'],
        'TOOLS_DISABLE': 'true',
        'MOZ_AUTOMATION': '1',
    })

    if run['tooltool-downloads']:
        internal = run['tooltool-downloads'] == 'internal'
        docker_worker_add_tooltool(config, job, taskdesc, internal=internal)

    worker['command'] = [
        '/builds/worker/bin/run-task',
        '--vcs-checkout=/builds/worker/workspace/build/src',
        '--',
        'bash',
        '-c',
        'cd /builds/worker && '
        './workspace/build/src/taskcluster/scripts/misc/{}'.format(
            run['script'])
    ]

    attributes = taskdesc.setdefault('attributes', {})
    attributes['toolchain-artifact'] = run['toolchain-artifact']
    if 'toolchain-alias' in run:
        attributes['toolchain-alias'] = run['toolchain-alias']

    add_optimizations(config, run, taskdesc)


@run_job_using("generic-worker", "toolchain-script", schema=toolchain_run_schema)
def windows_toolchain(config, job, taskdesc):
    run = job['run']
    taskdesc['run-on-projects'] = ['trunk', 'try']

    worker = taskdesc['worker']

    worker['artifacts'] = [{
        'path': r'public\build',
        'type': 'directory',
    }]
    worker['chain-of-trust'] = True

    docker_worker_add_gecko_vcs_env_vars(config, job, taskdesc)

    env = worker['env']
    env.update({
        'MOZ_BUILD_DATE': config.params['moz_build_date'],
        'MOZ_SCM_LEVEL': config.params['level'],
        'MOZ_AUTOMATION': '1',
    })

    hg = r'c:\Program Files\Mercurial\hg.exe'
    hg_command = ['"{}"'.format(hg)]
    hg_command.append('robustcheckout')
    hg_command.extend(['--sharebase', 'y:\\hg-shared'])
    hg_command.append('--purge')
    hg_command.extend(['--upstream', 'https://hg.mozilla.org/mozilla-unified'])
    hg_command.extend(['--revision', '%GECKO_HEAD_REV%'])
    hg_command.append('%GECKO_HEAD_REPOSITORY%')
    hg_command.append('.\\build\\src')

    bash = r'c:\mozilla-build\msys\bin\bash'
    worker['command'] = [
        ' '.join(hg_command),
        # do something intelligent.
        r'{} -c ./build/src/taskcluster/scripts/misc/{}'.format(bash, run['script'])
    ]

    attributes = taskdesc.setdefault('attributes', {})
    attributes['toolchain-artifact'] = run['toolchain-artifact']
    if 'toolchain-alias' in run:
        attributes['toolchain-alias'] = run['toolchain-alias']

    add_optimizations(config, run, taskdesc)
