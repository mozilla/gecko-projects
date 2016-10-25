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
def add_signing_artifacts(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']
        dep_platform = dep_job.attributes.get('build_platform')

        job['unsigned-artifacts'] = []
        product = 'android' if 'android' in dep_platform else 'desktop'
        l10n_chunk = dep_job.attributes.get('l10n_chunk')
        for locale in get_locale_list(product, l10n_chunk):
            filename = make_pretty_name(product, dep_platform, locale)
            job['unsigned-artifacts'].append({
                'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                      filename)
                })
            if 'tar.bz2' == filename[-7:]:
                # Add the checksums file to be signed for linux
                checksums_file = filename[:-7] + "checksums"
                job['unsigned-artifacts'].append({
                    'task-reference': ARTIFACT_URL.format('unsigned-repack',
                                                          checksums_file)
                    })
        yield job


@transforms.add
def make_signing_description(config, jobs):
    for job in jobs:
        dep_job = job['dependent-task']

        job['label'] = dep_job.label.replace("nightly-l10n-", "signing-l10n-")

        job['depname'] = 'unsigned-repack'
        job['signing-format'] = "gpg" if "linux" in dep_job.label else "jar"

        job['treeherder'] = {
            # Format symbol appropriate for l10n chunking
            'symbol': 'tc-L10n(Ns{})'.format(
                dep_job.attributes.get('l10n_chunk')),
        }
        yield job
