#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, unicode_literals

import os
import tempfile
import unittest
import zipfile

import mozunit

import mozfile
import mozlog.unstructured as mozlog
import mozprofile

from addon_stubs import generate_addon

here = os.path.dirname(os.path.abspath(__file__))


class TestAddonsManager(unittest.TestCase):
    """ Class to test mozprofile.addons.AddonManager """

    def setUp(self):
        self.logger = mozlog.getLogger('mozprofile.addons')
        self.logger.setLevel(mozlog.ERROR)

        self.profile = mozprofile.profile.Profile()
        self.am = self.profile.addons

        self.profile_path = self.profile.profile
        self.tmpdir = tempfile.mkdtemp()
        self.addCleanup(mozfile.remove, self.tmpdir)

    def test_install_multiple_same_source(self):
        # Generate installer stubs for all possible types of addons
        addon_xpi = generate_addon('test-addon-1@mozilla.org',
                                   path=self.tmpdir)
        addon_folder = generate_addon('test-addon-1@mozilla.org',
                                      path=self.tmpdir,
                                      xpi=False)

        # The same folder should not be installed twice
        self.am.install([addon_folder, addon_folder])
        self.assertEqual(self.am.installed_addons, [addon_folder])
        self.am.clean()

        # The same XPI file should not be installed twice
        self.am.install([addon_xpi, addon_xpi])
        self.assertEqual(self.am.installed_addons, [addon_xpi])
        self.am.clean()

        # Even if it is the same id the add-on should be installed twice, if
        # specified via XPI and folder
        self.am.install([addon_folder, addon_xpi])
        self.assertEqual(len(self.am.installed_addons), 2)
        self.assertIn(addon_folder, self.am.installed_addons)
        self.assertIn(addon_xpi, self.am.installed_addons)
        self.am.clean()

    def test_install_webextension_from_dir(self):
        addon = os.path.join(here, 'addons', 'apply-css.xpi')
        zipped = zipfile.ZipFile(addon)
        try:
            zipped.extractall(self.tmpdir)
        finally:
            zipped.close()
        self.am.install(self.tmpdir)
        self.assertEqual(len(self.am.installed_addons), 1)
        self.assertTrue(os.path.isdir(self.am.installed_addons[0]))

    def test_install_webextension(self):
        addon = os.path.join(here, 'addons', 'apply-css.xpi')
        self.am.install(addon)
        self.assertEqual(len(self.am.installed_addons), 1)
        self.assertTrue(os.path.isfile(self.am.installed_addons[0]))
        self.assertEqual('apply-css.xpi', os.path.basename(self.am.installed_addons[0]))

        details = self.am.addon_details(self.am.installed_addons[0])
        self.assertEqual('test-webext@quality.mozilla.org', details['id'])

    def test_install_webextension_sans_id(self):
        addon = os.path.join(here, 'addons', 'apply-css-sans-id.xpi')
        self.am.install(addon)

        self.assertEqual(len(self.am.installed_addons), 1)
        self.assertTrue(os.path.isfile(self.am.installed_addons[0]))
        self.assertEqual('apply-css-sans-id.xpi', os.path.basename(self.am.installed_addons[0]))

        details = self.am.addon_details(self.am.installed_addons[0])
        self.assertIn('@temporary-addon', details['id'])

    def test_install_xpi(self):
        addons_to_install = []
        addons_installed = []

        # Generate installer stubs and install them
        for ext in ['test-addon-1@mozilla.org', 'test-addon-2@mozilla.org']:
            temp_addon = generate_addon(ext, path=self.tmpdir)
            addons_to_install.append(self.am.addon_details(temp_addon)['id'])
            self.am.install(temp_addon)

        # Generate a list of addons installed in the profile
        addons_installed = [str(x[:-len('.xpi')]) for x in os.listdir(os.path.join(
                            self.profile.profile, 'extensions'))]
        self.assertEqual(addons_to_install.sort(), addons_installed.sort())

    def test_install_folder(self):
        # Generate installer stubs for all possible types of addons
        addons = []
        addons.append(generate_addon('test-addon-1@mozilla.org',
                                     path=self.tmpdir))
        addons.append(generate_addon('test-addon-2@mozilla.org',
                                     path=self.tmpdir,
                                     xpi=False))
        addons.append(generate_addon('test-addon-3@mozilla.org',
                                     path=self.tmpdir,
                                     name='addon-3'))
        addons.append(generate_addon('test-addon-4@mozilla.org',
                                     path=self.tmpdir,
                                     name='addon-4',
                                     xpi=False))
        addons.sort()

        self.am.install(self.tmpdir)

        self.assertEqual(self.am.installed_addons, addons)

    def test_install_unpack(self):
        # Generate installer stubs for all possible types of addons
        addon_xpi = generate_addon('test-addon-unpack@mozilla.org',
                                   path=self.tmpdir)
        addon_folder = generate_addon('test-addon-unpack@mozilla.org',
                                      path=self.tmpdir,
                                      xpi=False)
        addon_no_unpack = generate_addon('test-addon-1@mozilla.org',
                                         path=self.tmpdir)

        # Test unpack flag for add-on as XPI
        self.am.install(addon_xpi)
        self.assertEqual(self.am.installed_addons, [addon_xpi])
        self.am.clean()

        # Test unpack flag for add-on as folder
        self.am.install(addon_folder)
        self.assertEqual(self.am.installed_addons, [addon_folder])
        self.am.clean()

        # Test forcing unpack an add-on
        self.am.install(addon_no_unpack, unpack=True)
        self.assertEqual(self.am.installed_addons, [addon_no_unpack])
        self.am.clean()

    def test_install_after_reset(self):
        # Installing the same add-on after a reset should not cause a failure
        addon = generate_addon('test-addon-1@mozilla.org',
                               path=self.tmpdir, xpi=False)

        # We cannot use self.am because profile.reset() creates a new instance
        self.profile.addons.install(addon)

        self.profile.reset()

        self.profile.addons.install(addon)
        self.assertEqual(self.profile.addons.installed_addons, [addon])

    def test_install_backup(self):
        staged_path = os.path.join(self.profile_path, 'extensions')

        # Generate installer stubs for all possible types of addons
        addon_xpi = generate_addon('test-addon-1@mozilla.org',
                                   path=self.tmpdir)
        addon_folder = generate_addon('test-addon-1@mozilla.org',
                                      path=self.tmpdir,
                                      xpi=False)
        addon_name = generate_addon('test-addon-1@mozilla.org',
                                    path=self.tmpdir,
                                    name='test-addon-1-dupe@mozilla.org')

        # Test backup of xpi files
        self.am.install(addon_xpi)
        self.assertIsNone(self.am.backup_dir)

        self.am.install(addon_xpi)
        self.assertIsNotNone(self.am.backup_dir)
        self.assertEqual(os.listdir(self.am.backup_dir),
                         ['test-addon-1@mozilla.org.xpi'])

        self.am.clean()
        self.assertEqual(os.listdir(staged_path),
                         ['test-addon-1@mozilla.org.xpi'])
        self.am.clean()

        # Test backup of folders
        self.am.install(addon_folder)
        self.assertIsNone(self.am.backup_dir)

        self.am.install(addon_folder)
        self.assertIsNotNone(self.am.backup_dir)
        self.assertEqual(os.listdir(self.am.backup_dir),
                         ['test-addon-1@mozilla.org'])

        self.am.clean()
        self.assertEqual(os.listdir(staged_path),
                         ['test-addon-1@mozilla.org'])
        self.am.clean()

        # Test backup of xpi files with another file name
        self.am.install(addon_name)
        self.assertIsNone(self.am.backup_dir)

        self.am.install(addon_xpi)
        self.assertIsNotNone(self.am.backup_dir)
        self.assertEqual(os.listdir(self.am.backup_dir),
                         ['test-addon-1@mozilla.org.xpi'])

        self.am.clean()
        self.assertEqual(os.listdir(staged_path),
                         ['test-addon-1@mozilla.org.xpi'])
        self.am.clean()

    def test_install_invalid_addons(self):
        # Generate installer stubs for all possible types of addons
        addons = []
        addons.append(generate_addon('test-addon-invalid-no-manifest@mozilla.org',
                                     path=self.tmpdir,
                                     xpi=False))
        addons.append(generate_addon('test-addon-invalid-no-id@mozilla.org',
                                     path=self.tmpdir))

        self.am.install(self.tmpdir)

        self.assertEqual(self.am.installed_addons, [])

    @unittest.skip("Feature not implemented as part of AddonManger")
    def test_install_error(self):
        """ Check install raises an error with an invalid addon"""

        temp_addon = generate_addon('test-addon-invalid-version@mozilla.org')
        # This should raise an error here
        self.am.install(temp_addon)

    def test_addon_details(self):
        # Generate installer stubs for a valid and invalid add-on manifest
        valid_addon = generate_addon('test-addon-1@mozilla.org',
                                     path=self.tmpdir)
        invalid_addon = generate_addon('test-addon-invalid-not-wellformed@mozilla.org',
                                       path=self.tmpdir)

        # Check valid add-on
        details = self.am.addon_details(valid_addon)
        self.assertEqual(details['id'], 'test-addon-1@mozilla.org')
        self.assertEqual(details['name'], 'Test Add-on 1')
        self.assertEqual(details['unpack'], False)
        self.assertEqual(details['version'], '0.1')

        # Check invalid add-on
        self.assertRaises(mozprofile.addons.AddonFormatError,
                          self.am.addon_details, invalid_addon)

        # Check invalid path
        self.assertRaises(IOError,
                          self.am.addon_details, '')

        # Check invalid add-on format
        addon_path = os.path.join(os.path.join(here, 'files'), 'not_an_addon.txt')
        self.assertRaises(mozprofile.addons.AddonFormatError,
                          self.am.addon_details, addon_path)

    @unittest.skip("Bug 900154")
    def test_clean_addons(self):
        addon_one = generate_addon('test-addon-1@mozilla.org')
        addon_two = generate_addon('test-addon-2@mozilla.org')

        self.am.install(addon_one)
        installed_addons = [str(x[:-len('.xpi')]) for x in os.listdir(os.path.join(
                            self.profile.profile, 'extensions'))]

        # Create a new profile based on an existing profile
        # Install an extra addon in the new profile
        # Cleanup addons
        duplicate_profile = mozprofile.profile.Profile(profile=self.profile.profile,
                                                       addons=addon_two)
        duplicate_profile.addons.clean()

        addons_after_cleanup = [str(x[:-len('.xpi')]) for x in os.listdir(os.path.join(
                                duplicate_profile.profile, 'extensions'))]
        # New addons installed should be removed by clean_addons()
        self.assertEqual(installed_addons, addons_after_cleanup)

    def test_noclean(self):
        """test `restore=True/False` functionality"""
        profile = tempfile.mkdtemp()
        tmpdir = tempfile.mkdtemp()

        try:
            # empty initially
            self.assertFalse(bool(os.listdir(profile)))

            # make an addon
            addons = [
                generate_addon('test-addon-1@mozilla.org', path=tmpdir),
                os.path.join(here, 'addons', 'empty.xpi'),
            ]

            # install it with a restore=True AddonManager
            am = mozprofile.addons.AddonManager(profile, restore=True)

            for addon in addons:
                am.install(addon)

            # now its there
            self.assertEqual(os.listdir(profile), ['extensions'])
            staging_folder = os.path.join(profile, 'extensions')
            self.assertTrue(os.path.exists(staging_folder))
            self.assertEqual(len(os.listdir(staging_folder)), 2)

            del am

            self.assertEqual(os.listdir(profile), ['extensions'])
            self.assertTrue(os.path.exists(staging_folder))
            self.assertEqual(os.listdir(staging_folder), [])
        finally:
            mozfile.rmtree(tmpdir)
            mozfile.rmtree(profile)

    def test_remove_addon(self):
        addons = []
        addons.append(generate_addon('test-addon-1@mozilla.org',
                                     path=self.tmpdir))
        addons.append(generate_addon('test-addon-2@mozilla.org',
                                     path=self.tmpdir))

        self.am.install(self.tmpdir)

        extensions_path = os.path.join(self.profile_path, 'extensions')
        staging_path = os.path.join(extensions_path)

        for addon in self.am._addons:
            self.am.remove_addon(addon)

        self.assertEqual(os.listdir(staging_path), [])
        self.assertEqual(os.listdir(extensions_path), [])


if __name__ == '__main__':
    mozunit.main()
