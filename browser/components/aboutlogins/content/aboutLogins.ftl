# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### This file is not in a locales directory to prevent it from
### being translated as the feature is still in heavy development
### and strings are likely to change often.

### Fluent isn't translating elements in the shadow DOM so the translated strings
### need to be applied to the composed node where they can be moved to the proper
### descendant after translation.

about-logins-page-title = Logins & Passwords

create-login-button = New Login

login-filter =
  .placeholder = Search Logins

login-list =
  .count =
    { $count ->
        [one] { $count } entry
       *[other] { $count } entries
    }

login-item =
  .cancel-button = Cancel
  .copied-password-button = ✓ Copied!
  .copied-username-button = ✓ Copied!
  .copy-password-button = Copy
  .copy-username-button = Copy
  .delete-button = Delete
  .edit-button = Edit
  .hostname-label = Website Address
  .modal-input-reveal-checkbox-hide = Hide password
  .modal-input-reveal-checkbox-show = Show password
  .new-login-title = New Entry
  .open-site-button = Launch
  .password-label = Password
  .save-changes-button = Save Changes
  .time-created = Created: { DATETIME($timeCreated, day: "numeric", month: "long", year: "numeric") }
  .time-changed = Last modified: { DATETIME($timeChanged, day: "numeric", month: "long", year: "numeric") }
  .time-used = Last used: { DATETIME($timeUsed, day: "numeric", month: "long", year: "numeric") }
  .username-label = Username

master-password-notification-message = Please enter your master password to view saved logins & passwords
# TODO: Not sure how to use formatValue with these as attributes on a single ID
master-password-reload-button-label = Log in
# TODO: Not sure how to use formatValue with these as attributes on a single ID
master-password-reload-button-accesskey = L
