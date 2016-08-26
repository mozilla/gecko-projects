# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from firefox_ui_harness.testcases import FirefoxTestCase


class TestAboutWindow(FirefoxTestCase):

    def setUp(self):
        FirefoxTestCase.setUp(self)

        self.about_window = self.browser.open_about_window()
        self.deck = self.about_window.deck

    def tearDown(self):
        try:
            self.windows.close_all([self.browser])
        finally:
            FirefoxTestCase.tearDown(self)

    def test_basic(self):
        self.assertEqual(self.about_window.window_type, 'Browser:About')

    def test_elements(self):
        """Test correct retrieval of elements."""
        self.assertNotEqual(self.about_window.dtds, [])

        self.assertEqual(self.deck.element.get_attribute('localName'), 'deck')

        # apply panel
        panel = self.deck.apply
        self.assertEqual(panel.element.get_attribute('localName'), 'hbox')
        self.assertEqual(panel.button.get_attribute('localName'), 'button')

        # check_for_updates panel
        panel = self.deck.check_for_updates
        self.assertEqual(panel.element.get_attribute('localName'), 'hbox')
        self.assertEqual(panel.button.get_attribute('localName'), 'button')

        # checking_for_updates panel
        self.assertEqual(self.deck.checking_for_updates.element.get_attribute('localName'), 'hbox')

        # download_and_install panel
        panel = self.deck.download_and_install
        self.assertEqual(panel.element.get_attribute('localName'), 'hbox')
        self.assertEqual(panel.button.get_attribute('localName'), 'button')

        # download_failed panel
        self.assertEqual(self.deck.download_failed.element.get_attribute('localName'), 'hbox')

        # downloading panel
        self.assertEqual(self.deck.downloading.element.get_attribute('localName'), 'hbox')

    def test_open_window(self):
        """Test various opening strategies."""
        def opener(win):
            self.browser.menubar.select_by_id('helpMenu', 'aboutName')

        open_strategies = ('menu',
                           opener,
                           )

        self.about_window.close()
        for trigger in open_strategies:
            about_window = self.browser.open_about_window(trigger=trigger)
            self.assertEquals(about_window, self.windows.current)
            about_window.close()
