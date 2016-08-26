# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from firefox_ui_harness.testcases import FirefoxTestCase


class TestMixedContentPage(FirefoxTestCase):
    def setUp(self):
        FirefoxTestCase.setUp(self)

        self.locationbar = self.browser.navbar.locationbar
        self.identity_popup = self.locationbar.identity_popup

        self.url = 'https://mozqa.com/data/firefox/security/mixedcontent.html'

    def tearDown(self):
        try:
            self.identity_popup.close(force=True)
        finally:
            FirefoxTestCase.tearDown(self)

    def test_mixed_content(self):
        with self.marionette.using_context('content'):
            self.marionette.navigate(self.url)

        self.assertIn('identity-mixed-passive-loaded',
                      self.locationbar.connection_icon.value_of_css_property('list-style-image'))

        # Open the identity popup
        self.locationbar.open_identity_popup()

        # Only the insecure label is visible in the main view
        secure_label = self.identity_popup.view.main.secure_connection_label
        self.assertEqual(secure_label.value_of_css_property('display'), 'none')

        insecure_label = self.identity_popup.view.main.insecure_connection_label
        self.assertNotEqual(insecure_label.value_of_css_property('display'), 'none')

        # TODO: Bug 1177417 - Needs to open and close the security view, but a second
        # click on the expander doesn't hide the security view
        # self.identity_popup.view.main.expander.click()
        # Wait(self.marionette).until(lambda _: self.identity_popup.view.security.selected)

        # Only the insecure label is visible in the security view
        secure_label = self.identity_popup.view.security.secure_connection_label
        self.assertEqual(secure_label.value_of_css_property('display'), 'none')

        insecure_label = self.identity_popup.view.security.insecure_connection_label
        self.assertNotEqual(insecure_label.value_of_css_property('display'), 'none')

        # owner is not visible
        owner = self.identity_popup.view.security.owner
        self.assertEqual(owner.value_of_css_property('display'), 'none')
