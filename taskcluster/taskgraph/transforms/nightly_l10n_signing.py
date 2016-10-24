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
        for locale in dep_job.attributes.get('chunk_locales', []):
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
