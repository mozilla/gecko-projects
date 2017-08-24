# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

import logging
logger = logging.getLogger(__name__)


PLATFORM_RENAMES = {
    'windows2012-32': 'win32',
    'windows2012-64': 'win64',
    'osx-cross': 'macosx64',
}

BALROG_PLATFORM_MAP = {
    "linux": [
        "Linux_x86-gcc3"
    ],
    "linux64": [
        "Linux_x86_64-gcc3"
    ],
    "macosx64": [
        "Darwin_x86_64-gcc3-u-i386-x86_64",
        "Darwin_x86-gcc3-u-i386-x86_64",
        "Darwin_x86-gcc3",
        "Darwin_x86_64-gcc3"
    ],
    "win32": [
        "WINNT_x86-msvc",
        "WINNT_x86-msvc-x86",
        "WINNT_x86-msvc-x64"
    ],
    "win64": [
        "WINNT_x86_64-msvc",
        "WINNT_x86_64-msvc-x64"
    ]
}


def get_friendly_platform_name(platform):
    '''Convert build platform names into friendly platform names'''
    if '-nightly' in platform:
        platform = platform.replace('-nightly', '')
    if '-devedition' in platform:
        platform = platform.replace('-devedition', '')
    return PLATFORM_RENAMES.get(platform, platform)


def _open_release_history(release_history):
    # TODO the join here is fragile. Need to reliably determine artifact path
    with open(os.path.join('artifacts', release_history), 'r') as f:
        return json.load(f)
    return dict()


def _sanitize_platform(platform):
    platform = get_friendly_platform_name(platform)
    if platform not in BALROG_PLATFORM_MAP:
        return platform
    return BALROG_PLATFORM_MAP[platform][0]


def get_builds(release_history, platform, locale):
    '''Examine cached balrog release history and return the list of
    builds we need to generate diffs from'''
    history = _open_release_history(release_history)
    platform = _sanitize_platform(platform)
    return history.get(platform, {}).get(locale, {})


def get_partials_artifacts(release_history, platform, locale):
    history = _open_release_history(release_history)
    platform = _sanitize_platform(platform)
    return history.get(platform, {}).get(locale, {}).keys()


def get_partials_artifact_map(release_history, platform, locale):
    history = _open_release_history(release_history)
    platform = _sanitize_platform(platform)
    return {k: history[platform][locale][k]['buildid'] for k in history.get(platform, {}).get(locale, {})}


def get_partials_artifacts_friendly(release_history, platform, locale, current_builid):
    history = _open_release_history(release_history)
    platform = _sanitize_platform(platform)
    return history.get(platform, {}).get(locale, {}).keys()
