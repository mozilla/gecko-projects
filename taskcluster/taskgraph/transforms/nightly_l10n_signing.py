# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the signing task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import os

from mozbuild import milestone
from taskgraph.transforms.base import TransformSequence

ARTIFACT_URL = 'https://queue.taskcluster.net/v1/task/<{}>/artifacts/public/build/{}'

transforms = TransformSequence()

# XXXCallek: Do not merge this logic to m-c, its hardcoded for simplicity on date
# And should actually parse chunking info and/or the locales file instead.
LOCALES_PER_CHUNK = {
    'android': [
        ['ar', 'be', 'ca', 'cs', 'da', 'de', 'es-AR', 'es-ES'],  # chunk 1
        ['et', 'eu', 'fa', 'fi', 'fr', 'fy-NL', 'ga-IE'],  # chunk 2
        ['gd', 'gl', 'he', 'hu', 'id', 'it', 'ja'],  # chunk 3
        ['ko', 'lt', 'nb-NO', 'nl', 'nn-NO', 'pa-IN', 'pl'],  # chunk 4
        ['pt-BR', 'pt-PT', 'ro', 'ru', 'sk', 'sl', 'sq'],  # chunk 5
        ['sr', 'sv-SE', 'th', 'tr', 'uk', 'zh-CN', 'zh-TW'],  # chunk 6
    ],
    'desktop': [
        ['ar', 'ast', 'cs', 'de', 'en-GB', 'eo', 'es-AR'],  # chunk 1
        ['es-CL', 'es-ES', 'es-MX', 'fa', 'fr', 'fy-NL', 'gl'],  # chunk 2
        ['he', 'hu', 'id', 'it', 'ja', 'kk', 'ko'],  # chunk 3
        ['lt', 'lv', 'nb-NO', 'nl', 'nn-NO', 'pl'],  # chunk 4
        ['pt-BR', 'pt-PT', 'ru', 'sk', 'sl', 'sv-SE'],  # chunk 5
        ['th', 'tr', 'uk', 'vi', 'zh-CN', 'zh-TW'],  # chunk 6
    ]
}

# XXX Prettynames are bad, fix them
PRETTYNAMES = {
    'desktop': "firefox-{version}.{locale}.{platform}.tar.bz2",
    'android': "fennec-{version}.{locale}.{platform}.apk",
}
PRETTY_PLATFORM_FROM_BUILD_PLATFORM = {
    'android-api-15-nightly': 'android-arm',
    'linux64-nightly': 'linux-x86_64',
    'linux-nightly': 'linux-i686',
}
_version_cache = None  # don't get this multiple times


def get_locale_list(product, chunk):
    """ Gets the list of locales for this l10n chunk """
    # XXXCallek This should be refactored to parse the locales file instead of
    # a hardcoded list of things
    if product not in LOCALES_PER_CHUNK.keys():
        raise ValueError("Unexpected product passed")
    return LOCALES_PER_CHUNK[product][chunk - 1]


def get_version_number():
    global _version_cache  # Cache this value
    if _version_cache:
        return _version_cache
    milestone_file = os.path.join('config', 'milestone.txt')
    _version_cache = milestone.get_official_milestone(milestone_file)
    return _version_cache


def make_pretty_name(product, build_platform, locale):
    # If this fails, we need to add a new key to PRETTY_PLATFORM_FROM_BUILD_PLATFORM
    platform = PRETTY_PLATFORM_FROM_BUILD_PLATFORM[build_platform]
    return PRETTYNAMES[product].format(
        version=get_version_number(),
        locale=locale,
        platform=platform,
    )


@transforms.add
def add_signing_artifacts(config, tasks):
    for task in tasks:
        task['unsigned-artifacts'] = []
        product = 'android' if 'android' in task['build-platform'] else 'desktop'
        for locale in get_locale_list(product, task['l10n_chunk']):
            filename = make_pretty_name(product, task['build-platform'], locale)
            task['unsigned-artifacts'].append({
                'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                      filename)
                })
            if 'tar.bz2' == filename[-7:]:
                # Add the checksums file to be signed for linux
                checksums_file = filename[:-7] + "checksums"
                task['unsigned-artifacts'].append({
                    'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                          checksums_file)
                    })
        yield task


@transforms.add
def make_task_description(config, tasks):
    for task in tasks:
        task['label'] = task['build-label'].replace("nightly-l10n-", "signing-l10n-")
        task['description'] = (task['build-description'].replace("-", " ") +
                               " l10n repack signing").title()
        task['description'] = task['description'].replace("Api 15", "4.0 API15+")

        unsigned_artifacts = task['unsigned-artifacts']

        task['worker-type'] = "scriptworker-prov-v1/signing-linux-v1"
        task['worker'] = {'implementation': 'scriptworker-signing',
                          'unsigned-artifacts': unsigned_artifacts}

        signing_format = "gpg" if "linux" in task['label'] else "jar"
        signing_format_scope = "project:releng:signing:format:" + signing_format
        task['scopes'] = ["project:releng:signing:cert:nightly-signing",
                          signing_format_scope]

        task['dependencies'] = {'unsigned-repack': task['build-label']}
        attributes = task.setdefault('attributes', {})
        attributes['nightly'] = True
        attributes['build_platform'] = task['build-platform']
        attributes['build_type'] = task['build-type']
        task['run-on-projects'] = task['build-run-on-projects']
        task['treeherder'] = task['build-treeherder']
        task['treeherder'].setdefault('symbol', 'tc(Ns{})'.format(
            task.get('l10n_chunk', "")
        ))
        th_platform = task['build-platform'].replace("-nightly", "") + "/opt"
        th_platform = th_platform.replace("linux/opt", "linux32/opt")
        task['treeherder'].setdefault('platform', th_platform)
        task['treeherder'].setdefault('tier', 2)
        task['treeherder'].setdefault('kind', 'build')

        # delete stuff that's not part of a task description
        del task['build-description']
        del task['build-label']
        del task['build-type']
        del task['build-platform']
        del task['build-run-on-projects']
        del task['build-treeherder']
        del task['l10n_chunk']
        del task['unsigned-artifacts']

        yield task
