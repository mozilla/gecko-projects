#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import sys
import tarfile
import tempfile

import mozinfo
from mozharness.base.script import (
    PreScriptAction,
    PostScriptAction,
)
from mozharness.mozilla.tooltool import TooltoolMixin

code_coverage_config_options = [
    [["--code-coverage"],
     {"action": "store_true",
      "dest": "code_coverage",
      "default": False,
      "help": "Whether gcov c++ code coverage should be run."
      }],
    [["--disable-ccov-upload"],
     {"action": "store_true",
      "dest": "disable_ccov_upload",
      "default": False,
      "help": "Whether test run should package and upload code coverage data."
      }],
    [["--jsd-code-coverage"],
     {"action": "store_true",
      "dest": "jsd_code_coverage",
      "default": False,
      "help": "Whether JSDebugger code coverage should be run."
      }],
]


class CodeCoverageMixin(object):
    """
    Mixin for setting GCOV_PREFIX during test execution, packaging up
    the resulting .gcda files and uploading them to blobber.
    """
    gcov_dir = None
    jsvm_dir = None

    @property
    def code_coverage_enabled(self):
        try:
            if self.config.get('code_coverage'):
                return True

            # XXX workaround because bug 1110465 is hard
            return 'ccov' in self.buildbot_config['properties']['stage_platform']
        except (AttributeError, KeyError, TypeError):
            return False

    @property
    def ccov_upload_disabled(self):
        try:
            if self.config.get('disable_ccov_upload'):
                return True
            return False
        except (AttributeError, KeyError, TypeError):
            return False

    @property
    def jsd_code_coverage_enabled(self):
        try:
            if self.config.get('jsd_code_coverage'):
                return True

            # XXX workaround because bug 1110465 is hard
            return 'jsdcov' in self.buildbot_config['properties']['stage_platform']
        except (AttributeError, KeyError, TypeError):
            return False

    @PreScriptAction('run-tests')
    def _set_gcov_prefix(self, action):
        if not self.code_coverage_enabled:
            return
        # Set the GCOV directory.
        self.gcov_dir = tempfile.mkdtemp()
        os.environ['GCOV_PREFIX'] = self.gcov_dir

        # Set JSVM directory.
        self.jsvm_dir = tempfile.mkdtemp()
        os.environ['JS_CODE_COVERAGE_OUTPUT_DIR'] = self.jsvm_dir

        # Install grcov on the test machine
        # Get the path to the build machines gcno files.
        self.url_to_gcno = self.query_build_dir_url('target.code-coverage-gcno.zip')
        dirs = self.query_abs_dirs()

        # Create the grcov directory, get the tooltool manifest, and finally
        # download and unpack the grcov binary.
        self.grcov_dir = tempfile.mkdtemp()

        if mozinfo.os == 'linux':
            platform = 'linux64'
            tar_file = 'grcov-linux-standalone-x86_64.tar.bz2'
        elif mozinfo.os == 'win':
            platform = 'win32'
            tar_file = 'grcov-win-i686.tar.bz2'

        manifest = os.path.join(dirs.get('abs_test_install_dir', os.path.join(dirs['abs_work_dir'], 'tests')), \
            'config/tooltool-manifests/%s/ccov.manifest' % platform)

        tooltool_path = self._fetch_tooltool_py()
        cmd = [sys.executable, tooltool_path, '--url', 'https://tooltool.mozilla-releng.net/', 'fetch', \
            '-m', manifest, '-o', '-c', '/builds/worker/tooltool-cache']
        self.run_command(cmd, cwd=self.grcov_dir)

        with tarfile.open(os.path.join(self.grcov_dir, tar_file)) as tar:
            tar.extractall(self.grcov_dir)

    @PostScriptAction('run-tests')
    def _package_coverage_data(self, action, success=None):
        if self.jsd_code_coverage_enabled:
            # Setup the command for compression
            dirs = self.query_abs_dirs()
            jsdcov_dir = dirs['abs_blob_upload_dir']
            zipFile = os.path.join(jsdcov_dir, "jsdcov_artifacts.zip")
            command = ["zip", "-r", zipFile, ".", "-i", "jscov*.json"]

            self.info("Beginning compression of JSDCov artifacts...")
            self.run_command(command, cwd=jsdcov_dir)

            # Delete already compressed JSCov artifacts.
            for filename in os.listdir(jsdcov_dir):
                if filename.startswith("jscov") and filename.endswith(".json"):
                    os.remove(os.path.join(jsdcov_dir, filename))

            self.info("Completed compression of JSDCov artifacts!")
            self.info("Path to JSDCov compressed artifacts: " + zipFile)

        if not self.code_coverage_enabled:
            return

        del os.environ['GCOV_PREFIX']
        del os.environ['JS_CODE_COVERAGE_OUTPUT_DIR']

        if not self.ccov_upload_disabled:
            # TODO This is fragile, find rel_topsrcdir properly somehow
            # We need to find the path relative to the gecko topsrcdir. Use
            # some known gecko directories as a test.
            canary_dirs = ['browser', 'docshell', 'dom', 'js', 'layout', 'toolkit', 'xpcom', 'xpfe']
            rel_topsrcdir = None
            for root, dirs, files in os.walk(self.gcov_dir):
                # need to use 'any' in case no gcda data was generated in that subdir.
                if any(d in dirs for d in canary_dirs):
                    rel_topsrcdir = root
                    break

            if rel_topsrcdir is None:
                # Unable to upload code coverage files. Since this is the whole
                # point of code coverage, making this fatal.
                self.fatal("Could not find relative topsrcdir in code coverage data!")

            dirs = self.query_abs_dirs()

            # Package GCOV coverage data.
            file_path_gcda = os.path.join(dirs['abs_blob_upload_dir'], 'code-coverage-gcda.zip')
            self.run_command(['zip', '-r', file_path_gcda, '.'], cwd=rel_topsrcdir)

            # Package JSVM coverage data.
            file_path_jsvm = os.path.join(dirs['abs_blob_upload_dir'], 'code-coverage-jsvm.zip')
            self.run_command(['zip', '-r', file_path_jsvm, '.'], cwd=self.jsvm_dir)

            # GRCOV post-processing
            # Download the gcno fom the build machine.
            self.download_file(self.url_to_gcno, file_name=None, parent_dir=self.grcov_dir)

            if mozinfo.os == 'linux':
                prefix = '/builds/worker/workspace/build/src/'
            elif mozinfo.os == 'win':
                prefix = 'z:/build/build/src/'

            # Run grcov on the zipped .gcno and .gcda files.
            grcov_command = [
                os.path.join(self.grcov_dir, 'grcov'),
                '-t', 'lcov',
                '-p', prefix,
                '--ignore-dir', 'gcc',
                os.path.join(self.grcov_dir, 'target.code-coverage-gcno.zip'), file_path_gcda
            ]

            # 'grcov_output' will be a tuple, the first variable is the path to the lcov output,
            # the other is the path to the standard error output.
            grcov_output = self.get_output_from_command(
                grcov_command,
                cwd=self.grcov_dir,
                silent=True,
                tmpfile_base_path=os.path.join(self.grcov_dir, 'grcov_lcov_output'),
                save_tmpfiles=True,
                return_type='files'
            )
            new_output_name = grcov_output[0] + '.info'
            os.rename(grcov_output[0], new_output_name)

            # Zip the grcov output and upload it.
            self.run_command(
                ['zip', os.path.join(dirs['abs_blob_upload_dir'], 'code-coverage-grcov.zip'), new_output_name],
                cwd=self.grcov_dir
            )

        shutil.rmtree(self.gcov_dir)
        shutil.rmtree(self.jsvm_dir)
        shutil.rmtree(self.grcov_dir)
