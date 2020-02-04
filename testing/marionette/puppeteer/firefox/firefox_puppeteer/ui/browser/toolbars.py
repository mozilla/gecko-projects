# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import

from marionette_driver import By, keys, Wait

from firefox_puppeteer.ui.base import UIBaseLib


class NavBar(UIBaseLib):
    """Provides access to the DOM elements contained in the
    navigation bar as well as the location bar."""

    def __init__(self, *args, **kwargs):
        super(NavBar, self).__init__(*args, **kwargs)

        self._locationbar = None

    @property
    def back_button(self):
        """Provides access to the DOM element back button in the navbar.

        :returns: Reference to the back button.
        """
        return self.marionette.find_element(By.ID, 'back-button')

    @property
    def forward_button(self):
        """Provides access to the DOM element forward button in the navbar.

        :returns: Reference to the forward button.
        """
        return self.marionette.find_element(By.ID, 'forward-button')

    @property
    def home_button(self):
        """Provides access to the DOM element home button in the navbar.

        :returns: Reference to the home button element
        """
        return self.marionette.find_element(By.ID, 'home-button')

    @property
    def locationbar(self):
        """Provides access to the DOM elements contained in the
        locationbar.

        See the :class:`LocationBar` reference.
        """
        if not self._locationbar:
            urlbar = self.marionette.find_element(By.ID, 'urlbar')
            self._locationbar = LocationBar(self.marionette, self.window, urlbar)

        return self._locationbar

    @property
    def menu_button(self):
        """Provides access to the DOM element menu button in the navbar.

        :returns: Reference to the menu button element.
        """
        return self.marionette.find_element(By.ID, 'PanelUI-menu-button')

    @property
    def toolbar(self):
        """The DOM element which represents the navigation toolbar.

        :returns: Reference to the navigation toolbar.
        """
        return self.element


class LocationBar(UIBaseLib):
    """Provides access to and methods for the DOM elements contained in the
    locationbar (the text area of the ui that typically displays the current url)."""

    def __init__(self, *args, **kwargs):
        super(LocationBar, self).__init__(*args, **kwargs)

        self._identity_popup = None

    def clear(self):
        """Clears the contents of the url bar (via the DELETE shortcut)."""
        self.focus('shortcut')
        self.urlbar.send_keys(keys.Keys.DELETE)
        Wait(self.marionette).until(
            lambda _: self.value == '',
            message='Contents of location bar could not be cleared.')

    @property
    def focused(self):
        """Checks the focus state of the location bar.

        :returns: `True` if focused, otherwise `False`
        """
        return self.urlbar.get_attribute('focused') == 'true'

    @property
    def identity_icon(self):
        """ Provides access to the urlbar identity icon.

        :returns: Reference to the identity icon element.
        """
        return self.marionette.find_element(By.ID, 'identity-icon')

    def focus(self, event='click'):
        """Focus the location bar according to the provided event.

        :param eventt: The event to synthesize in order to focus the urlbar
                       (one of `click` or `shortcut`).
        """
        if event == 'click':
            self.urlbar.click()
        elif event == 'shortcut':
            self.window.send_shortcut('l', accel=True)
        else:
            raise ValueError("An unknown event type was passed: %s" % event)

        Wait(self.marionette).until(
            lambda _: self.focused,
            message='Location bar has not be focused.')

    @property
    def identity_box(self):
        """The DOM element which represents the identity box.

        :returns: Reference to the identity box.
        """
        return self.marionette.find_element(By.ID, 'identity-box')

    @property
    def identity_country_label(self):
        """The DOM element which represents the identity icon country label.

        :returns: Reference to the identity icon country label.
        """
        return self.marionette.find_element(By.ID, 'identity-icon-country-label')

    @property
    def identity_organization_label(self):
        """The DOM element which represents the identity icon label.

        :returns: Reference to the identity icon label.
        """
        return self.marionette.find_element(By.ID, 'identity-icon-label')

    @property
    def identity_popup(self):
        """Provides utility members for accessing and manipulating the
        identity popup.

        See the :class:`IdentityPopup` reference.
        """
        if not self._identity_popup:
            popup = self.marionette.find_element(By.ID, 'identity-popup')
            self._identity_popup = IdentityPopup(self.marionette,
                                                 self.window, popup)

        return self._identity_popup

    def load_url(self, url):
        """Load the specified url in the location bar by synthesized
        keystrokes.

        :param url: The url to load.
        """
        self.clear()
        self.focus('shortcut')
        self.urlbar.send_keys(url + keys.Keys.ENTER)

    @property
    def notification_popup(self):
        """Provides access to the DOM element notification popup.

        :returns: Reference to the notification popup.
        """
        return self.marionette.find_element(By.ID, "notification-popup")

    def open_identity_popup(self):
        """Open the identity popup."""
        self.identity_box.click()
        Wait(self.marionette).until(
            lambda _: self.identity_popup.is_open,
            message='Identity popup has not been opened.')

    @property
    def reload_button(self):
        """Provides access to the DOM element reload button.

        :returns: Reference to the reload button.
        """
        return self.marionette.find_element(By.ID, 'reload-button')

    def reload_url(self, trigger='button', force=False):
        """Reload the currently open page.

        :param trigger: The event type to use to cause the reload (one of
                        `shortcut`, `shortcut2`, or `button`).
        :param force: Whether to cause a forced reload.
        """
        # TODO: The force parameter is ignored for the moment. Use
        # mouse event modifiers or actions when they're ready.
        # Bug 1097705 tracks this feature in marionette.
        if trigger == 'button':
            self.reload_button.click()
        elif trigger == 'shortcut':
            self.window.send_shortcut('r')
        elif trigger == 'shortcut2':
            self.window.send_shortcut(keys.Keys.F5)

    @property
    def stop_button(self):
        """Provides access to the DOM element stop button.

        :returns: Reference to the stop button.
        """
        return self.marionette.find_element(By.ID, 'stop-button')

    @property
    def urlbar(self):
        """Provides access to the DOM element urlbar.

        :returns: Reference to the url bar.
        """
        return self.marionette.find_element(By.ID, 'urlbar')

    @property
    def urlbar_input(self):
        """Provides access to the urlbar input element.

        :returns: Reference to the urlbar input.
        """
        return self.marionette.find_element(By.ID, 'urlbar-input')

    @property
    def value(self):
        """Provides access to the currently displayed value of the urlbar.

        :returns: The urlbar value.
        """
        return self.urlbar_input.get_property('value')


class IdentityPopup(UIBaseLib):
    """Wraps DOM elements and methods for interacting with the identity popup."""

    def __init__(self, *args, **kwargs):
        super(IdentityPopup, self).__init__(*args, **kwargs)

        self._view = None

    @property
    def is_open(self):
        """Returns whether this popup is currently open.

        :returns: True when the popup is open, otherwise false.
        """
        return self.element.get_property('state') == 'open'

    def close(self, force=False):
        """Closes the identity popup by hitting the escape key.

        :param force: Optional, If `True` force close the popup.
         Defaults to `False`
        """
        if not self.is_open:
            return

        if force:
            self.marionette.execute_script("""
              arguments[0].hidePopup();
            """, script_args=[self.element])
        else:
            self.element.send_keys(keys.Keys.ESCAPE)

        Wait(self.marionette).until(
            lambda _: not self.is_open,
            message='Identity popup has not been closed.')

    @property
    def view(self):
        """Provides utility members for accessing and manipulating the
        identity popup's multi view.

        See the :class:`IdentityPopupMultiView` reference.
        """
        if not self._view:
            view = self.marionette.find_element(By.ID, 'identity-popup-multiView')
            self._view = IdentityPopupMultiView(self.marionette, self.window, view)

        return self._view


class IdentityPopupMultiView(UIBaseLib):

    def _create_view_for_id(self, view_id):
        """Creates an instance of :class:`IdentityPopupView` for the specified view id.

        :param view_id: The ID of the view to create an instance of.

        :returns: :class:`IdentityPopupView` instance
        """
        mapping = {'identity-popup-mainView': IdentityPopupMainView,
                   'identity-popup-securityView': IdentityPopupSecurityView,
                   }

        view = self.marionette.find_element(By.ID, view_id)
        return mapping.get(view_id, IdentityPopupView)(self.marionette, self.window, view)

    @property
    def main(self):
        """The DOM element which represents the main view.

        :returns: Reference to the main view.
        """
        return self._create_view_for_id('identity-popup-mainView')

    @property
    def security(self):
        """The DOM element which represents the security view.

        :returns: Reference to the security view.
        """
        return self._create_view_for_id('identity-popup-securityView')


class IdentityPopupView(UIBaseLib):

    @property
    def selected(self):
        """Checks if the view is selected.

        :return: `True` if the view is selected.
        """
        return self.element.get_attribute('visible') == 'true'


class IdentityPopupMainView(IdentityPopupView):

    @property
    def selected(self):
        """Checks if the view is selected.

        :return: `True` if the view is selected.
        """
        return self.marionette.execute_script("""
            return arguments[0].panelMultiView.getAttribute('viewtype') == 'main';
        """, script_args=[self.element])

    @property
    def expander(self):
        """The DOM element which represents the expander button for the security content.

        :returns: Reference to the identity popup expander button.
        """
        return self.element.find_element(By.CLASS_NAME, 'identity-popup-expander')

    @property
    def header(self):
        """The DOM element which represents the identity-popup header.

        :returns: Reference to the identity-popup header.
        """
        return self.element.find_element(By.ID, 'identity-popup-mainView-panel-header-span')

    @property
    def insecure_connection_label(self):
        """The DOM element which represents the identity popup insecure connection label.

        :returns: Reference to the identity-popup insecure connection label.
        """
        return self.element.find_element(By.CLASS_NAME, 'identity-popup-connection-not-secure')

    @property
    def internal_connection_label(self):
        """The DOM element which represents the identity popup internal connection label.

        :returns: Reference to the identity-popup internal connection label.
        """
        return self.element.find_element(By.CSS_SELECTOR, 'description[when-connection=chrome]')

    @property
    def permissions(self):
        """The DOM element which represents the identity-popup permissions content.

        :returns: Reference to the identity-popup permissions.
        """
        return self.element.find_element(By.ID, 'identity-popup-permissions-content')

    @property
    def secure_connection_label(self):
        """The DOM element which represents the identity popup secure connection label.

        :returns: Reference to the identity-popup secure connection label.
        """
        return self.element.find_element(By.CLASS_NAME, 'identity-popup-connection-secure')


class IdentityPopupSecurityView(IdentityPopupView):

    @property
    def disable_mixed_content_blocking_button(self):
        """The DOM element which represents the disable mixed content blocking button.

        :returns: Reference to the disable mixed content blocking button.
        """
        return self.element.find_element(By.CSS_SELECTOR,
                                         'button[when-mixedcontent=active-blocked]')

    @property
    def enable_mixed_content_blocking_button(self):
        """The DOM element which represents the enable mixed content blocking button.

        :returns: Reference to the enable mixed content blocking button.
        """
        return self.element.find_element(By.CSS_SELECTOR,
                                         'button[when-mixedcontent=active-loaded]')

    @property
    def host(self):
        """The DOM element which represents the identity-popup content host.

        :returns: Reference to the identity-popup content host.
        """
        return self.element.find_element(By.ID, 'identity-popup-host')

    @property
    def insecure_connection_label(self):
        """The DOM element which represents the identity popup insecure connection label.

        :returns: Reference to the identity-popup insecure connection label.
        """
        return self.element.find_element(By.CLASS_NAME, 'identity-popup-connection-not-secure')

    @property
    def more_info_button(self):
        """The DOM element which represents the identity-popup more info button.

        :returns: Reference to the identity-popup more info button.
        """
        return self.element.find_element(By.ID, 'identity-popup-more-info')

    @property
    def owner(self):
        """The DOM element which represents the identity-popup content owner.

        :returns: Reference to the identity-popup content owner.
        """
        return self.element.find_element(By.ID, 'identity-popup-content-owner')

    @property
    def owner_location(self):
        """The DOM element which represents the identity-popup content supplemental.

        :returns: Reference to the identity-popup content supplemental.
        """
        return self.element.find_element(By.ID, 'identity-popup-content-supplemental')

    @property
    def secure_connection_label(self):
        """The DOM element which represents the identity popup secure connection label.

        :returns: Reference to the identity-popup secure connection label.
        """
        return self.element.find_element(By.CLASS_NAME, 'identity-popup-connection-secure')

    @property
    def verifier(self):
        """The DOM element which represents the identity-popup content verifier.

        :returns: Reference to the identity-popup content verifier.
        """
        return self.element.find_element(By.ID, 'identity-popup-content-verifier')
