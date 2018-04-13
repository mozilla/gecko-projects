/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * vim: ts=4 sw=4 expandtab:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import java.util.ArrayList;

import android.os.Parcel;
import android.os.Parcelable;
import android.content.Context;
import android.support.annotation.IntDef;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.Log;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

public final class GeckoRuntime implements Parcelable {
    private static final String LOGTAG = "GeckoRuntime";
    private static final boolean DEBUG = false;

    private static GeckoRuntime sDefaultRuntime;

    /**
     * Get the default runtime for the given context.
     * This will create and initialize the runtime with the default settings.
     *
     * Note: Only use this for session-less apps.
     *       For regular apps, use create() and createSession() instead.
     *
     * @return The (static) default runtime for the context.
     */
    public static synchronized @NonNull GeckoRuntime getDefault(
            final @NonNull Context context) {
        Log.d(LOGTAG, "getDefault");
        if (sDefaultRuntime == null) {
            sDefaultRuntime = new GeckoRuntime();
            sDefaultRuntime.attachTo(context);
            sDefaultRuntime.init(new GeckoRuntimeSettings());
        }

        return sDefaultRuntime;
    }

    private GeckoRuntimeSettings mSettings;

    /**
     * Attach the runtime to the given context.
     *
     * @param context The new context to attach to.
     */
    public void attachTo(final @NonNull Context context) {
        if (DEBUG) {
            Log.d(LOGTAG, "attachTo " + context.getApplicationContext());
        }
        final Context appContext = context.getApplicationContext();
        if (!appContext.equals(GeckoAppShell.getApplicationContext())) {
            GeckoAppShell.setApplicationContext(appContext);
        }
    }

    /* package */ boolean init(final @NonNull GeckoRuntimeSettings settings) {
        if (DEBUG) {
            Log.d(LOGTAG, "init");
        }
        final int flags = settings.getUseContentProcessHint()
                          ? GeckoThread.FLAG_PRELOAD_CHILD
                          : 0;
        if (GeckoThread.initMainProcess(/* profile */ null,
                                        settings.getArguments(),
                                        settings.getExtras(),
                                        flags)) {
            if (!GeckoThread.launch()) {
                Log.d(LOGTAG, "init failed (GeckoThread already launched)");
                return false;
            }
            mSettings = settings;
            return true;
        }
        Log.d(LOGTAG, "init failed (could not initiate GeckoThread)");
        return false;
    }

    /**
     * Create a new runtime with default settings and attach it to the given
     * context.
     *
     * Create will throw if there is already an active Gecko instance running,
     * to prevent that, bind the runtime to the process lifetime instead of the
     * activity lifetime.
     *
     * @param context The context of the runtime.
     * @return An initialized runtime.
     */
    public static @NonNull GeckoRuntime create(final @NonNull Context context) {
        return create(context, new GeckoRuntimeSettings());
    }

    /**
     * Create a new runtime with the given settings and attach it to the given
     * context.
     *
     * Create will throw if there is already an active Gecko instance running,
     * to prevent that, bind the runtime to the process lifetime instead of the
     * activity lifetime.
     *
     * @param context The context of the runtime.
     * @param settings The settings for the runtime.
     * @return An initialized runtime.
     */
    public static @NonNull GeckoRuntime create(
        final @NonNull Context context,
        final @NonNull GeckoRuntimeSettings settings) {
        if (DEBUG) {
            Log.d(LOGTAG, "create " + context);
        }

        final GeckoRuntime runtime = new GeckoRuntime();
        runtime.attachTo(context);

        if (!runtime.init(settings)) {
            throw new IllegalStateException("Failed to initialize GeckoRuntime");
        }

        return runtime;
    }

    /**
     * Shutdown the runtime. This will invalidate all attached sessions.
     */
    public void shutdown() {
        if (DEBUG) {
            Log.d(LOGTAG, "shutdown");
        }

        GeckoThread.forceQuit();
    }

    @Override // Parcelable
    public int describeContents() {
        return 0;
    }

    @Override // Parcelable
    public void writeToParcel(Parcel out, int flags) {
        out.writeParcelable(mSettings, flags);
    }

    // AIDL code may call readFromParcel even though it's not part of Parcelable.
    public void readFromParcel(final Parcel source) {
        mSettings = source.readParcelable(getClass().getClassLoader());
    }

    public static final Parcelable.Creator<GeckoRuntime> CREATOR
        = new Parcelable.Creator<GeckoRuntime>() {
        @Override
        public GeckoRuntime createFromParcel(final Parcel in) {
            final GeckoRuntime runtime = new GeckoRuntime();
            runtime.readFromParcel(in);
            return runtime;
        }

        @Override
        public GeckoRuntime[] newArray(final int size) {
            return new GeckoRuntime[size];
        }
    };
}
