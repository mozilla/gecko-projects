/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 package org.mozilla.gecko.home.activitystream;

import android.content.Context;
import android.database.Cursor;
import android.os.Bundle;
import android.support.v4.app.LoaderManager;
import android.support.v4.content.Loader;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.util.AttributeSet;
import android.util.Log;
import android.view.View;
import android.widget.FrameLayout;

import org.mozilla.gecko.GeckoProfile;
import org.mozilla.gecko.R;
import org.mozilla.gecko.db.BrowserDB;
import org.mozilla.gecko.home.HomePager;
import org.mozilla.gecko.home.SimpleCursorLoader;
import org.mozilla.gecko.home.activitystream.topsites.TopSitesPagerAdapter;
import org.mozilla.gecko.widget.RecyclerViewClickSupport;

public class ActivityStream extends FrameLayout {
    private final StreamRecyclerAdapter adapter;

    private static final int LOADER_ID_HIGHLIGHTS = 0;
    private static final int LOADER_ID_TOPSITES = 1;

    public ActivityStream(Context context, AttributeSet attrs) {
        super(context, attrs);

        inflate(context, R.layout.as_content, this);

        adapter = new StreamRecyclerAdapter();

        RecyclerView rv = (RecyclerView) findViewById(R.id.activity_stream_main_recyclerview);

        rv.setAdapter(adapter);
        rv.setLayoutManager(new LinearLayoutManager(getContext()));
        rv.setHasFixedSize(true);

        RecyclerViewClickSupport.addTo(rv)
                .setOnItemClickListener(adapter);
    }

    void setOnUrlOpenListener(HomePager.OnUrlOpenListener listener) {
        adapter.setOnUrlOpenListener(listener);
    }

    public void load(LoaderManager lm) {
        CursorLoaderCallbacks callbacks = new CursorLoaderCallbacks();

        lm.initLoader(LOADER_ID_HIGHLIGHTS, null, callbacks);
        lm.initLoader(LOADER_ID_TOPSITES, null, callbacks);
    }

    public void unload() {
        adapter.swapHighlightsCursor(null);
        adapter.swapTopSitesCursor(null);
    }

    private class CursorLoaderCallbacks implements LoaderManager.LoaderCallbacks<Cursor> {
        @Override
        public Loader<Cursor> onCreateLoader(int id, Bundle args) {
            final Context context = getContext();
            if (id == LOADER_ID_HIGHLIGHTS) {
                return BrowserDB.from(context).getHighlights(context, 10);
            } else if (id == LOADER_ID_TOPSITES) {
                return BrowserDB.from(context).getActivityStreamTopSites(
                        context, TopSitesPagerAdapter.TOTAL_ITEMS);
            } else {
                throw new IllegalArgumentException("Can't handle loader id " + id);
            }
        }

        @Override
        public void onLoadFinished(Loader<Cursor> loader, Cursor data) {
            if (loader.getId() == LOADER_ID_HIGHLIGHTS) {
                adapter.swapHighlightsCursor(data);
            } else if (loader.getId() == LOADER_ID_TOPSITES) {
                adapter.swapTopSitesCursor(data);
            }
        }

        @Override
        public void onLoaderReset(Loader<Cursor> loader) {
            if (loader.getId() == LOADER_ID_HIGHLIGHTS) {
                adapter.swapHighlightsCursor(null);
            } else if (loader.getId() == LOADER_ID_TOPSITES) {
                adapter.swapTopSitesCursor(null);
            }
        }
    }
}
