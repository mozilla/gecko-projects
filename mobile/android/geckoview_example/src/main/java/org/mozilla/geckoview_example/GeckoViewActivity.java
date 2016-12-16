/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import org.mozilla.gecko.BaseGeckoInterface;
import org.mozilla.gecko.GeckoProfile;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.GeckoView;

import static org.mozilla.gecko.GeckoView.setGeckoInterface;

public class GeckoViewActivity extends Activity {
    private static final String LOGTAG = "GeckoViewActivity";

    GeckoView mGeckoView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setGeckoInterface(new BaseGeckoInterface(this));

        setContentView(R.layout.geckoview_activity);

        mGeckoView = (GeckoView) findViewById(R.id.gecko_view);
        mGeckoView.setChromeDelegate(new MyGeckoViewChrome());
        mGeckoView.setContentDelegate(new MyGeckoViewContent());
    }

    @Override
    protected void onStart() {
        super.onStart();

        final GeckoProfile profile = GeckoProfile.get(getApplicationContext());

        GeckoThread.init(profile, /* args */ null, /* action */ null, /* debugging */ false);
        GeckoThread.launch();
    }

    private class MyGeckoViewChrome implements GeckoView.ChromeDelegate {
        @Override
        public void onAlert(GeckoView view, String message, GeckoView.PromptResult result) {
            Log.i(LOGTAG, "Alert!");
            result.confirm();
            Toast.makeText(getApplicationContext(), message, Toast.LENGTH_LONG).show();
        }

        @Override
        public void onConfirm(GeckoView view, String message, final GeckoView.PromptResult result) {
            Log.i(LOGTAG, "Confirm!");
            new AlertDialog.Builder(GeckoViewActivity.this)
                .setTitle("javaScript dialog")
                .setMessage(message)
                .setPositiveButton(android.R.string.ok,
                                   new DialogInterface.OnClickListener() {
                                       public void onClick(DialogInterface dialog, int which) {
                                           result.confirm();
                                       }
                                   })
                .setNegativeButton(android.R.string.cancel,
                                   new DialogInterface.OnClickListener() {
                                       public void onClick(DialogInterface dialog, int which) {
                                           result.cancel();
                                       }
                                   })
                .create()
                .show();
        }

        @Override
        public void onPrompt(GeckoView view, String message, String defaultValue, GeckoView.PromptResult result) {
            result.cancel();
        }

        @Override
        public void onDebugRequest(GeckoView view, GeckoView.PromptResult result) {
            Log.i(LOGTAG, "Remote Debug!");
            result.confirm();
        }
    }

    private class MyGeckoViewContent implements GeckoView.ContentDelegate {
        @Override
        public void onPageStart(GeckoView view, String url) {

        }

        @Override
        public void onPageStop(GeckoView view, boolean success) {

        }

        @Override
        public void onPageShow(GeckoView view) {

        }

        @Override
        public void onReceivedTitle(GeckoView view, String title) {
            Log.i(LOGTAG, "Received a title: " + title);
        }

        @Override
        public void onReceivedFavicon(GeckoView view, String url, int size) {
            Log.i(LOGTAG, "Received a favicon URL: " + url);
        }
    }
}
