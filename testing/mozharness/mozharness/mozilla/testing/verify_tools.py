#!/usr/bin/env python
# ***** BEGIN LICENSE BLOCK *****
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
# ***** END LICENSE BLOCK *****

import argparse
import os
import re
import mozinfo
from manifestparser import TestManifest
from mozharness.base.script import PostScriptAction

verify_config_options = [
    [["--verify"],
     {"action": "store_true",
      "dest": "verify",
      "default": "False",
      "help": "Run additional verification on modified tests."
      }],
]


class VerifyToolsMixin(object):
    """Utility functions for test verification."""

    def __init__(self):
        self.verify_suites = {}
        self.verify_downloaded = False

    @PostScriptAction('download-and-extract')
    def find_tests_for_verification(self, action, success=None):
        """
           For each file modified on this push, determine if the modified file
           is a test, by searching test manifests. Populate self.verify_suites
           with test files, organized by suite.

           This depends on test manifests, so can only run after test zips have
           been downloaded and extracted.
        """

        if self.config.get('verify') != True:
            return

        repository = os.environ.get("GECKO_HEAD_REPOSITORY")
        revision = os.environ.get("GECKO_HEAD_REV")
        if not repository or not revision:
            self.warning("unable to verify tests: no repo or revision!")
            return []

        def get_automationrelevance():
            response = self.load_json_url(url)
            return response

        dirs = self.query_abs_dirs()
        manifests = [
            (os.path.join(dirs['abs_mochitest_dir'], 'tests', 'mochitest.ini'), 'plain'),
            (os.path.join(dirs['abs_mochitest_dir'], 'chrome', 'chrome.ini'), 'chrome'),
            (os.path.join(dirs['abs_mochitest_dir'], 'browser', 'browser-chrome.ini'), 'browser-chrome'),
            (os.path.join(dirs['abs_mochitest_dir'], 'a11y', 'a11y.ini'), 'a11y'),
            (os.path.join(dirs['abs_xpcshell_dir'], 'tests', 'xpcshell.ini'), 'xpcshell'),
        ]

        tests_by_path = {}
        for (path, suite) in manifests:
            if os.path.exists(path):
                man = TestManifest([path], strict=False)
                active = man.active_tests(exists=False, disabled=False, filters=[], **mozinfo.info)
                tests_by_path.update({t['relpath']:(suite,t.get('subsuite')) for t in active})
                self.info("Verification updated with manifest %s" % path)

        # determine which files were changed on this push
        url = '%s/json-automationrelevance/%s' % (repository.rstrip('/'), revision)
        contents = self.retry(get_automationrelevance, attempts=2, sleeptime=10)
        changed_files = set()
        for c in contents['changesets']:
            self.info(" {cset} {desc}".format(
                cset=c['node'][0:12],
                desc=c['desc'].splitlines()[0].encode('ascii', 'ignore')))
            changed_files |= set(c['files'])

        # for each changed file, determine if it is a test file, and what suite it is in
        for file in changed_files:
            entry = tests_by_path.get(file)
            if entry:
                self.info("Verification found test %s" % file)
                subsuite_mapping = {
                    ('browser-chrome', 'clipboard') : 'browser-chrome-clipboard',
                    ('chrome', 'clipboard') : 'chrome-clipboard',
                    ('plain', 'clipboard') : 'plain-clipboard',
                    ('browser-chrome', 'devtools') : 'mochitest-devtools-chrome',
                    ('browser-chrome', 'gpu') : 'browser-chrome-gpu',
                    ('chrome', 'gpu') : 'chrome-gpu',
                    ('plain', 'gpu') : 'plain-gpu',
                    ('plain', 'media') : 'mochitest-media',
                    ('plain', 'webgl') : 'mochitest-gl',
                }
                if entry in subsuite_mapping:
                    suite = subsuite_mapping[entry]
                else:
                    suite = entry[0]
                suite_files = self.verify_suites.get(suite)
                if not suite_files:
                    suite_files = []
                suite_files.append(file)
                self.verify_suites[suite] = suite_files
        self.verify_downloaded = True

    def query_verify_args(self, suite):
        """
           For the specified suite, return an array of command line arguments to
           be passed to test harnesses when running in verify mode.

           Each array element is an array of command line arguments for a modified
           test in the suite.
        """

        # Limit each test harness run to 15 minutes, to avoid task timeouts
        # when verifying long-running tests.
        MAX_TIME_PER_TEST = 900

        if self.config.get('verify') != True:
            # not in verify mode: run once, with no additional args
            args = [[]]
        else:
            # in verify mode, run nothing by default (unsupported suite or no files modified)
            args = []
            # otherwise, run once for each file in requested suite
            files = self.verify_suites.get(suite)
            for file in files:
                args.append(['--verify-max-time=%d' % MAX_TIME_PER_TEST, '--verify', file])
            self.info("Verification file for '%s': %s" % (suite, files))
        return args

    def query_verify_category_suites(self, category, all_suites):
        """
           In verify mode, determine which suites are active, for the given
           suite category.
        """
        suites = None
        if self.config.get('verify') == True:
            if all_suites and self.verify_downloaded:
                suites = dict((key, all_suites.get(key)) for key in
                    self.verify_suites if key in all_suites.keys())
            else:
                # Until test zips are downloaded, manifests are not available,
                # so it is not possible to determine which suites are active/
                # required for verification; assume all suites from supported
                # suite categories are required.
                if category in ['mochitest', 'xpcshell']:
                    suites = all_suites
        return suites

    def log_verify_status(self, test_name, tbpl_status, log_level):
        """
           Log verification status of a single test. This will display in the
           Job Details pane in treeherder - a convenient summary of verification.
           Special test name formatting is needed because treeherder truncates
           lines that are too long, and may remove duplicates after truncation.
        """
        max_test_name_len = 40
        if len(test_name) > max_test_name_len:
            head = test_name
            new = ""
            previous = None
            max_test_name_len = max_test_name_len - len('.../')
            while len(new) < max_test_name_len:
                head, tail = os.path.split(head)
                previous = new
                new = os.path.join(tail, new)
            test_name = os.path.join('...', previous or new)
            test_name = test_name.rstrip(os.path.sep)
        self.log("TinderboxPrint: Verification of %s<br/>: %s" %
                 (test_name, tbpl_status), level=log_level)

