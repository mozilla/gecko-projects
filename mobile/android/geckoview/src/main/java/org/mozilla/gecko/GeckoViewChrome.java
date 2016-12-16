/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.os.Bundle;

public class GeckoViewChrome implements GeckoView.ChromeDelegate {
    /**
    * Tell the host application to display an alert dialog.
    * @param view The GeckoView that initiated the callback.
    * @param message The string to display in the dialog.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onAlert(GeckoView view, String message, GeckoView.PromptResult result) {
        result.cancel();
    }

    /**
    * Tell the host application to display a confirmation dialog.
    * @param view The GeckoView that initiated the callback.
    * @param message The string to display in the dialog.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onConfirm(GeckoView view, String message, GeckoView.PromptResult result) {
        result.cancel();
    }

    /**
    * Tell the host application to display an input prompt dialog.
    * @param view The GeckoView that initiated the callback.
    * @param message The string to display in the dialog.
    * @param defaultValue The string to use as default input.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onPrompt(GeckoView view, String message, String defaultValue, GeckoView.PromptResult result) {
        result.cancel();
    }

    /**
    * Tell the host application to display a remote debugging request dialog.
    * @param view The GeckoView that initiated the callback.
    * @param result A PromptResult used to send back the result without blocking.
    * Defaults to cancel requests.
    */
    @Override
    public void onDebugRequest(GeckoView view, GeckoView.PromptResult result) {
        result.cancel();
    }
}
