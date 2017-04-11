# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

from voluptuous import Required
from taskgraph.util.taskcluster import get_artifact_url
from taskgraph.transforms.job import run_job_using
from taskgraph.util.schema import Schema
from taskgraph.transforms.tests import (
    test_description_schema,
    get_firefox_version,
    normpath
)
from taskgraph.transforms.job.common import (
    support_vcs_checkout,
)
import os
import re

ARTIFACTS = [
    # (artifact name prefix, in-image path)
    ("public/logs/", "build/upload/logs/"),
    ("public/test", "artifacts/"),
    ("public/test_info/", "build/blobber_upload_dir/"),
]

BUILDER_NAME_PREFIX = {
    'linux64-pgo': 'Ubuntu VM 12.04 x64',
    'linux64': 'Ubuntu VM 12.04 x64',
    'linux64-nightly': 'Ubuntu VM 12.04 x64',
    'linux64-asan': 'Ubuntu ASAN VM 12.04 x64',
    'linux64-ccov': 'Ubuntu Code Coverage VM 12.04 x64',
    'linux64-jsdcov': 'Ubuntu Code Coverage VM 12.04 x64',
    'linux64-stylo': 'Ubuntu VM 12.04 x64',
    'macosx64': 'Rev7 MacOSX Yosemite 10.10.5',
    'android-4.3-arm7-api-15': 'Android 4.3 armv7 API 15+',
    'android-4.2-x86': 'Android 4.2 x86 Emulator',
    'android-4.3-arm7-api-15-gradle': 'Android 4.3 armv7 API 15+',
}

test_description_schema = {str(k): v for k, v in test_description_schema.schema.iteritems()}

mozharness_test_run_schema = Schema({
    Required('using'): 'mozharness-test',
    Required('test'): test_description_schema,
})


@run_job_using('docker-engine', 'mozharness-test', schema=mozharness_test_run_schema)
@run_job_using('docker-worker', 'mozharness-test', schema=mozharness_test_run_schema)
def mozharness_test_on_docker(config, job, taskdesc):
    test = taskdesc['run']['test']
    mozharness = test['mozharness']
    worker = taskdesc['worker']

    artifacts = [
        # (artifact name prefix, in-image path)
        ("public/logs/", "/home/worker/workspace/build/upload/logs/"),
        ("public/test", "/home/worker/artifacts/"),
        ("public/test_info/", "/home/worker/workspace/build/blobber_upload_dir/"),
    ]

    installer_url = get_artifact_url('<build>', mozharness['build-artifact-name'])
    test_packages_url = get_artifact_url('<build>',
                                         'public/build/target.test_packages.json')
    mozharness_url = get_artifact_url('<build>',
                                      'public/build/mozharness.zip')

    worker['artifacts'] = [{
        'name': prefix,
        'path': os.path.join('/home/worker/workspace', path),
        'type': 'directory',
    } for (prefix, path) in artifacts]

    worker['caches'] = [{
        'type': 'persistent',
        'name': 'level-{}-{}-test-workspace'.format(
            config.params['level'], config.params['project']),
        'mount-point': "/home/worker/workspace",
    }]

    env = worker['env'] = {
        'MOZHARNESS_CONFIG': ' '.join(mozharness['config']),
        'MOZHARNESS_SCRIPT': mozharness['script'],
        'MOZILLA_BUILD_URL': {'task-reference': installer_url},
        'NEED_PULSEAUDIO': 'true',
        'NEED_WINDOW_MANAGER': 'true',
        'ENABLE_E10S': str(bool(test.get('e10s'))).lower(),
    }

    if mozharness.get('mochitest-flavor'):
        env['MOCHITEST_FLAVOR'] = mozharness['mochitest-flavor']

    if mozharness['set-moz-node-path']:
        env['MOZ_NODE_PATH'] = '/usr/local/bin/node'

    if 'actions' in mozharness:
        env['MOZHARNESS_ACTIONS'] = ' '.join(mozharness['actions'])

    if config.params['project'] == 'try':
        env['TRY_COMMIT_MSG'] = config.params['message']

    # handle some of the mozharness-specific options

    if mozharness['tooltool-downloads']:
        worker['relengapi-proxy'] = True
        worker['caches'].append({
            'type': 'persistent',
            'name': 'tooltool-cache',
            'mount-point': '/home/worker/tooltool-cache',
        })
        taskdesc['scopes'].extend([
            'docker-worker:relengapi-proxy:tooltool.download.internal',
            'docker-worker:relengapi-proxy:tooltool.download.public',
        ])

    # assemble the command line
    command = [
        '/home/worker/bin/run-task',
        # The workspace cache/volume is default owned by root:root.
        '--chown', '/home/worker/workspace',
    ]

    # Support vcs checkouts regardless of whether the task runs from
    # source or not in case it is needed on an interactive loaner.
    support_vcs_checkout(config, job, taskdesc)

    # If we have a source checkout, run mozharness from it instead of
    # downloading a zip file with the same content.
    if test['checkout']:
        command.extend(['--vcs-checkout', '/home/worker/checkouts/gecko'])
        env['MOZHARNESS_PATH'] = '/home/worker/checkouts/gecko/testing/mozharness'
    else:
        env['MOZHARNESS_URL'] = {'task-reference': mozharness_url}

    command.extend([
        '--',
        '/home/worker/bin/test-linux.sh',
    ])

    if mozharness.get('no-read-buildbot-config'):
        command.append("--no-read-buildbot-config")
    command.extend([
        {"task-reference": "--installer-url=" + installer_url},
        {"task-reference": "--test-packages-url=" + test_packages_url},
    ])
    command.extend(mozharness.get('extra-options', []))

    # TODO: remove the need for run['chunked']
    if mozharness.get('chunked') or test['chunks'] > 1:
        # Implement mozharness['chunking-args'], modifying command in place
        if mozharness['chunking-args'] == 'this-chunk':
            command.append('--total-chunk={}'.format(test['chunks']))
            command.append('--this-chunk={}'.format(test['this-chunk']))
        elif mozharness['chunking-args'] == 'test-suite-suffix':
            suffix = mozharness['chunk-suffix'].replace('<CHUNK>', str(test['this-chunk']))
            for i, c in enumerate(command):
                if isinstance(c, basestring) and c.startswith('--test-suite'):
                    command[i] += suffix

    if 'download-symbols' in mozharness:
        download_symbols = mozharness['download-symbols']
        download_symbols = {True: 'true', False: 'false'}.get(download_symbols, download_symbols)
        command.append('--download-symbols=' + download_symbols)

    worker['command'] = command


@run_job_using('generic-worker', 'mozharness-test', schema=mozharness_test_run_schema)
def mozharness_test_on_generic_worker(config, job, taskdesc):
    test = taskdesc['run']['test']
    mozharness = test['mozharness']
    worker = taskdesc['worker']

    artifacts = [
        {
            'name': 'public/logs/localconfig.json',
            'path': 'logs/localconfig.json',
            'type': 'file'
        },
        {
            'name': 'public/logs/log_critical.log',
            'path': 'logs/log_critical.log',
            'type': 'file'
        },
        {
            'name': 'public/logs/log_error.log',
            'path': 'logs/log_error.log',
            'type': 'file'
        },
        {
            'name': 'public/logs/log_fatal.log',
            'path': 'logs/log_fatal.log',
            'type': 'file'
        },
        {
            'name': 'public/logs/log_info.log',
            'path': 'logs/log_info.log',
            'type': 'file'
        },
        {
            'name': 'public/logs/log_raw.log',
            'path': 'logs/log_raw.log',
            'type': 'file'
        },
        {
            'name': 'public/logs/log_warning.log',
            'path': 'logs/log_warning.log',
            'type': 'file'
        },
        {
            'name': 'public/test_info',
            'path': 'build/blobber_upload_dir',
            'type': 'directory'
        }
    ]

    build_platform = taskdesc['attributes']['build_platform']

    target = 'firefox-{}.en-US.{}'.format(get_firefox_version(), build_platform) \
        if build_platform.startswith('win') else 'target'

    installer_url = get_artifact_url('<build>', mozharness['build-artifact-name'])

    test_packages_url = get_artifact_url(
        '<build>', 'public/build/{}.test_packages.json'.format(target))

    taskdesc['scopes'].extend(
        ['generic-worker:os-group:{}'.format(group) for group in test['os-groups']])

    worker['os-groups'] = test['os-groups']

    worker['max-run-time'] = test['max-run-time']
    worker['artifacts'] = artifacts

    # this list will get cleaned up / reduced / removed in bug 1354088
    if build_platform.startswith('macosx'):
        worker['env'] = {
            'IDLEIZER_DISABLE_SHUTDOWN': 'true',
            'LANG': 'en_US.UTF-8',
            'LC_ALL': 'en_US.UTF-8',
            'MOZ_HIDE_RESULTS_TABLE': '1',
            'MOZ_NODE_PATH': '/usr/local/bin/node',
            'MOZ_NO_REMOTE': '1',
            'NO_EM_RESTART': '1',
            'NO_FAIL_ON_TEST_ERRORS': '1',
            'PATH': '/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin',
            'SHELL': '/bin/bash',
            'XPCOM_DEBUG_BREAK': 'warn',
            'XPC_FLAGS': '0x0',
            'XPC_SERVICE_NAME': '0'
        }

    if build_platform.startswith('macosx'):
        mh_command = [
            'python2.7',
            '-u',
            'mozharness/scripts/' + mozharness['script']
        ]
    elif build_platform.startswith('win'):
        mh_command = [
            'c:\\mozilla-build\\python\\python.exe',
            '-u',
            'mozharness\\scripts\\' + normpath(mozharness['script'])
        ]
    else:
        mh_command = [
            'python',
            '-u',
            'mozharness/scripts/' + mozharness['script']
        ]

    for mh_config in mozharness['config']:
        cfg_path = 'mozharness/configs/' + mh_config
        if build_platform.startswith('win'):
            cfg_path = normpath(cfg_path)
        mh_command.extend(['--cfg', cfg_path])
    mh_command.extend(mozharness.get('extra-options', []))
    if mozharness.get('no-read-buildbot-config'):
        mh_command.append('--no-read-buildbot-config')
    mh_command.extend(['--installer-url', installer_url])
    mh_command.extend(['--test-packages-url', test_packages_url])
    if mozharness.get('download-symbols'):
        if isinstance(mozharness['download-symbols'], basestring):
            mh_command.extend(['--download-symbols', mozharness['download-symbols']])
        else:
            mh_command.extend(['--download-symbols', 'true'])

    # TODO: remove the need for run['chunked']
    if mozharness.get('chunked') or test['chunks'] > 1:
        # Implement mozharness['chunking-args'], modifying command in place
        if mozharness['chunking-args'] == 'this-chunk':
            mh_command.append('--total-chunk={}'.format(test['chunks']))
            mh_command.append('--this-chunk={}'.format(test['this-chunk']))
        elif mozharness['chunking-args'] == 'test-suite-suffix':
            suffix = mozharness['chunk-suffix'].replace('<CHUNK>', str(test['this-chunk']))
            for i, c in enumerate(mh_command):
                if isinstance(c, basestring) and c.startswith('--test-suite'):
                    mh_command[i] += suffix

    worker['mounts'] = [{
        'directory': '.',
        'content': {
            'artifact': 'public/build/mozharness.zip',
            'task-id': {
                'task-reference': '<build>'
            }
        },
        'format': 'zip'
    }]

    if build_platform.startswith('win'):
        worker['command'] = [
            {'task-reference': ' '.join(mh_command)}
        ]
    else:
        mh_command_task_ref = []
        for token in mh_command:
            mh_command_task_ref.append({'task-reference': token})
        worker['command'] = [
            mh_command_task_ref
        ]


@run_job_using('native-engine', 'mozharness-test', schema=mozharness_test_run_schema)
def mozharness_test_on_native_engine(config, job, taskdesc):
    test = taskdesc['run']['test']
    mozharness = test['mozharness']
    worker = taskdesc['worker']

    installer_url = get_artifact_url('<build>', mozharness['build-artifact-name'])
    test_packages_url = get_artifact_url('<build>',
                                         'public/build/target.test_packages.json')
    mozharness_url = get_artifact_url('<build>',
                                      'public/build/mozharness.zip')

    worker['artifacts'] = [{
        'name': prefix.rstrip('/'),
        'path': path.rstrip('/'),
        'type': 'directory',
    } for (prefix, path) in ARTIFACTS]

    worker['reboot'] = test['reboot']
    worker['env'] = {
        'GECKO_HEAD_REPOSITORY': config.params['head_repository'],
        'GECKO_HEAD_REV': config.params['head_rev'],
        'MOZHARNESS_CONFIG': ' '.join(mozharness['config']),
        'MOZHARNESS_SCRIPT': mozharness['script'],
        'MOZHARNESS_URL': {'task-reference': mozharness_url},
        'MOZILLA_BUILD_URL': {'task-reference': installer_url},
        "MOZ_NO_REMOTE": '1',
        "NO_EM_RESTART": '1',
        "XPCOM_DEBUG_BREAK": 'warn',
        "NO_FAIL_ON_TEST_ERRORS": '1',
        "MOZ_HIDE_RESULTS_TABLE": '1',
        "MOZ_NODE_PATH": "/usr/local/bin/node",
    }

    worker['context'] = '{}/raw-file/{}/taskcluster/scripts/tester/test-macosx.sh'.format(
        config.params['head_repository'], config.params['head_rev']
    )

    command = worker['command'] = ["./test-macosx.sh"]
    if mozharness.get('no-read-buildbot-config'):
        command.append("--no-read-buildbot-config")
    command.extend([
        {"task-reference": "--installer-url=" + installer_url},
        {"task-reference": "--test-packages-url=" + test_packages_url},
    ])
    if mozharness.get('include-blob-upload-branch'):
        command.append('--blob-upload-branch=' + config.params['project'])
    command.extend(mozharness.get('extra-options', []))

    # TODO: remove the need for run['chunked']
    if mozharness.get('chunked') or test['chunks'] > 1:
        # Implement mozharness['chunking-args'], modifying command in place
        if mozharness['chunking-args'] == 'this-chunk':
            command.append('--total-chunk={}'.format(test['chunks']))
            command.append('--this-chunk={}'.format(test['this-chunk']))
        elif mozharness['chunking-args'] == 'test-suite-suffix':
            suffix = mozharness['chunk-suffix'].replace('<CHUNK>', str(test['this-chunk']))
            for i, c in enumerate(command):
                if isinstance(c, basestring) and c.startswith('--test-suite'):
                    command[i] += suffix

    if 'download-symbols' in mozharness:
        download_symbols = mozharness['download-symbols']
        download_symbols = {True: 'true', False: 'false'}.get(download_symbols, download_symbols)
        command.append('--download-symbols=' + download_symbols)


@run_job_using('buildbot-bridge', 'mozharness-test', schema=mozharness_test_run_schema)
def mozharness_test_buildbot_bridge(config, job, taskdesc):
    test = taskdesc['run']['test']
    mozharness = test['mozharness']
    worker = taskdesc['worker']

    branch = config.params['project']
    platform, build_type = test['build-platform'].split('/')
    test_name = test.get('talos-try-name', test['test-name'])
    mozharness = test['mozharness']

    # mochitest e10s follows the pattern mochitest-e10s-<suffix>
    # in buildbot, except for these special cases
    buildbot_specials = [
        'mochitest-webgl',
        'mochitest-clipboard',
        'mochitest-media',
        'mochitest-gpu',
        'mochitest-e10s',
    ]
    test_name = test.get(
                    'talos-try-name',
                    test.get(
                        'unittest-try-name',
                        test['test-name']
                    )
                )
    if test['e10s'] and 'e10s' not in test_name:
        test_name += '-e10s'

    if test_name.startswith('mochitest') \
            and test_name.endswith('e10s') \
            and not any(map(
                lambda name: test_name.startswith(name),
                buildbot_specials
            )):
        split_mochitest = test_name.split('-')
        test_name = '-'.join([
            split_mochitest[0],
            split_mochitest[-1],
            '-'.join(split_mochitest[1:-1])
        ])

    # in buildbot, mochitest-webgl is called mochitest-gl
    test_name = test_name.replace('webgl', 'gl')

    if mozharness.get('chunked', False):
        this_chunk = test.get('this-chunk')
        test_name = '{}-{}'.format(test_name, this_chunk)

    if test.get('suite', '') == 'talos':
        # on linux64-<variant>/<build>, we add the variant to the buildername
        m = re.match(r'\w+-([^/]+)/.*', test['test-platform'])
        variant = ''
        if m and m.group(1):
            variant = m.group(1) + ' '
        # On beta and release, we run nightly builds on-push; the talos
        # builders need to run against non-nightly buildernames
        if variant == 'nightly ':
            variant = ''
        # this variant name has branch after the variant type in BBB bug 1338871
        if variant == 'stylo ':
            buildername = '{} {}{} talos {}'.format(
                BUILDER_NAME_PREFIX[platform],
                variant,
                branch,
                test_name
            )
        else:
            buildername = '{} {} {}talos {}'.format(
                BUILDER_NAME_PREFIX[platform],
                branch,
                variant,
                test_name
            )
        if buildername.startswith('Ubuntu'):
            buildername = buildername.replace('VM', 'HW')
    else:
        buildername = '{} {} {} test {}'.format(
            BUILDER_NAME_PREFIX[platform],
            branch,
            build_type,
            test_name
        )

    worker.update({
        'buildername': buildername,
        'sourcestamp': {
            'branch': branch,
            'repository': config.params['head_repository'],
            'revision': config.params['head_rev'],
        },
        'properties': {
            'product': test.get('product', 'firefox'),
            'who': config.params['owner'],
            'installer_path': mozharness['build-artifact-name'],
        }
    })
