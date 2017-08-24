# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Query balrog for partials information before task generation.
"""
from __future__ import absolute_import, print_function, unicode_literals


import requests
import redo

import logging
logger = logging.getLogger(__name__)


BALROG_API_ROOT = 'https://aus5.mozilla.org/api/v1'


PLATFORM_MAP = {
    "android": [
        "Android_arm-eabi-gcc3"
    ],
    "android-api-11": [
        "Android_arm-eabi-gcc3"
    ],
    "android-api-9": [
        "Android_arm-eabi-gcc3"
    ],
    "android-x86": [
        "Android_x86-gcc3"
    ],
    "aries-user-kk": [
        "aries-user-kk",
        "aries"
    ],
    "flame-kk": [
        "flame-kk",
        "flame"
    ],
    "flame-user-kk": [
        "flame-user-kk",
        "flame-kk",
        "flame"
    ],
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
    "osx-cross": [
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
    "windows2012-32": [
        "WINNT_x86-msvc",
        "WINNT_x86-msvc-x86",
        "WINNT_x86-msvc-x64"
    ],

    "windows2012-64": [
        "WINNT_x86_64-msvc",
        "WINNT_x86_64-msvc-x64"
    ],
    "win64": [
        "WINNT_x86_64-msvc",
        "WINNT_x86_64-msvc-x64"
    ]
}


def _retry_on_http_errors(url, verify, params, errors):
    for _ in redo.retrier(sleeptime=5, max_sleeptime=30, attempts=10):
        try:
            req = requests.get(url, verify=verify, params=params)
            req.raise_for_status()
            return req
        except requests.HTTPError as e:
            if e.response.status_code in errors:
                logger.exception("Got HTTP %s trying to reach %s",
                                 e.response.status_code, url)
            else:
                raise
    else:
        raise


def get_releases(product, branch):
    """Returns a list of release names from Balrog.
    :param product: product name, AKA appName
    :param branch: branch name, e.g. mozilla-central
    :return: a list of release names
    """
    url = "{}/releases".format(BALROG_API_ROOT)
    params = {
        "product": product,
        # Adding -nightly-2 (2 stands for the beginning of build ID
        # based on date) should filter out release and latest blobs.
        # This should be changed to -nightly-3 in 3000 ;)
        "name_prefix": "{}-{}-nightly-2".format(product, branch),
        "names_only": True
    }
    params_str = "&".join("=".join([k, str(v)])
                          for k, v in params.iteritems())
    logger.info("Connecting to %s?%s", url, params_str)
    req = _retry_on_http_errors(
        url=url, verify=True, params=params,
        errors=[500])
    releases = req.json()["names"]
    releases = sorted(releases, reverse=True)
    return releases


def get_release_builds(release):
    url = "{}/releases/{}".format(BALROG_API_ROOT, release)
    logger.info("Connecting to %s", url)
    req = _retry_on_http_errors(
        url=url, verify=True, params=None,
        errors=[500])
    return req.json()


def populate_release_history(product, branch, maxbuilds=4, maxsearch=10):
    """Find relevant releases in Balrog
    Not all releases have all platforms and locales, due
    to Taskcluster migration.

        Args:
            product (str): capitalized product name, AKA appName, e.g. Firefox
            branch (str): branch name (mozilla-central)
            maxbuilds (int): Maximum number of historical releases to populate
        Returns:
            json object based on data from balrog api

            results = {
                'platform1': {
                    'locale1': {
                        'buildid1': mar_url,
                        'buildid2': mar_url,
                        'buildid3': mar_url,
                    },
                    'locale2': {
                        'target.partial-1.mar': ('buildid1': 'mar_url'),
                    }
                },
                'platform2': {
                }
            }
        """
    last_releases = get_releases(product, branch)

    partial_mar_tmpl = 'target.partial-{}.mar'

    builds = dict()
    for release in last_releases[:maxsearch]:
        # maxbuilds in all categories, don't make any more queries
        full = len(builds) > 0 and all(len(builds[b][p]) >= maxbuilds for b in builds for p in builds[b])
        if full:
            break
        history = get_release_builds(release)

        for platform in history['platforms']:
            if 'alias' in history['platforms'][platform]:
                continue
            if platform not in builds:
                builds[platform] = dict()
            for locale in history['platforms'][platform]['locales']:
                if locale not in builds[platform]:
                    builds[platform][locale] = dict()
                if len(builds[platform][locale]) >= maxbuilds:
                    continue
                buildid = history['platforms'][platform]['locales'][locale]['buildID']
                url = history['platforms'][platform]['locales'][locale]['completes'][0]['fileUrl']
                nextkey = len(builds[platform][locale]) + 1
                builds[platform][locale][partial_mar_tmpl.format(nextkey)] = {
                    'buildid': buildid,
                    'mar_url': url,
                }
    return builds
