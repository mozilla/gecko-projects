/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.gecko.home.activitystream;

import android.database.Cursor;
import android.support.v7.widget.RecyclerView;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import org.mozilla.gecko.db.BrowserContract;
import org.mozilla.gecko.home.HomePager;
import org.mozilla.gecko.home.activitystream.StreamItem.BottomPanel;
import org.mozilla.gecko.home.activitystream.StreamItem.HighlightItem;
import org.mozilla.gecko.home.activitystream.StreamItem.TopPanel;
import org.mozilla.gecko.widget.RecyclerViewClickSupport;

import java.util.EnumSet;

public class StreamRecyclerAdapter extends RecyclerView.Adapter<StreamItem> implements RecyclerViewClickSupport.OnItemClickListener {
    private Cursor highlightsCursor;
    private Cursor topSitesCursor;

    private HomePager.OnUrlOpenListener onUrlOpenListener;

    void setOnUrlOpenListener(HomePager.OnUrlOpenListener onUrlOpenListener) {
        this.onUrlOpenListener = onUrlOpenListener;
    }

    @Override
    public int getItemViewType(int position) {
        if (position == 0) {
            return TopPanel.LAYOUT_ID;
        } else if (position == getItemCount() - 1) {
            return BottomPanel.LAYOUT_ID;
        } else {
            return HighlightItem.LAYOUT_ID;
        }
    }

    @Override
    public StreamItem onCreateViewHolder(ViewGroup parent, final int type) {
        final LayoutInflater inflater = LayoutInflater.from(parent.getContext());

        if (type == TopPanel.LAYOUT_ID) {
            return new TopPanel(inflater.inflate(type, parent, false), onUrlOpenListener);
        } else if (type == BottomPanel.LAYOUT_ID) {
                return new BottomPanel(inflater.inflate(type, parent, false));
        } else if (type == HighlightItem.LAYOUT_ID) {
            return new HighlightItem(inflater.inflate(type, parent, false));
        } else {
            throw new IllegalStateException("Missing inflation for ViewType " + type);
        }
    }

    private int translatePositionToCursor(int position) {
        if (position == 0 ||
            position == getItemCount() - 1) {
            throw new IllegalArgumentException("Requested cursor position for invalid item");
        }

        // We have one blank panel at the top, hence remove that to obtain the cursor position
        return position - 1;
    }

    @Override
    public void onBindViewHolder(StreamItem holder, int position) {
        int type = getItemViewType(position);

        if (type == HighlightItem.LAYOUT_ID) {
            final int cursorPosition = translatePositionToCursor(position);

            highlightsCursor.moveToPosition(cursorPosition);
            holder.bind(highlightsCursor);
        } else if (type == TopPanel.LAYOUT_ID) {
            holder.bind(topSitesCursor);
        }
    }

    @Override
    public void onItemClicked(RecyclerView recyclerView, int position, View v) {
        if (position < 1) {
            // The header contains top sites and has its own click handling.
            return;
        }

        highlightsCursor.moveToPosition(
                translatePositionToCursor(position));

        final String url = highlightsCursor.getString(
                highlightsCursor.getColumnIndexOrThrow(BrowserContract.Combined.URL));

        onUrlOpenListener.onUrlOpen(url, EnumSet.of(HomePager.OnUrlOpenListener.Flags.ALLOW_SWITCH_TO_TAB));
    }

    @Override
    public int getItemCount() {
        final int highlightsCount;
        if (highlightsCursor != null) {
            highlightsCount = highlightsCursor.getCount();
        } else {
            highlightsCount = 0;
        }

        return 2 + highlightsCount;
    }

    public void swapHighlightsCursor(Cursor cursor) {
        highlightsCursor = cursor;

        notifyDataSetChanged();
    }

    public void swapTopSitesCursor(Cursor cursor) {
        this.topSitesCursor = cursor;

        notifyItemChanged(0);
    }
}
