# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from firefox_ui_harness.testcases import FirefoxTestCase


class testPreferences(FirefoxTestCase):

    def setUp(self):
        FirefoxTestCase.setUp(self)

        self.new_pref = 'marionette.unittest.set_pref'
        self.unknown_pref = 'marionette.unittest.unknown'

        self.bool_pref = 'browser.tabs.loadBookmarksInBackground'
        self.int_pref = 'browser.tabs.maxOpenBeforeWarn'
        # Consider using new test preferences
        # See Bug 1303863 Comment #32
        self.string_pref = 'browser.startup.homepage'

    def tearDown(self):
        try:
            self.marionette.clear_pref('marionette.unittest.set_pref')
            self.marionette.clear_pref('marionette.unittest.unknown')
            self.marionette.clear_pref('browser.tabs.loadBookmarksInBackground')
            self.marionette.clear_pref('browser.tabs.maxOpenBeforeWarn')
            self.marionette.clear_pref('browser.startup.homepage')
        finally:
            FirefoxTestCase.tearDown(self)

    def test_get_pref(self):
        # check correct types
        self.assertTrue(isinstance(self.prefs.get_pref(self.bool_pref),
                                   bool))
        self.assertTrue(isinstance(self.prefs.get_pref(self.int_pref),
                                   int))
        self.assertTrue(isinstance(self.prefs.get_pref(self.string_pref),
                                   basestring))

        # unknown
        self.assertIsNone(self.prefs.get_pref(self.unknown_pref))

        # default branch
        orig_value = self.prefs.get_pref(self.int_pref)
        self.prefs.set_pref(self.int_pref, 99999)
        self.assertEqual(self.prefs.get_pref(self.int_pref), 99999)
        self.assertEqual(self.prefs.get_pref(self.int_pref, True), orig_value)

        # complex value
        properties_file = 'chrome://branding/locale/browserconfig.properties'
        self.assertEqual(self.prefs.get_pref('browser.startup.homepage'),
                         properties_file)

        value = self.prefs.get_pref('browser.startup.homepage',
                                    interface='nsIPrefLocalizedString')
        self.assertNotEqual(value, properties_file)

    def test_set_pref_casted_values(self):
        # basestring as boolean
        self.prefs.set_pref(self.bool_pref, '')
        self.assertFalse(self.prefs.get_pref(self.bool_pref))
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.bool_pref)

        self.prefs.set_pref(self.bool_pref, 'unittest')
        self.assertTrue(self.prefs.get_pref(self.bool_pref))
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.bool_pref)

        # int as boolean
        self.prefs.set_pref(self.bool_pref, 0)
        self.assertFalse(self.prefs.get_pref(self.bool_pref))
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.bool_pref)

        self.prefs.set_pref(self.bool_pref, 5)
        self.assertTrue(self.prefs.get_pref(self.bool_pref))
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.bool_pref)

        # boolean as int
        self.prefs.set_pref(self.int_pref, False)
        self.assertEqual(self.prefs.get_pref(self.int_pref), 0)
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.int_pref)

        self.prefs.set_pref(self.int_pref, True)
        self.assertEqual(self.prefs.get_pref(self.int_pref), 1)
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.int_pref)

        # int as string
        self.prefs.set_pref(self.string_pref, 54)
        self.assertEqual(self.prefs.get_pref(self.string_pref), '54')
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.string_pref)

    def test_set_pref_invalid(self):
        self.assertRaises(AssertionError,
                          self.prefs.set_pref, self.new_pref, None)

    def test_set_pref_new_preference(self):
        self.prefs.set_pref(self.new_pref, True)
        self.assertTrue(self.prefs.get_pref(self.new_pref))
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.new_pref)

        self.prefs.set_pref(self.new_pref, 5)
        self.assertEqual(self.prefs.get_pref(self.new_pref), 5)
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.new_pref)

        self.prefs.set_pref(self.new_pref, 'test')
        self.assertEqual(self.prefs.get_pref(self.new_pref), 'test')
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.new_pref)

    def test_set_pref_new_values(self):
        self.prefs.set_pref(self.bool_pref, True)
        self.assertTrue(self.prefs.get_pref(self.bool_pref))
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.bool_pref)

        self.prefs.set_pref(self.int_pref, 99999)
        self.assertEqual(self.prefs.get_pref(self.int_pref), 99999)
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.int_pref)

        self.prefs.set_pref(self.string_pref, 'test_string')
        self.assertEqual(self.prefs.get_pref(self.string_pref), 'test_string')
        # Remove when all self.marionette methods are implemented
        # Please see Bug 1293588
        self.marionette.clear_pref(self.string_pref)
