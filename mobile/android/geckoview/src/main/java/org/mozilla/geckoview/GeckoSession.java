/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * vim: ts=4 sw=4 expandtab:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import java.net.URLConnection;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.UUID;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.gfx.LayerSession;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoEditableChild;
import org.mozilla.gecko.GeckoThread;
import org.mozilla.gecko.IGeckoEditableParent;
import org.mozilla.gecko.mozglue.JNIObject;
import org.mozilla.gecko.NativeQueue;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.Resources;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.SystemClock;
import android.support.annotation.Nullable;
import android.support.annotation.NonNull;
import android.util.Log;

public class GeckoSession extends LayerSession
                          implements Parcelable {
    private static final String LOGTAG = "GeckoSession";
    private static final boolean DEBUG = false;

    // Type of changes given to onWindowChanged.
    // Window has been cleared due to the session being closed.
    private static final int WINDOW_CLOSE = 0;
    // Window has been set due to the session being opened.
    private static final int WINDOW_OPEN = 1; // Window has been opened.
    // Window has been cleared due to the session being transferred to another session.
    private static final int WINDOW_TRANSFER_OUT = 2; // Window has been transfer.
    // Window has been set due to another session being transferred to this one.
    private static final int WINDOW_TRANSFER_IN = 3;

    private enum State implements NativeQueue.State {
        INITIAL(0),
        READY(1);

        private final int mRank;

        private State(int rank) {
            mRank = rank;
        }

        @Override
        public boolean is(final NativeQueue.State other) {
            return this == other;
        }

        @Override
        public boolean isAtLeast(final NativeQueue.State other) {
            return (other instanceof State) &&
                   mRank >= ((State) other).mRank;
        }
    }

    private final NativeQueue mNativeQueue =
        new NativeQueue(State.INITIAL, State.READY);

    private final EventDispatcher mEventDispatcher =
        new EventDispatcher(mNativeQueue);

    private final TextInputController mTextInput = new TextInputController(this, mNativeQueue);

    private String mId = UUID.randomUUID().toString().replace("-", "");
    /* package */ String getId() { return mId; }

    private final GeckoSessionHandler<ContentDelegate> mContentHandler =
        new GeckoSessionHandler<ContentDelegate>(
            "GeckoViewContent", this,
            new String[]{
                "GeckoView:ContextMenu",
                "GeckoView:DOMTitleChanged",
                "GeckoView:DOMWindowFocus",
                "GeckoView:DOMWindowClose",
                "GeckoView:FullScreenEnter",
                "GeckoView:FullScreenExit"
            }
        ) {
            @Override
            public void handleMessage(final ContentDelegate delegate,
                                      final String event,
                                      final GeckoBundle message,
                                      final EventCallback callback) {

                if ("GeckoView:ContextMenu".equals(event)) {
                    delegate.onContextMenu(GeckoSession.this,
                                           message.getInt("screenX"),
                                           message.getInt("screenY"),
                                           message.getString("uri"),
                                           message.getString("elementSrc"));
                } else if ("GeckoView:DOMTitleChanged".equals(event)) {
                    delegate.onTitleChange(GeckoSession.this,
                                           message.getString("title"));
                } else if ("GeckoView:DOMWindowFocus".equals(event)) {
                    delegate.onFocusRequest(GeckoSession.this);
                } else if ("GeckoView:DOMWindowClose".equals(event)) {
                    delegate.onCloseRequest(GeckoSession.this);
                } else if ("GeckoView:FullScreenEnter".equals(event)) {
                    delegate.onFullScreen(GeckoSession.this, true);
                } else if ("GeckoView:FullScreenExit".equals(event)) {
                    delegate.onFullScreen(GeckoSession.this, false);
                }
            }
        };

    private final GeckoSessionHandler<NavigationDelegate> mNavigationHandler =
        new GeckoSessionHandler<NavigationDelegate>(
            "GeckoViewNavigation", this,
            new String[]{
                "GeckoView:LocationChange",
                "GeckoView:OnLoadRequest",
                "GeckoView:OnNewSession"
            }
        ) {
            // This needs to match nsIBrowserDOMWindow.idl
            private int convertGeckoTarget(int geckoTarget) {
                switch (geckoTarget) {
                    case 0: // OPEN_DEFAULTWINDOW
                    case 1: // OPEN_CURRENTWINDOW
                        return NavigationDelegate.TARGET_WINDOW_CURRENT;
                    default: // OPEN_NEWWINDOW, OPEN_NEWTAB, OPEN_SWITCHTAB
                        return NavigationDelegate.TARGET_WINDOW_NEW;
                }
            }

            @Override
            public void handleMessage(final NavigationDelegate delegate,
                                      final String event,
                                      final GeckoBundle message,
                                      final EventCallback callback) {
                if ("GeckoView:LocationChange".equals(event)) {
                    delegate.onLocationChange(GeckoSession.this,
                                              message.getString("uri"));
                    delegate.onCanGoBack(GeckoSession.this,
                                         message.getBoolean("canGoBack"));
                    delegate.onCanGoForward(GeckoSession.this,
                                            message.getBoolean("canGoForward"));
                } else if ("GeckoView:OnLoadRequest".equals(event)) {
                    final String uri = message.getString("uri");
                    final int where = convertGeckoTarget(message.getInt("where"));
                    final boolean result =
                        delegate.onLoadRequest(GeckoSession.this, uri, where);
                    callback.sendSuccess(result);
                } else if ("GeckoView:OnNewSession".equals(event)) {
                    final String uri = message.getString("uri");
                    delegate.onNewSession(GeckoSession.this, uri,
                        new Response<GeckoSession>() {
                            @Override
                            public void respond(GeckoSession session) {
                                if (session == null) {
                                    callback.sendSuccess(null);
                                    return;
                                }

                                if (session.isOpen()) {
                                    throw new IllegalArgumentException("Must use an unopened GeckoSession instance");
                                }

                                session.open(null);
                                callback.sendSuccess(session.getId());
                            }
                        });
                }
            }
        };

    private final GeckoSessionHandler<ProgressDelegate> mProgressHandler =
        new GeckoSessionHandler<ProgressDelegate>(
            "GeckoViewProgress", this,
            new String[]{
                "GeckoView:PageStart",
                "GeckoView:PageStop",
                "GeckoView:SecurityChanged"
            }
        ) {
            @Override
            public void handleMessage(final ProgressDelegate delegate,
                                      final String event,
                                      final GeckoBundle message,
                                      final EventCallback callback) {
                if ("GeckoView:PageStart".equals(event)) {
                    delegate.onPageStart(GeckoSession.this,
                                         message.getString("uri"));
                } else if ("GeckoView:PageStop".equals(event)) {
                    delegate.onPageStop(GeckoSession.this,
                                        message.getBoolean("success"));
                } else if ("GeckoView:SecurityChanged".equals(event)) {
                    final GeckoBundle identity = message.getBundle("identity");
                    delegate.onSecurityChange(GeckoSession.this, new ProgressDelegate.SecurityInformation(identity));
                }
            }
        };

    private final GeckoSessionHandler<ScrollDelegate> mScrollHandler =
        new GeckoSessionHandler<ScrollDelegate>(
            "GeckoViewScroll", this,
            new String[]{ "GeckoView:ScrollChanged" }
        ) {
            @Override
            public void handleMessage(final ScrollDelegate delegate,
                                      final String event,
                                      final GeckoBundle message,
                                      final EventCallback callback) {

                if ("GeckoView:ScrollChanged".equals(event)) {
                    delegate.onScrollChanged(GeckoSession.this,
                                             message.getInt("scrollX"),
                                             message.getInt("scrollY"));
                }
            }
        };

    private final GeckoSessionHandler<TrackingProtectionDelegate> mTrackingProtectionHandler =
        new GeckoSessionHandler<TrackingProtectionDelegate>(
            "GeckoViewTrackingProtection", this,
            new String[]{ "GeckoView:TrackingProtectionBlocked" }
        ) {
            @Override
            public void handleMessage(final TrackingProtectionDelegate delegate,
                                      final String event,
                                      final GeckoBundle message,
                                      final EventCallback callback) {

                if ("GeckoView:TrackingProtectionBlocked".equals(event)) {
                    final String uri = message.getString("src");
                    final String matchedList = message.getString("matchedList");
                    delegate.onTrackerBlocked(GeckoSession.this, uri,
                        TrackingProtection.listToCategory(matchedList));
                }
            }
        };

    private final GeckoSessionHandler<PermissionDelegate> mPermissionHandler =
        new GeckoSessionHandler<PermissionDelegate>(
            "GeckoViewPermission", this,
            new String[] {
                "GeckoView:AndroidPermission",
                "GeckoView:ContentPermission",
                "GeckoView:MediaPermission"
            }, /* alwaysListen */ true
        ) {
            @Override
            public void handleMessage(final PermissionDelegate delegate,
                                      final String event,
                                      final GeckoBundle message,
                                      final EventCallback callback) {

                if (delegate == null) {
                    callback.sendSuccess(/* granted */ false);
                    return;
                }
                if ("GeckoView:AndroidPermission".equals(event)) {
                    delegate.onAndroidPermissionsRequest(
                            GeckoSession.this, message.getStringArray("perms"),
                            new PermissionCallback("android", callback));
                } else if ("GeckoView:ContentPermission".equals(event)) {
                    final String typeString = message.getString("perm");
                    final int type;
                    if ("geolocation".equals(typeString)) {
                        type = PermissionDelegate.PERMISSION_GEOLOCATION;
                    } else if ("desktop_notification".equals(typeString)) {
                        type = PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION;
                    } else {
                        throw new IllegalArgumentException("Unknown permission request: " + typeString);
                    }
                    delegate.onContentPermissionRequest(
                            GeckoSession.this, message.getString("uri"),
                            type, message.getString("access"),
                            new PermissionCallback(typeString, callback));
                } else if ("GeckoView:MediaPermission".equals(event)) {
                    GeckoBundle[] videoBundles = message.getBundleArray("video");
                    GeckoBundle[] audioBundles = message.getBundleArray("audio");
                    PermissionDelegate.MediaSource[] videos = null;
                    PermissionDelegate.MediaSource[] audios = null;

                    if (videoBundles != null) {
                        videos = new PermissionDelegate.MediaSource[videoBundles.length];
                        for (int i = 0; i < videoBundles.length; i++) {
                            videos[i] = new PermissionDelegate.MediaSource(videoBundles[i]);
                        }
                    }

                    if (audioBundles != null) {
                        audios = new PermissionDelegate.MediaSource[audioBundles.length];
                        for (int i = 0; i < audioBundles.length; i++) {
                            audios[i] = new PermissionDelegate.MediaSource(audioBundles[i]);
                        }
                    }

                    delegate.onMediaPermissionRequest(
                            GeckoSession.this, message.getString("uri"),
                            videos, audios, new PermissionCallback("media", callback));
                }
            }
        };

    /* package */ int handlersCount;

    private final GeckoSessionHandler<?>[] mSessionHandlers = new GeckoSessionHandler<?>[] {
        mContentHandler, mNavigationHandler, mProgressHandler, mScrollHandler,
        mTrackingProtectionHandler, mPermissionHandler
    };

    private static class PermissionCallback implements
        PermissionDelegate.Callback, PermissionDelegate.MediaCallback {

        private final String mType;
        private EventCallback mCallback;

        public PermissionCallback(final String type, final EventCallback callback) {
            mType = type;
            mCallback = callback;
        }

        private void submit(final Object response) {
            if (mCallback != null) {
                mCallback.sendSuccess(response);
                mCallback = null;
            }
        }

        @Override // PermissionDelegate.Callback
        public void grant() {
            if ("media".equals(mType)) {
                throw new UnsupportedOperationException();
            }
            submit(/* response */ true);
        }

        @Override // PermissionDelegate.Callback, PermissionDelegate.MediaCallback
        public void reject() {
            submit(/* response */ false);
        }

        @Override // PermissionDelegate.MediaCallback
        public void grant(final String video, final String audio) {
            if (!"media".equals(mType)) {
                throw new UnsupportedOperationException();
            }
            final GeckoBundle response = new GeckoBundle(2);
            response.putString("video", video);
            response.putString("audio", audio);
            submit(response);
        }

        @Override // PermissionDelegate.MediaCallback
        public void grant(final PermissionDelegate.MediaSource video, final PermissionDelegate.MediaSource audio) {
            grant(video != null ? video.id : null,
                  audio != null ? audio.id : null);
        }
    }

    /**
     * Get the current prompt delegate for this GeckoSession.
     * @return PromptDelegate instance or null if using default delegate.
     */
    public PermissionDelegate getPermissionDelegate() {
        return mPermissionHandler.getDelegate();
    }

    /**
     * Set the current permission delegate for this GeckoSession.
     * @param delegate PermissionDelegate instance or null to use the default delegate.
     */
    public void setPermissionDelegate(final PermissionDelegate delegate) {
        mPermissionHandler.setDelegate(delegate, this);
    }

    private PromptDelegate mPromptDelegate;

    private final Listener mListener = new Listener();

    /* package */ static final class Window extends JNIObject implements IInterface {
        private NativeQueue mNativeQueue;
        private Binder mBinder;

        public Window(final NativeQueue nativeQueue) {
            mNativeQueue = nativeQueue;
        }

        @Override // IInterface
        public Binder asBinder() {
            if (mBinder == null) {
                mBinder = new Binder();
                mBinder.attachInterface(this, Window.class.getName());
            }
            return mBinder;
        }

        // Create a new Gecko window and assign an initial set of Java session objects to it.
        @WrapForJNI(dispatchTo = "proxy")
        public static native void open(Window instance, NativeQueue queue,
                                       Compositor compositor, EventDispatcher dispatcher,
                                       GeckoBundle settings, String id, String chromeUri,
                                       int screenId, boolean privateMode);

        @Override // JNIObject
        public void disposeNative() {
            if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
                nativeDisposeNative();
            } else {
                GeckoThread.queueNativeCallUntil(GeckoThread.State.PROFILE_READY,
                        this, "nativeDisposeNative");
            }
        }

        @WrapForJNI(dispatchTo = "proxy", stubName = "DisposeNative")
        private native void nativeDisposeNative();

        // Force the underlying Gecko window to close and release assigned Java objects.
        public void close() {
            // Reset our queue, so we don't end up with queued calls on a disposed object.
            synchronized (this) {
                if (mNativeQueue == null) {
                    // Already closed elsewhere.
                    return;
                }
                mNativeQueue.reset(State.INITIAL);
                mNativeQueue = null;
            }

            // Detach ourselves from the binder as well, to prevent this window from being
            // read from any parcels.
            asBinder().attachInterface(null, Window.class.getName());

            if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
                nativeClose();
            } else {
                GeckoThread.queueNativeCallUntil(GeckoThread.State.PROFILE_READY,
                        this, "nativeClose");
            }
        }

        @WrapForJNI(dispatchTo = "proxy", stubName = "Close")
        private native void nativeClose();

        // Assign a new set of Java session objects to the underlying Gecko window.
        // This replaces previously assigned objects from open() or transfer() calls.
        public synchronized void transfer(final NativeQueue queue,
                                          final Compositor compositor,
                                          final EventDispatcher dispatcher,
                                          final GeckoBundle settings) {
            if (mNativeQueue == null) {
                // Already closed.
                return;
            }

            if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
                nativeTransfer(queue, compositor, dispatcher, settings);
            } else {
                GeckoThread.queueNativeCallUntil(GeckoThread.State.PROFILE_READY,
                        this, "nativeTransfer",
                        NativeQueue.class, queue,
                        Compositor.class, compositor,
                        EventDispatcher.class, dispatcher,
                        GeckoBundle.class, settings);
            }

            if (mNativeQueue != queue) {
                // Reset the old queue to prevent old events from affecting this window.
                // Gecko will call onReady later with the new queue if needed.
                mNativeQueue.reset(State.INITIAL);
                mNativeQueue = queue;
            }
        }

        @WrapForJNI(dispatchTo = "proxy", stubName = "Transfer")
        private native void nativeTransfer(NativeQueue queue, Compositor compositor,
                                           EventDispatcher dispatcher, GeckoBundle settings);

        @WrapForJNI(dispatchTo = "proxy")
        public native void attachEditable(IGeckoEditableParent parent,
                                          GeckoEditableChild child);

        @WrapForJNI(calledFrom = "gecko")
        private synchronized void onReady(final @Nullable NativeQueue queue) {
            // onReady is called the first time the Gecko window is ready, with a null queue
            // argument. In this case, we simply set the current queue to ready state.
            //
            // After the initial call, onReady is called again every time Window.transfer()
            // is called, with a non-null queue argument. In this case, we only set the
            // current queue to ready state _if_ the current queue matches the given queue,
            // because if the queues don't match, we know there is another onReady call coming.

            if ((queue == null && mNativeQueue == null) ||
                (queue != null && mNativeQueue != queue)) {
                return;
            }

            if (mNativeQueue.checkAndSetState(State.INITIAL, State.READY) &&
                    queue == null) {
                Log.i(LOGTAG, "zerdatime " + SystemClock.elapsedRealtime() +
                      " - chrome startup finished");
            }
        }
    }

    private class Listener implements BundleEventListener {
        /* package */ void registerListeners() {
            getEventDispatcher().registerUiThreadListener(this,
                "GeckoView:Prompt",
                null);
        }

        @Override
        public void handleMessage(final String event, final GeckoBundle message,
                                  final EventCallback callback) {
            if (DEBUG) {
                Log.d(LOGTAG, "handleMessage: event = " + event);
            }

            if ("GeckoView:Prompt".equals(event)) {
                handlePromptEvent(GeckoSession.this, message, callback);
            }
        }
    }

    protected Window mWindow;
    private GeckoSessionSettings mSettings;

    public GeckoSession() {
        this(null);
    }

    public GeckoSession(final GeckoSessionSettings settings) {
        mSettings = new GeckoSessionSettings(settings, this);
        mListener.registerListeners();

        if (BuildConfig.DEBUG && handlersCount != mSessionHandlers.length) {
            throw new AssertionError("Add new handler to handlers list");
        }
    }

    private void transferFrom(final Window window, final GeckoSessionSettings settings,
                              final String id) {
        if (isOpen()) {
            throw new IllegalStateException("Session is open");
        }

        if (window != null) {
            onWindowChanged(WINDOW_TRANSFER_IN, /* inProgress */ true);
        }

        mWindow = window;
        mSettings = new GeckoSessionSettings(settings, this);
        mId = id;

        if (mWindow != null) {
            mWindow.transfer(mNativeQueue, mCompositor,
                             mEventDispatcher, mSettings.asBundle());

            onWindowChanged(WINDOW_TRANSFER_IN, /* inProgress */ false);
        }
    }

    /* package */ void transferFrom(final GeckoSession session) {
        final boolean changing = (session.mWindow != null);
        if (changing) {
            session.onWindowChanged(WINDOW_TRANSFER_OUT, /* inProgress */ true);
        }

        transferFrom(session.mWindow, session.mSettings, session.mId);
        session.mWindow = null;

        if (changing) {
            session.onWindowChanged(WINDOW_TRANSFER_OUT, /* inProgress */ false);
        }
    }

    @Override // Parcelable
    public int describeContents() {
        return 0;
    }

    @Override // Parcelable
    public void writeToParcel(Parcel out, int flags) {
        out.writeStrongInterface(mWindow);
        out.writeParcelable(mSettings, flags);
        out.writeString(mId);
    }

    // AIDL code may call readFromParcel even though it's not part of Parcelable.
    public void readFromParcel(final Parcel source) {
        final IBinder binder = source.readStrongBinder();
        final IInterface ifce = (binder != null) ?
                binder.queryLocalInterface(Window.class.getName()) : null;
        final Window window = (ifce instanceof Window) ? (Window) ifce : null;
        final GeckoSessionSettings settings =
                source.readParcelable(getClass().getClassLoader());
        final String id = source.readString();
        transferFrom(window, settings, id);
    }

    public static final Creator<GeckoSession> CREATOR = new Creator<GeckoSession>() {
        @Override
        public GeckoSession createFromParcel(final Parcel in) {
            final GeckoSession session = new GeckoSession();
            session.readFromParcel(in);
            return session;
        }

        @Override
        public GeckoSession[] newArray(final int size) {
            return new GeckoSession[size];
        }
    };

    /**
     * Preload GeckoSession by starting Gecko in the background, if Gecko is not already running.
     *
     * @param context Activity or Application Context for starting GeckoSession.
     */
    public static void preload(final @NonNull Context context) {
        preload(context, /* geckoArgs */ null,
                /* extras */ null, /* multiprocess */ false);
    }

    /**
     * Preload GeckoSession by starting Gecko with the specified arguments in the background,
     * if Gecko is not already running.
     *
     * @param context Activity or Application Context for starting GeckoSession.
     * @param geckoArgs Arguments to be passed to Gecko, if Gecko is not already running.
     * @param multiprocess True if child process in multiprocess mode should be preloaded.
     */
    public static void preload(final @NonNull Context context,
                               final @Nullable String[] geckoArgs,
                               final @Nullable Bundle extras,
                               final boolean multiprocess) {
        final Context appContext = context.getApplicationContext();
        if (!appContext.equals(GeckoAppShell.getApplicationContext())) {
            GeckoAppShell.setApplicationContext(appContext);
        }

        if (GeckoThread.isLaunched()) {
            return;
        }

        final int flags = multiprocess ? GeckoThread.FLAG_PRELOAD_CHILD : 0;
        if (GeckoThread.initMainProcess(/* profile */ null, geckoArgs, extras, flags)) {
            GeckoThread.launch();
        }
    }

    public boolean isOpen() {
        return mWindow != null;
    }

    /* package */ boolean isReady() {
        return mNativeQueue.isReady();
    }

    /**
     * Opens the session.
     *
     * The session is in a 'closed' state when first created. Opening it creates
     * the underlying Gecko objects necessary to load a page, etc. Most GeckoSession
     * methods only take affect on an open session, and are queued until the session
     * is opened here. Opening a session is an asynchronous operation. You can check
     * the current state via isOpen().
     *
     * Call this when you are ready to use a GeckoSession instance.
     *
     * @param appContext An application context
     */
    public void open(final @Nullable Context appContext) {
        ThreadUtils.assertOnUiThread();

        if (isOpen()) {
            throw new IllegalStateException("Session is open");
        }

        if (appContext != null) {
            final boolean multiprocess =
                    mSettings.getBoolean(GeckoSessionSettings.USE_MULTIPROCESS);
            preload(appContext, /* geckoArgs */ null, /* extras */ null, multiprocess);
        }

        openWindow();
    }

    private void openWindow() {
        final String chromeUri = mSettings.getString(GeckoSessionSettings.CHROME_URI);
        final int screenId = mSettings.getInt(GeckoSessionSettings.SCREEN_ID);
        final boolean isPrivate = mSettings.getBoolean(GeckoSessionSettings.USE_PRIVATE_MODE);

        mWindow = new Window(mNativeQueue);

        onWindowChanged(WINDOW_OPEN, /* inProgress */ true);

        if (GeckoThread.isStateAtLeast(GeckoThread.State.PROFILE_READY)) {
            Window.open(mWindow, mNativeQueue, mCompositor, mEventDispatcher,
                        mSettings.asBundle(), mId, chromeUri, screenId, isPrivate);
        } else {
            GeckoThread.queueNativeCallUntil(
                GeckoThread.State.PROFILE_READY,
                Window.class, "open",
                Window.class, mWindow,
                NativeQueue.class, mNativeQueue,
                Compositor.class, mCompositor,
                EventDispatcher.class, mEventDispatcher,
                GeckoBundle.class, mSettings.asBundle(),
                String.class, mId,
                String.class, chromeUri,
                screenId, isPrivate);
        }

        onWindowChanged(WINDOW_OPEN, /* inProgress */ false);
    }

    /**
     * Closes the session.
     *
     * This frees the underlying Gecko objects and unloads the current page. The session may be
     * reopened later, but page state is not restored. Call this when you are finished using
     * a GeckoSession instance.
     */
    public void close() {
        ThreadUtils.assertOnUiThread();

        if (!isOpen()) {
            Log.w(LOGTAG, "Attempted to close a GeckoSession that was already closed.");
            return;
        }

        onWindowChanged(WINDOW_CLOSE, /* inProgress */ true);

        mWindow.close();
        mWindow.disposeNative();
        mWindow = null;

        onWindowChanged(WINDOW_CLOSE, /* inProgress */ false);
    }

    private void onWindowChanged(int change, boolean inProgress) {
        if ((change == WINDOW_OPEN || change == WINDOW_TRANSFER_IN) && !inProgress) {
            mTextInput.onWindowChanged(mWindow);
        }

        if (change == WINDOW_CLOSE) {
            // Detach when window is closing, and reattach immediately after window is closed.
            // We reattach immediate after closing because we want any actions performed while the
            // session is closed to be properly queued, until the session is open again.
            for (final GeckoSessionHandler<?> handler : mSessionHandlers) {
                handler.setSessionIsReady(getEventDispatcher(), !inProgress);
            }
        }
    }

    /**
     * Get the TextInputController instance for this session.
     *
     * @return TextInputController instance.
     */
    public @NonNull TextInputController getTextInputController() {
        // May be called on any thread.
        return mTextInput;
    }

    /**
    * Load the given URI.
    * @param uri The URI of the resource to load.
    */
    public void loadUri(String uri) {
        final GeckoBundle msg = new GeckoBundle();
        msg.putString("uri", uri);
        mEventDispatcher.dispatch("GeckoView:LoadUri", msg);
    }

    /**
    * Load the given URI.
    * @param uri The URI of the resource to load.
    */
    public void loadUri(Uri uri) {
        loadUri(uri.toString());
    }

    /**
    * Reload the current URI.
    */
    public void reload() {
        mEventDispatcher.dispatch("GeckoView:Reload", null);
    }

    /**
    * Stop loading.
    */
    public void stop() {
        mEventDispatcher.dispatch("GeckoView:Stop", null);
    }

    /**
    * Go back in history.
    */
    public void goBack() {
        mEventDispatcher.dispatch("GeckoView:GoBack", null);
    }

    /**
    * Go forward in history.
    */
    public void goForward() {
        mEventDispatcher.dispatch("GeckoView:GoForward", null);
    }

    /**
    * Set this GeckoSession as active or inactive. Setting a GeckoSession to inactive will
    * significantly reduce its memory footprint, but should only be done if the
    * GeckoSession is not currently visible.
    * @param active A boolean determining whether the GeckoSession is active
    */
    public void setActive(boolean active) {
        final GeckoBundle msg = new GeckoBundle();
        msg.putBoolean("active", active);
        mEventDispatcher.dispatch("GeckoView:SetActive", msg);
    }

    public GeckoSessionSettings getSettings() {
        return mSettings;
    }

    public void importScript(final String url) {
        if (url.startsWith("resource://android/assets/")) {
            final GeckoBundle data = new GeckoBundle(1);
            data.putString("scriptURL", url);
            getEventDispatcher().dispatch("GeckoView:ImportScript", data);
            return;
        }

        throw new IllegalArgumentException("Must import script from 'resources://android/assets/' location.");
    }

    /**
    * Exits fullscreen mode
    */
    public void exitFullScreen() {
        mEventDispatcher.dispatch("GeckoViewContent:ExitFullScreen", null);
    }

    /**
    * Set the content callback handler.
    * This will replace the current handler.
    * @param delegate An implementation of ContentDelegate.
    */
    public void setContentDelegate(ContentDelegate delegate) {
        mContentHandler.setDelegate(delegate, this);
    }

    /**
    * Get the content callback handler.
    * @return The current content callback handler.
    */
    public ContentDelegate getContentDelegate() {
        return mContentHandler.getDelegate();
    }

    /**
    * Set the progress callback handler.
    * This will replace the current handler.
    * @param delegate An implementation of ProgressDelegate.
    */
    public void setProgressDelegate(ProgressDelegate delegate) {
        mProgressHandler.setDelegate(delegate, this);
    }

    /**
    * Get the progress callback handler.
    * @return The current progress callback handler.
    */
    public ProgressDelegate getProgressDelegate() {
        return mProgressHandler.getDelegate();
    }

    /**
    * Set the navigation callback handler.
    * This will replace the current handler.
    * @param delegate An implementation of NavigationDelegate.
    */
    public void setNavigationDelegate(NavigationDelegate delegate) {
        mNavigationHandler.setDelegate(delegate, this);
    }

    /**
    * Get the navigation callback handler.
    * @return The current navigation callback handler.
    */
    public NavigationDelegate getNavigationDelegate() {
        return mNavigationHandler.getDelegate();
    }

    /**
    * Set the content scroll callback handler.
    * This will replace the current handler.
    * @param delegate An implementation of ScrollDelegate.
    */
    public void setScrollDelegate(ScrollDelegate delegate) {
        mScrollHandler.setDelegate(delegate, this);
    }

    public ScrollDelegate getScrollDelegate() {
        return mScrollHandler.getDelegate();
    }

    /**
    * Set the tracking protection callback handler.
    * This will replace the current handler.
    * @param delegate An implementation of TrackingProtectionDelegate.
    */
    public void setTrackingProtectionDelegate(TrackingProtectionDelegate delegate) {
        mTrackingProtectionHandler.setDelegate(delegate, this);
    }

    /**
    * Get the tracking protection callback handler.
    * @return The current tracking protection callback handler.
    */
    public TrackingProtectionDelegate getTrackingProtectionDelegate() {
        return mTrackingProtectionHandler.getDelegate();
    }

    /**
     * Set the current prompt delegate for this GeckoSession.
     * @param delegate PromptDelegate instance or null to use the built-in delegate.
     */
    public void setPromptDelegate(PromptDelegate delegate) {
        mPromptDelegate = delegate;
    }

    /**
     * Get the current prompt delegate for this GeckoSession.
     * @return PromptDelegate instance or null if using built-in delegate.
     */
    public PromptDelegate getPromptDelegate() {
        return mPromptDelegate;
    }

    private static class PromptCallback implements
        PromptDelegate.AlertCallback, PromptDelegate.ButtonCallback,
        PromptDelegate.TextCallback, PromptDelegate.AuthCallback,
        PromptDelegate.ChoiceCallback, PromptDelegate.FileCallback {

        private final String mType;
        private final String mMode;
        private final boolean mHasCheckbox;
        private final String mCheckboxMessage;

        private EventCallback mCallback;
        private boolean mCheckboxValue;
        private GeckoBundle mResult;

        public PromptCallback(final String type, final String mode,
                              final GeckoBundle message, final EventCallback callback) {
            mType = type;
            mMode = mode;
            mCallback = callback;
            mHasCheckbox = message.getBoolean("hasCheck");
            mCheckboxMessage = message.getString("checkMsg");
            mCheckboxValue = message.getBoolean("checkValue");
        }

        private GeckoBundle ensureResult() {
            if (mResult == null) {
                // Usually result object contains two items.
                mResult = new GeckoBundle(2);
            }
            return mResult;
        }

        private void submit() {
            if (mHasCheckbox) {
                ensureResult().putBoolean("checkValue", mCheckboxValue);
            }
            if (mCallback != null) {
                mCallback.sendSuccess(mResult);
                mCallback = null;
            }
        }

        @Override // AlertCallbcak
        public void dismiss() {
            // Send a null result.
            mResult = null;
            submit();
        }

        @Override // AlertCallbcak
        public boolean hasCheckbox() {
            return mHasCheckbox;
        }

        @Override // AlertCallbcak
        public String getCheckboxMessage() {
            return mCheckboxMessage;
        }

        @Override // AlertCallbcak
        public boolean getCheckboxValue() {
            return mCheckboxValue;
        }

        @Override // AlertCallbcak
        public void setCheckboxValue(final boolean value) {
            mCheckboxValue = value;
        }

        @Override // ButtonCallback
        public void confirm(final int value) {
            if ("button".equals(mType)) {
                ensureResult().putInt("button", value);
            } else {
                throw new UnsupportedOperationException();
            }
            submit();
        }

        @Override // TextCallback, AuthCallback, ChoiceCallback, FileCallback
        public void confirm(final String value) {
            if ("text".equals(mType) || "color".equals(mType) || "datetime".equals(mType)) {
                ensureResult().putString(mType, value);
            } else if ("auth".equals(mType)) {
                if (!"password".equals(mMode)) {
                    throw new IllegalArgumentException();
                }
                ensureResult().putString("password", value);
            } else if ("choice".equals(mType)) {
                confirm(new String[] { value });
                return;
            } else {
                throw new UnsupportedOperationException();
            }
            submit();
        }

        @Override // AuthCallback
        public void confirm(final String username, final String password) {
            if ("auth".equals(mType)) {
                if (!"auth".equals(mMode)) {
                    throw new IllegalArgumentException();
                }
                ensureResult().putString("username", username);
                ensureResult().putString("password", password);
            } else {
                throw new UnsupportedOperationException();
            }
            submit();
        }

        @Override // ChoiceCallback, FileCallback
        public void confirm(final String[] values) {
            if (("menu".equals(mMode) || "single".equals(mMode)) &&
                (values == null || values.length != 1)) {
                throw new IllegalArgumentException();
            }
            if ("choice".equals(mType)) {
                ensureResult().putStringArray("choices", values);
            } else {
                throw new UnsupportedOperationException();
            }
            submit();
        }

        @Override // ChoiceCallback
        public void confirm(PromptDelegate.Choice item) {
            if ("choice".equals(mType)) {
                confirm(item == null ? null : item.id);
                return;
            } else {
                throw new UnsupportedOperationException();
            }
        }

        @Override // ChoiceCallback
        public void confirm(PromptDelegate.Choice[] items) {
            if (("menu".equals(mMode) || "single".equals(mMode)) &&
                (items == null || items.length != 1)) {
                throw new IllegalArgumentException();
            }
            if ("choice".equals(mType)) {
                if (items == null) {
                    confirm((String[]) null);
                    return;
                }
                final String[] ids = new String[items.length];
                for (int i = 0; i < ids.length; i++) {
                    ids[i] = (items[i] == null) ? null : items[i].id;
                }
                confirm(ids);
                return;
            } else {
                throw new UnsupportedOperationException();
            }
        }

        @Override // FileCallback
        public void confirm(final Context context, final Uri uri) {
            if ("file".equals(mType)) {
                confirm(context, uri == null ? null : new Uri[] { uri });
                return;
            } else {
                throw new UnsupportedOperationException();
            }
        }

        private static String getFile(final Context context, final Uri uri) {
            if (uri == null) {
                return null;
            }
            if ("file".equals(uri.getScheme())) {
                return uri.getPath();
            }
            final ContentResolver cr = context.getContentResolver();
            final Cursor cur = cr.query(uri, new String[] { "_data" }, /* selection */ null,
                                        /* args */ null, /* sort */ null);
            if (cur == null) {
                return null;
            }
            try {
                final int idx = cur.getColumnIndex("_data");
                if (idx < 0 || !cur.moveToFirst()) {
                    return null;
                }
                do {
                    try {
                        final String path = cur.getString(idx);
                        if (path != null && !path.isEmpty()) {
                            return path;
                        }
                    } catch (final Exception e) {
                    }
                } while (cur.moveToNext());
            } finally {
                cur.close();
            }
            return null;
        }

        @Override // FileCallback
        public void confirm(final Context context, final Uri[] uris) {
            if ("single".equals(mMode) && (uris == null || uris.length != 1)) {
                throw new IllegalArgumentException();
            }
            if ("file".equals(mType)) {
                final String[] paths = new String[uris != null ? uris.length : 0];
                for (int i = 0; i < paths.length; i++) {
                    paths[i] = getFile(context, uris[i]);
                    if (paths[i] == null) {
                        Log.e(LOGTAG, "Only file URI is supported: " + uris[i]);
                    }
                }
                ensureResult().putStringArray("files", paths);
            } else {
                throw new UnsupportedOperationException();
            }
            submit();
        }
    }

    /* package */ static void handlePromptEvent(final GeckoSession session,
                                                final GeckoBundle message,
                                                final EventCallback callback) {
        final PromptDelegate delegate = session.getPromptDelegate();
        if (delegate == null) {
            // Default behavior is same as calling dismiss() on callback.
            callback.sendSuccess(null);
            return;
        }

        final String type = message.getString("type");
        final String mode = message.getString("mode");
        final PromptCallback cb = new PromptCallback(type, mode, message, callback);
        final String title = message.getString("title");
        final String msg = message.getString("msg");
        switch (type) {
            case "alert": {
                delegate.onAlert(session, title, msg, cb);
                break;
            }
            case "button": {
                final String[] btnTitle = message.getStringArray("btnTitle");
                final String[] btnCustomTitle = message.getStringArray("btnCustomTitle");
                for (int i = 0; i < btnCustomTitle.length; i++) {
                    final int resId;
                    if ("ok".equals(btnTitle[i])) {
                        resId = android.R.string.ok;
                    } else if ("cancel".equals(btnTitle[i])) {
                        resId = android.R.string.cancel;
                    } else if ("yes".equals(btnTitle[i])) {
                        resId = android.R.string.yes;
                    } else if ("no".equals(btnTitle[i])) {
                        resId = android.R.string.no;
                    } else {
                        continue;
                    }
                    btnCustomTitle[i] = Resources.getSystem().getString(resId);
                }
                delegate.onButtonPrompt(session, title, msg, btnCustomTitle, cb);
                break;
            }
            case "text": {
                delegate.onTextPrompt(session, title, msg, message.getString("value"), cb);
                break;
            }
            case "auth": {
                delegate.onAuthPrompt(session, title, msg, new PromptDelegate.AuthOptions(message.getBundle("options")), cb);
                break;
            }
            case "choice": {
                final int intMode;
                if ("menu".equals(mode)) {
                    intMode = PromptDelegate.Choice.CHOICE_TYPE_MENU;
                } else if ("single".equals(mode)) {
                    intMode = PromptDelegate.Choice.CHOICE_TYPE_SINGLE;
                } else if ("multiple".equals(mode)) {
                    intMode = PromptDelegate.Choice.CHOICE_TYPE_MULTIPLE;
                } else {
                    callback.sendError("Invalid mode");
                    return;
                }

                GeckoBundle[] choiceBundles = message.getBundleArray("choices");
                PromptDelegate.Choice choices[];
                if (choiceBundles == null || choiceBundles.length == 0) {
                    choices = null;
                } else {
                    choices = new PromptDelegate.Choice[choiceBundles.length];
                    for (int i = 0; i < choiceBundles.length; i++) {
                        choices[i] = new PromptDelegate.Choice(choiceBundles[i]);
                    }
                }
                delegate.onChoicePrompt(session, title, msg, intMode,
                                         choices, cb);
                break;
            }
            case "color": {
                delegate.onColorPrompt(session, title, message.getString("value"), cb);
                break;
            }
            case "datetime": {
                final int intMode;
                if ("date".equals(mode)) {
                    intMode = PromptDelegate.DATETIME_TYPE_DATE;
                } else if ("month".equals(mode)) {
                    intMode = PromptDelegate.DATETIME_TYPE_MONTH;
                } else if ("week".equals(mode)) {
                    intMode = PromptDelegate.DATETIME_TYPE_WEEK;
                } else if ("time".equals(mode)) {
                    intMode = PromptDelegate.DATETIME_TYPE_TIME;
                } else if ("datetime-local".equals(mode)) {
                    intMode = PromptDelegate.DATETIME_TYPE_DATETIME_LOCAL;
                } else {
                    callback.sendError("Invalid mode");
                    return;
                }
                delegate.onDateTimePrompt(session, title, intMode,
                                           message.getString("value"),
                                           message.getString("min"),
                                           message.getString("max"), cb);
                break;
            }
            case "file": {
                final int intMode;
                if ("single".equals(mode)) {
                    intMode = PromptDelegate.FILE_TYPE_SINGLE;
                } else if ("multiple".equals(mode)) {
                    intMode = PromptDelegate.FILE_TYPE_MULTIPLE;
                } else {
                    callback.sendError("Invalid mode");
                    return;
                }
                String[] mimeTypes = message.getStringArray("mimeTypes");
                final String[] extensions = message.getStringArray("extension");
                if (extensions != null) {
                    final ArrayList<String> combined =
                            new ArrayList<>(mimeTypes.length + extensions.length);
                    combined.addAll(Arrays.asList(mimeTypes));
                    for (final String extension : extensions) {
                        final String mimeType =
                                URLConnection.guessContentTypeFromName(extension);
                        if (mimeType != null) {
                            combined.add(mimeType);
                        }
                    }
                    mimeTypes = combined.toArray(new String[combined.size()]);
                }
                delegate.onFilePrompt(session, title, intMode, mimeTypes, cb);
                break;
            }
            default: {
                callback.sendError("Invalid type");
                break;
            }
        }
    }

    public EventDispatcher getEventDispatcher() {
        return mEventDispatcher;
    }

    public interface ProgressDelegate {
        /**
         * Class representing security information for a site.
         */
        public class SecurityInformation {
            public static final int SECURITY_MODE_UNKNOWN = 0;
            public static final int SECURITY_MODE_IDENTIFIED = 1;
            public static final int SECURITY_MODE_VERIFIED = 2;

            public static final int CONTENT_UNKNOWN = 0;
            public static final int CONTENT_BLOCKED = 1;
            public static final int CONTENT_LOADED = 2;
            /**
             * Indicates whether or not the site is secure.
             */
            public final boolean isSecure;
            /**
             * Indicates whether or not the site is a security exception.
             */
            public final boolean isException;
            /**
             * Contains the origin of the certificate.
             */
            public final String origin;
            /**
             * Contains the host associated with the certificate.
             */
            public final String host;
            /**
             * Contains the human-readable name of the certificate subject.
             */
            public final String organization;
            /**
             * Contains the full name of the certificate subject, including location.
             */
            public final String subjectName;
            /**
             * Contains the common name of the issuing authority.
             */
            public final String issuerCommonName;
            /**
             * Contains the full/proper name of the issuing authority.
             */
            public final String issuerOrganization;
            /**
             * Indicates the security level of the site; possible values are SECURITY_MODE_UNKNOWN,
             * SECURITY_MODE_IDENTIFIED, and SECURITY_MODE_VERIFIED. SECURITY_MODE_IDENTIFIED
             * indicates domain validation only, while SECURITY_MODE_VERIFIED indicates extended validation.
             */
            public final int securityMode;
            /**
             * Indicates the presence of passive mixed content; possible values are
             * CONTENT_UNKNOWN, CONTENT_BLOCKED, and CONTENT_LOADED.
             */
            public final int mixedModePassive;
            /**
             * Indicates the presence of active mixed content; possible values are
             * CONTENT_UNKNOWN, CONTENT_BLOCKED, and CONTENT_LOADED.
             */
            public final int mixedModeActive;
            /**
             * Indicates the status of tracking protection; possible values are
             * CONTENT_UNKNOWN, CONTENT_BLOCKED, and CONTENT_LOADED.
             */
            public final int trackingMode;

            /* package */ SecurityInformation(GeckoBundle identityData) {
                final GeckoBundle mode = identityData.getBundle("mode");

                mixedModePassive = mode.getInt("mixed_display");
                mixedModeActive = mode.getInt("mixed_active");
                trackingMode = mode.getInt("tracking");

                securityMode = mode.getInt("identity");

                isSecure = identityData.getBoolean("secure");
                isException = identityData.getBoolean("securityException");
                origin = identityData.getString("origin");
                host = identityData.getString("host");
                organization = identityData.getString("organization");
                subjectName = identityData.getString("subjectName");
                issuerCommonName = identityData.getString("issuerCommonName");
                issuerOrganization = identityData.getString("issuerOrganization");
            }
        }

        /**
        * A View has started loading content from the network.
        * @param session GeckoSession that initiated the callback.
        * @param url The resource being loaded.
        */
        void onPageStart(GeckoSession session, String url);

        /**
        * A View has finished loading content from the network.
        * @param session GeckoSession that initiated the callback.
        * @param success Whether the page loaded successfully or an error occurred.
        */
        void onPageStop(GeckoSession session, boolean success);

        /**
        * The security status has been updated.
        * @param session GeckoSession that initiated the callback.
        * @param securityInfo The new security information.
        */
        void onSecurityChange(GeckoSession session, SecurityInformation securityInfo);
    }

    public interface ContentDelegate {
        /**
        * A page title was discovered in the content or updated after the content
        * loaded.
        * @param session The GeckoSession that initiated the callback.
        * @param title The title sent from the content.
        */
        void onTitleChange(GeckoSession session, String title);

        /**
        * A page has requested focus. Note that window.focus() in content will not result
        * in this being called.
        * @param session The GeckoSession that initiated the callback.
        */
        void onFocusRequest(GeckoSession session);

        /**
        * A page has requested to close
        * @param session The GeckoSession that initiated the callback.
        */
        void onCloseRequest(GeckoSession session);

        /**
         * A page has entered or exited full screen mode. Typically, the implementation
         * would set the Activity containing the GeckoSession to full screen when the page is
         * in full screen mode.
         *
         * @param session The GeckoSession that initiated the callback.
         * @param fullScreen True if the page is in full screen mode.
         */
        void onFullScreen(GeckoSession session, boolean fullScreen);


        /**
         * A user has initiated the context menu via long-press.
         * This event is fired on links, (nested) images and (nested) media
         * elements.
         *
         * @param session The GeckoSession that initiated the callback.
         * @param screenX The screen coordinates of the press.
         * @param screenY The screen coordinates of the press.
         * @param uri The URI of the pressed link, set for links and
         *            image-links.
         * @param elementSrc The source URI of the pressed element, set for
         *                   (nested) images and media elements.
         */
        void onContextMenu(GeckoSession session, int screenX, int screenY,
                           String uri, String elementSrc);
    }

    /**
     * This is used to send responses in delegate methods that have asynchronous responses.
     */
    public interface Response<T> {
        /**
         * @param val The value contained in the response
         */
        void respond(T val);
    }

    public interface NavigationDelegate {
        /**
        * A view has started loading content from the network.
        * @param session The GeckoSession that initiated the callback.
        * @param url The resource being loaded.
        */
        void onLocationChange(GeckoSession session, String url);

        /**
        * The view's ability to go back has changed.
        * @param session The GeckoSession that initiated the callback.
        * @param canGoBack The new value for the ability.
        */
        void onCanGoBack(GeckoSession session, boolean canGoBack);

        /**
        * The view's ability to go forward has changed.
        * @param session The GeckoSession that initiated the callback.
        * @param canGoForward The new value for the ability.
        */
        void onCanGoForward(GeckoSession session, boolean canGoForward);

        public static final int TARGET_WINDOW_NONE = 0;
        public static final int TARGET_WINDOW_CURRENT = 1;
        public static final int TARGET_WINDOW_NEW = 2;

        /**
         * A request to open an URI.
         * @param session The GeckoSession that initiated the callback.
         * @param uri The URI to be loaded.
         * @param target The target where the window has requested to open. One of
         *               TARGET_WINDOW_*.
         *
         * @return Whether or not the load was handled. Returning false will allow Gecko
         *         to continue the load as normal.
         */
        boolean onLoadRequest(GeckoSession session, String uri, int target);

        /**
        * A request has been made to open a new session. The URI is provided only for
        * informational purposes. Do not call GeckoSession.loadUri() here. Additionally, the
        * returned GeckoSession must be a newly-created one.
        *
        * @param session The GeckoSession that initiated the callback.
        * @param uri The URI to be loaded.
        *
        * @param response A Response which will hold the returned GeckoSession
        */
        void onNewSession(GeckoSession session, String uri, Response<GeckoSession> response);
    }

    /**
     * GeckoSession applications implement this interface to handle prompts triggered by
     * content in the GeckoSession, such as alerts, authentication dialogs, and select list
     * pickers.
     **/
    public interface PromptDelegate {
        /**
         * Callback interface for notifying the result of a prompt, and for accessing the
         * optional features for prompts (e.g. optional checkbox).
         */
        interface AlertCallback {
            /**
             * Called by the prompt implementation when the prompt is dismissed without a
             * result, for example if the user presses the "Back" button. All prompts
             * must call dismiss() or confirm(), if available, when the prompt is dismissed.
             */
            void dismiss();

            /**
             * Return whether the prompt shown should include a checkbox. For example, if
             * a page shows multiple prompts within a short period of time, the next
             * prompt will include a checkbox to let the user disable future prompts.
             * Although the API allows checkboxes for all prompts, in practice, only
             * alert/button/text/auth prompts will possibly have a checkbox.
             *
             * @return True if prompt includes a checkbox.
             */
            boolean hasCheckbox();

            /**
             * Return the message label for the optional checkbox.
             *
             * @return Checkbox message or null if none.
             */
            String getCheckboxMessage();

            /**
             * Return the initial value for the optional checkbox.
             *
             * @return Initial checkbox value.
             */
            boolean getCheckboxValue();

            /**
             * Set the current value for the optional checkbox.
             *
             * @param value New checkbox value.
             */
            void setCheckboxValue(boolean value);
        }

        /**
         * Display a simple message prompt.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog.
         * @param msg Message for the prompt dialog.
         * @param callback Callback interface.
         */
        void onAlert(GeckoSession session, String title, String msg, AlertCallback callback);

        /**
         * Callback interface for notifying the result of a button prompt.
         */
        interface ButtonCallback extends AlertCallback {
            /**
             * Called by the prompt implementation when the button prompt is dismissed by
             * the user pressing one of the buttons.
             *
             * @param button Button result; one of BUTTON_TYPE_* constants.
             */
            void confirm(int button);
        }

        static final int BUTTON_TYPE_POSITIVE = 0;
        static final int BUTTON_TYPE_NEUTRAL = 1;
        static final int BUTTON_TYPE_NEGATIVE = 2;

        /**
         * Display a prompt with up to three buttons.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog.
         * @param msg Message for the prompt dialog.
         * @param btnMsg Array of 3 elements indicating labels for the individual buttons.
         *               btnMsg[BUTTON_TYPE_POSITIVE] is the label for the "positive" button.
         *               btnMsg[BUTTON_TYPE_NEUTRAL] is the label for the "neutral" button.
         *               btnMsg[BUTTON_TYPE_NEGATIVE] is the label for the "negative" button.
         *               The button is hidden if the corresponding label is null.
         * @param callback Callback interface.
         */
        void onButtonPrompt(GeckoSession session, String title, String msg,
                             String[] btnMsg, ButtonCallback callback);

        /**
         * Callback interface for notifying the result of prompts that have text results,
         * including color and date/time pickers.
         */
        interface TextCallback extends AlertCallback {
            /**
             * Called by the prompt implementation when the text prompt is confirmed by
             * the user, for example by pressing the "OK" button.
             *
             * @param text Text result.
             */
            void confirm(String text);
        }

        /**
         * Display a prompt for inputting text.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog.
         * @param msg Message for the prompt dialog.
         * @param value Default input text for the prompt.
         * @param callback Callback interface.
         */
        void onTextPrompt(GeckoSession session, String title, String msg,
                           String value, TextCallback callback);

        /**
         * Callback interface for notifying the result of authentication prompts.
         */
        interface AuthCallback extends AlertCallback {
            /**
             * Called by the prompt implementation when a password-only prompt is
             * confirmed by the user.
             *
             * @param password Entered password.
             */
            void confirm(String password);

            /**
             * Called by the prompt implementation when a username/password prompt is
             * confirmed by the user.
             *
             * @param username Entered username.
             * @param password Entered password.
             */
            void confirm(String username, String password);
        }

        class AuthOptions {
            /**
             * The auth prompt is for a network host.
             */
            public static final int AUTH_FLAG_HOST = 1;
            /**
             * The auth prompt is for a proxy.
             */
            public static final int AUTH_FLAG_PROXY = 2;
            /**
             * The auth prompt should only request a password.
             */
            public static final int AUTH_FLAG_ONLY_PASSWORD = 8;
            /**
             * The auth prompt is the result of a previous failed login.
             */
            public static final int AUTH_FLAG_PREVIOUS_FAILED = 16;
            /**
             * The auth prompt is for a cross-origin sub-resource.
             */
            public static final int AUTH_FLAG_CROSS_ORIGIN_SUB_RESOURCE = 32;

            /**
             * The auth request is unencrypted or the encryption status is unknown.
             */
            public static final int AUTH_LEVEL_NONE = 0;
            /**
             * The auth request only encrypts password but not data.
             */
            public static final int AUTH_LEVEL_PW_ENCRYPTED = 1;
            /**
             * The auth request encrypts both password and data.
             */
            public static final int AUTH_LEVEL_SECURE = 2;

            /**
             * An int bit-field of AUTH_FLAG_* flags.
             */
            public int flags;

            /**
             * A string containing the URI for the auth request or null if unknown.
             */
            public String uri;

            /**
             * An int, one of AUTH_LEVEL_*, indicating level of encryption.
             */
            public int level;

            /**
             * A string containing the initial username or null if password-only.
             */
            public String username;

            /**
             * A string containing the initial password.
             */
            public String password;

            /* package */ AuthOptions(GeckoBundle options) {
                flags = options.getInt("flags");
                uri = options.getString("uri");
                level = options.getInt("level");
                username = options.getString("username");
                password = options.getString("password");
            }
        }

        /**
         * Display a prompt for authentication credentials.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog.
         * @param msg Message for the prompt dialog.
         * @param options AuthOptions containing options for the prompt
         * @param callback Callback interface.
         */
        void onAuthPrompt(GeckoSession session, String title, String msg,
                           AuthOptions options, AuthCallback callback);

        class Choice {
            /**
             * Display choices in a menu that dismisses as soon as an item is chosen.
             */
            public static final int CHOICE_TYPE_MENU = 1;

            /**
             * Display choices in a list that allows a single selection.
             */
            public static final int CHOICE_TYPE_SINGLE = 2;

            /**
             * Display choices in a list that allows multiple selections.
             */
            public static final int CHOICE_TYPE_MULTIPLE = 3;

            /**
             * A boolean indicating if the item is disabled. Item should not be
             * selectable if this is true.
             */
            public final boolean disabled;

            /**
             * A String giving the URI of the item icon, or null if none exists
             * (only valid for menus)
             */
            public final String icon;

            /**
             * A String giving the ID of the item or group
             */
            public final String id;

            /**
             * A Choice array of sub-items in a group, or null if not a group
             */
            public final Choice[] items;

            /**
             * A string giving the label for displaying the item or group
             */
            public final String label;

            /**
             * A boolean indicating if the item should be pre-selected
             * (pre-checked for menu items)
             */
            public final boolean selected;

            /**
             * A boolean indicating if the item should be a menu separator
             * (only valid for menus)
             */
            public final boolean separator;

            /* package */ Choice(GeckoBundle choice) {
                disabled = choice.getBoolean("disabled");
                icon = choice.getString("icon");
                id = choice.getString("id");
                label = choice.getString("label");
                selected = choice.getBoolean("selected");
                separator = choice.getBoolean("separator");

                GeckoBundle[] choices = choice.getBundleArray("items");
                if (choices == null) {
                    items = null;
                } else {
                    items = new Choice[choices.length];
                    for (int i = 0; i < choices.length; i++) {
                        items[i] = new Choice(choices[i]);
                    }
                }
            }
        }

        /**
         * Callback interface for notifying the result of menu or list choice.
         */
        interface ChoiceCallback extends AlertCallback {
            /**
             * Called by the prompt implementation when the menu or single-choice list is
             * dismissed by the user.
             *
             * @param id ID of the selected item.
             */
            void confirm(String id);

            /**
             * Called by the prompt implementation when the multiple-choice list is
             * dismissed by the user.
             *
             * @param ids IDs of the selected items.
             */
            void confirm(String[] ids);

            /**
             * Called by the prompt implementation when the menu or single-choice list is
             * dismissed by the user.
             *
             * @param item Choice representing the selected item; must be an original
             *             Choice object that was passed to the implementation.
             */
            void confirm(Choice item);

            /**
             * Called by the prompt implementation when the multiple-choice list is
             * dismissed by the user.
             *
             * @param items Choice array representing the selected items; must be original
             *              Choice objects that were passed to the implementation.
             */
            void confirm(Choice[] items);
        }


        /**
         * Display a menu prompt or list prompt.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog, or null for no title.
         * @param msg Message for the prompt dialog, or null for no message.
         * @param type One of CHOICE_TYPE_* indicating the type of prompt.
         * @param choices Array of Choices each representing an item or group.
         * @param callback Callback interface.
         */
        void onChoicePrompt(GeckoSession session, String title, String msg, int type,
                             Choice[] choices, ChoiceCallback callback);

        /**
         * Display a color prompt.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog.
         * @param value Initial color value in HTML color format.
         * @param callback Callback interface; the result passed to confirm() must be in
         *                 HTML color format.
         */
        void onColorPrompt(GeckoSession session, String title, String value,
                            TextCallback callback);

        /**
         * Prompt for year, month, and day.
         */
        static final int DATETIME_TYPE_DATE = 1;

        /**
         * Prompt for year and month.
         */
        static final int DATETIME_TYPE_MONTH = 2;

        /**
         * Prompt for year and week.
         */
        static final int DATETIME_TYPE_WEEK = 3;

        /**
         * Prompt for hour and minute.
         */
        static final int DATETIME_TYPE_TIME = 4;

        /**
         * Prompt for year, month, day, hour, and minute, without timezone.
         */
        static final int DATETIME_TYPE_DATETIME_LOCAL = 5;

        /**
         * Display a date/time prompt.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog; currently always null.
         * @param type One of DATETIME_TYPE_* indicating the type of prompt.
         * @param value Initial date/time value in HTML date/time format.
         * @param min Minimum date/time value in HTML date/time format.
         * @param max Maximum date/time value in HTML date/time format.
         * @param callback Callback interface; the result passed to confirm() must be in
         *                 HTML date/time format.
         */
        void onDateTimePrompt(GeckoSession session, String title, int type,
                               String value, String min, String max, TextCallback callback);

        /**
         * Callback interface for notifying the result of file prompts.
         */
        interface FileCallback extends AlertCallback {
            /**
             * Called by the prompt implementation when the user makes a file selection in
             * single-selection mode.
             *
             * @param context An application Context for parsing URIs.
             * @param uri The URI of the selected file.
             */
            void confirm(Context context, Uri uri);

            /**
             * Called by the prompt implementation when the user makes file selections in
             * multiple-selection mode.
             *
             * @param context An application Context for parsing URIs.
             * @param uris Array of URI objects for the selected files.
             */
            void confirm(Context context, Uri[] uris);
        }

        static final int FILE_TYPE_SINGLE = 1;
        static final int FILE_TYPE_MULTIPLE = 2;

        /**
         * Display a file prompt.
         *
         * @param session GeckoSession that triggered the prompt
         * @param title Title for the prompt dialog.
         * @param type One of FILE_TYPE_* indicating the prompt type.
         * @param mimeTypes Array of permissible MIME types for the selected files, in
         *                  the form "type/subtype", where "type" and/or "subtype" can be
         *                  "*" to indicate any value.
         * @param callback Callback interface.
         */
        void onFilePrompt(GeckoSession session, String title, int type,
                           String[] mimeTypes, FileCallback callback);
    }

    /**
     * GeckoSession applications implement this interface to handle content scroll
     * events.
     **/
    public interface ScrollDelegate {
        /**
         * The scroll position of the content has changed.
         *
        * @param session GeckoSession that initiated the callback.
        * @param scrollX The new horizontal scroll position in pixels.
        * @param scrollY The new vertical scroll position in pixels.
        */
        public void onScrollChanged(GeckoSession session, int scrollX, int scrollY);
    }

    private final TrackingProtection mTrackingProtection = new TrackingProtection(this);

    /**
     * GeckoSession applications implement this interface to handle tracking
     * protection events.
     **/
    public interface TrackingProtectionDelegate {
        static final int CATEGORY_AD = 1 << 0;
        static final int CATEGORY_ANALYTIC = 1 << 1;
        static final int CATEGORY_SOCIAL = 1 << 2;
        static final int CATEGORY_CONTENT = 1 << 3;

        /**
         * A tracking element has been blocked from loading.
         *
        * @param session The GeckoSession that initiated the callback.
        * @param uri The URI of the blocked element.
        * @param categories The tracker categories of the blocked element.
        *                   One or more of the {@link TrackingProtectionDelegate#CATEGORY_AD}
        *                   flags.
        */
        void onTrackerBlocked(GeckoSession session, String uri, int categories);
    }

    /**
     * Enable tracking protection.
     * @param categories The categories of trackers that should be blocked.
     *                   Use one or more of the {@link TrackingProtectionDelegate#CATEGORY_AD}
     *                   flags.
     **/
    public void enableTrackingProtection(int categories) {
        mTrackingProtection.enable(categories);
    }

    /**
     * Disable tracking protection.
     **/
    public void disableTrackingProtection() {
        mTrackingProtection.disable();
    }

    /**
     * GeckoSession applications implement this interface to handle requests for permissions
     * from content, such as geolocation and notifications. For each permission, usually
     * two requests are generated: one request for the Android app permission through
     * requestAppPermissions, which is typically handled by a system permission dialog;
     * and another request for the content permission (e.g. through
     * requestContentPermission), which is typically handled by an app-specific
     * permission dialog.
     **/
    public interface PermissionDelegate {
        /**
         * Permission for using the geolocation API.
         * See: https://developer.mozilla.org/en-US/docs/Web/API/Geolocation
         */
        public static final int PERMISSION_GEOLOCATION = 0;

        /**
         * Permission for using the notifications API.
         * See: https://developer.mozilla.org/en-US/docs/Web/API/notification
         */
        public static final int PERMISSION_DESKTOP_NOTIFICATION = 1;

        /**
         * Callback interface for notifying the result of a permission request.
         */
        interface Callback {
            /**
             * Called by the implementation after permissions are granted; the
             * implementation must call either grant() or reject() for every request.
             */
            void grant();

            /**
             * Called by the implementation when permissions are not granted; the
             * implementation must call either grant() or reject() for every request.
             */
            void reject();
        }

        /**
         * Request Android app permissions.
         *
         * @param session GeckoSession instance requesting the permissions.
         * @param permissions List of permissions to request; possible values are,
         *                    android.Manifest.permission.ACCESS_COARSE_LOCATION
         *                    android.Manifest.permission.ACCESS_FINE_LOCATION
         *                    android.Manifest.permission.CAMERA
         *                    android.Manifest.permission.RECORD_AUDIO
         * @param callback Callback interface.
         */
        void onAndroidPermissionsRequest(GeckoSession session, String[] permissions,
                                       Callback callback);

        /**
         * Request content permission.
         *
         * @param session GeckoSession instance requesting the permission.
         * @param uri The URI of the content requesting the permission.
         * @param type The type of the requested permission; possible values are,
         *             PERMISSION_GEOLOCATION
         *             PERMISSION_DESKTOP_NOTIFICATION
         * @param access Not used.
         * @param callback Callback interface.
         */
        void onContentPermissionRequest(GeckoSession session, String uri, int type,
                                      String access, Callback callback);

        class MediaSource {
            /**
             * The media source is a camera.
             */
            public static final int SOURCE_CAMERA = 0;

            /**
             * The media source is the screen.
             */
            public static final int SOURCE_SCREEN  = 1;

            /**
             * The media source is an application.
             */
            public static final int SOURCE_APPLICATION = 2;

            /**
             * The media source is a window.
             */
            public static final int SOURCE_WINDOW = 3;

            /**
             * The media source is the browser.
             */
            public static final int SOURCE_BROWSER = 4;

            /**
             * The media source is a microphone.
             */
            public static final int SOURCE_MICROPHONE = 5;

            /**
             * The media source is audio capture.
             */
            public static final int SOURCE_AUDIOCAPTURE = 6;

            /**
             * The media source does not fall into any of the other categories.
             */
            public static final int SOURCE_OTHER = 7;

            /**
             * The media type is video.
             */
            public static final int TYPE_VIDEO = 0;

            /**
             * The media type is audio.
             */
            public static final int TYPE_AUDIO = 1;

            /**
             * A string giving the origin-specific source identifier.
             */
            public final String id;

            /**
             * A string giving the non-origin-specific source identifier.
             */
            public final String rawId;

            /**
             * A string giving the name of the video source from the system
             * (for example, "Camera 0, Facing back, Orientation 90").
             * May be empty.
             */
            public final String name;

            /**
             * An int giving the media source type.
             * Possible values for a video source are:
             * SOURCE_CAMERA, SOURCE_SCREEN, SOURCE_APPLICATION, SOURCE_WINDOW, SOURCE_BROWSER, and SOURCE_OTHER.
             * Possible values for an audio source are:
             * SOURCE_MICROPHONE, SOURCE_AUDIOCAPTURE, and SOURCE_OTHER.
             */
            public final int source;

            /**
             * An int giving the type of media, must be either TYPE_VIDEO or TYPE_AUDIO.
             */
            public final int type;

            private static int getSourceFromString(String src) {
                // The strings here should match those in MediaSourceEnum in MediaStreamTrack.webidl
                if ("camera".equals(src)) {
                    return SOURCE_CAMERA;
                } else if ("screen".equals(src)) {
                    return SOURCE_SCREEN;
                } else if ("application".equals(src)) {
                    return SOURCE_APPLICATION;
                } else if ("window".equals(src)) {
                    return SOURCE_WINDOW;
                } else if ("browser".equals(src)) {
                    return SOURCE_BROWSER;
                } else if ("microphone".equals(src)) {
                    return SOURCE_MICROPHONE;
                } else if ("audioCapture".equals(src)) {
                    return SOURCE_AUDIOCAPTURE;
                } else if ("other".equals(src)) {
                    return SOURCE_OTHER;
                } else {
                    throw new IllegalArgumentException("String: " + src + " is not a valid media source string");
                }
            }

            private static int getTypeFromString(String type) {
                // The strings here should match the possible types in MediaDevice::MediaDevice in MediaManager.cpp
                if ("video".equals(type)) {
                    return TYPE_VIDEO;
                } else if ("audio".equals(type)) {
                    return TYPE_AUDIO;
                } else {
                    throw new IllegalArgumentException("String: " + type + " is not a valid media type string");
                }
            }

            /* package */ MediaSource(GeckoBundle media) {
                id = media.getString("id");
                rawId = media.getString("rawId");
                name = media.getString("name");
                source = getSourceFromString(media.getString("source"));
                type = getTypeFromString(media.getString("type"));
            }
        }

        /**
         * Callback interface for notifying the result of a media permission request,
         * including which media source(s) to use.
         */
        interface MediaCallback {
            /**
             * Called by the implementation after permissions are granted; the
             * implementation must call one of grant() or reject() for every request.
             *
             * @param video "id" value from the bundle for the video source to use,
             *              or null when video is not requested.
             * @param audio "id" value from the bundle for the audio source to use,
             *              or null when audio is not requested.
             */
            void grant(final String video, final String audio);

            /**
             * Called by the implementation after permissions are granted; the
             * implementation must call one of grant() or reject() for every request.
             *
             * @param video MediaSource for the video source to use (must be an original
             *              MediaSource object that was passed to the implementation);
             *              or null when video is not requested.
             * @param audio MediaSource for the audio source to use (must be an original
             *              MediaSource object that was passed to the implementation);
             *              or null when audio is not requested.
             */
            void grant(final MediaSource video, final MediaSource audio);

            /**
             * Called by the implementation when permissions are not granted; the
             * implementation must call one of grant() or reject() for every request.
             */
            void reject();
        }

        /**
         * Request content media permissions, including request for which video and/or
         * audio source to use.
         *
         * @param session GeckoSession instance requesting the permission.
         * @param uri The URI of the content requesting the permission.
         * @param video List of video sources, or null if not requesting video.
         * @param audio List of audio sources, or null if not requesting audio.
         * @param callback Callback interface.
         */
        void onMediaPermissionRequest(GeckoSession session, String uri, MediaSource[] video,
                                    MediaSource[] audio, MediaCallback callback);
    }
}
