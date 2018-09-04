# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## The Enterprise Policies feature is aimed at system administrators
## who want to deploy these settings across several Firefox installations
## all at once. This is traditionally done through the Windows Group Policy
## feature, but the system also supports other forms of deployment.
## These are short descriptions for individual policies, to be displayed
## in the documentation section in about:policies.

policy-AppUpdateURL = Set custom app update URL.

policy-Authentication = Configure integrated authentication for websites that support it.

policy-BlockAboutAddons = Block access to the Add-ons Manager (about:addons).

policy-BlockAboutConfig = Block access to the about:config page.

policy-BlockAboutProfiles = Block access to the about:profiles page.

policy-BlockAboutSupport = Block access to the about:support page.

policy-Bookmarks = Create bookmarks in the Bookmarks toolbar, Bookmarks menu, or a specified folder inside them.

policy-Certificates = Whether or not to use built-in certificates. This policy is Windows only at this time.

policy-Cookies = Allow or deny websites to set cookies.

policy-DisableAppUpdate = Prevent the browser from updating.

policy-DisableBuiltinPDFViewer = Disable PDF.js, the built-in PDF viewer in { -brand-short-name }.

policy-DisableDeveloperTools = Block access to the developer tools.

policy-DisableFeedbackCommands = Disable commands to send feedback from the Help menu (Submit Feedback and Report Deceptive Site).

policy-DisableFirefoxAccounts = Disable { -fxaccount-brand-name } based services, including Sync.

# Firefox Screenshots is the name of the feature, and should not be translated.
policy-DisableFirefoxScreenshots = Disable the Firefox Screenshots feature.

policy-DisableFirefoxStudies = Prevent { -brand-short-name } from running studies.

policy-DisableForgetButton = Prevent access to the Forget button.

policy-DisableFormHistory = Don’t remember search and form history.

policy-DisableMasterPasswordCreation = If true, a master password can’t be created.

policy-DisablePocket = Disable the feature to save webpages to Pocket.

policy-DisablePrivateBrowsing = Disable Private Browsing.

policy-DisableProfileImport = Disable the menu command to Import data from another browser.

policy-DisableProfileRefresh = Disable the Refresh { -brand-short-name } button in the about:support page.

policy-DisableSafeMode = Disable the feature to restart in Safe Mode. Note: the Shift key to enter Safe Mode can only be disabled on Windows using Group Policy.

policy-DisableSecurityBypass = Prevent the user from bypassing certain security warnings.

policy-DisableSetDesktopBackground = Disable the menu command Set Image as Desktop Background.

policy-DisableSystemAddonUpdate = Prevent the browser from installing and updating system add-ons.

policy-DisableTelemetry = Turn off Telemetry.

policy-DisplayBookmarksToolbar = Display the Bookmark Toolbar by default.

policy-DisplayMenuBar = Display the Menu Bar by default.

policy-DontCheckDefaultBrowser = Disable check for default browser on startup.

policy-EnableTrackingProtection = Enable or disable Content Blocking and optionally lock it.

policy-Extensions = Install, uninstall or lock extensions. The Install option takes URLs or paths as parameters. The Uninstall and Locked options take extension IDs.

policy-FlashPlugin = Allow or deny usage of the Flash plugin.

policy-HardwareAcceleration = If false, turn off hardware acceleration.

policy-Homepage = Set and optionally lock the homepage.

policy-InstallAddonsPermission = Allow certain websites to install add-ons.

policy-NoDefaultBookmarks = Disable creation of the default bookmarks bundled with { -brand-short-name }, and the Smart Bookmarks (Most Visited, Recent Tags). Note: this policy is only effective if used before the first run of the profile.

policy-OfferToSaveLogins = Enforce the setting to allow { -brand-short-name } to offer to remember saved logins and passwords. Both true and false values are accepted.

policy-OverrideFirstRunPage = Override the first run page. Set this policy to blank if you want to disable the first run page.

policy-OverridePostUpdatePage = Override the post-update "What’s New" page. Set this policy to blank if you want to disable the post-update page.

policy-Permissions = Configure permissions for camera, microphone, location and notifications.

policy-PopupBlocking = Allow certain websites to display popups by default.

policy-Proxy = Configure proxy settings.

policy-SanitizeOnShutdown = Clear all navigation data on shutdown.

policy-SearchBar = Set the default location of the search bar. The user is still allowed to customize it.

policy-SearchEngines = Configure search engine settings. This policy is only available on the Extended Support Release (ESR) version.

policy-WebsiteFilter = Block websites from being visited. See documentation for more details on the format.
