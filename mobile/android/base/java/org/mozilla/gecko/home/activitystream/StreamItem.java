/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.gecko.home.activitystream;

import android.database.Cursor;
import android.support.v4.view.ViewPager;
import android.support.v7.widget.RecyclerView;
import android.text.format.DateUtils;
import android.view.View;
import android.widget.TextView;

import org.mozilla.gecko.R;
import org.mozilla.gecko.db.BrowserContract;
import org.mozilla.gecko.home.HomePager;
import org.mozilla.gecko.home.activitystream.topsites.CirclePageIndicator;
import org.mozilla.gecko.home.activitystream.topsites.TopSitesPagerAdapter;
import org.mozilla.gecko.icons.IconCallback;
import org.mozilla.gecko.icons.IconResponse;
import org.mozilla.gecko.icons.Icons;
import org.mozilla.gecko.widget.FaviconView;

import java.util.concurrent.Future;

public abstract class StreamItem extends RecyclerView.ViewHolder {
    public StreamItem(View itemView) {
        super(itemView);
    }

    public void bind(Cursor cursor) {
        throw new IllegalStateException("Cannot bind " + this.getClass().getSimpleName());
    }

    public static class TopPanel extends StreamItem {
        public static final int LAYOUT_ID = R.layout.activity_stream_main_toppanel;
        private final ViewPager topSitesPager;

        public TopPanel(View itemView, HomePager.OnUrlOpenListener onUrlOpenListener) {
            super(itemView);

            topSitesPager = (ViewPager) itemView.findViewById(R.id.topsites_pager);
            topSitesPager.setAdapter(new TopSitesPagerAdapter(itemView.getContext(), onUrlOpenListener));

            CirclePageIndicator indicator = (CirclePageIndicator) itemView.findViewById(R.id.topsites_indicator);
            indicator.setViewPager(topSitesPager);
        }

        @Override
        public void bind(Cursor cursor) {
            ((TopSitesPagerAdapter) topSitesPager.getAdapter()).swapCursor(cursor);
        }
    }

    public static class BottomPanel extends StreamItem {
        public static final int LAYOUT_ID = R.layout.activity_stream_main_bottompanel;

        public BottomPanel(View itemView) {
            super(itemView);
        }
    }

    public static class HighlightItem extends StreamItem implements IconCallback {
        public static final int LAYOUT_ID = R.layout.activity_stream_card_history_item;

        final FaviconView vIconView;
        final TextView vLabel;
        final TextView vTimeSince;
        final TextView vSourceView;

        private Future<IconResponse> ongoingIconLoad;

        public HighlightItem(View itemView) {
            super(itemView);
            vLabel = (TextView) itemView.findViewById(R.id.card_history_label);
            vTimeSince = (TextView) itemView.findViewById(R.id.card_history_time_since);
            vIconView = (FaviconView) itemView.findViewById(R.id.icon);
            vSourceView = (TextView) itemView.findViewById(R.id.card_history_source);
        }

        @Override
        public void bind(Cursor cursor) {
            final long time = cursor.getLong(cursor.getColumnIndexOrThrow(BrowserContract.Highlights.DATE));
            final String ago = DateUtils.getRelativeTimeSpanString(time, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS, 0).toString();
            final String url = cursor.getString(cursor.getColumnIndexOrThrow(BrowserContract.Combined.URL));

            vLabel.setText(cursor.getString(cursor.getColumnIndexOrThrow(BrowserContract.History.TITLE)));
            vTimeSince.setText(ago);

            updateSource(cursor);

            if (ongoingIconLoad != null) {
                ongoingIconLoad.cancel(true);
            }

            ongoingIconLoad = Icons.with(itemView.getContext())
                    .pageUrl(url)
                    .skipNetwork()
                    .build()
                    .execute(this);
        }

        private void updateSource(final Cursor cursor) {
            final boolean isBookmark = -1 != cursor.getLong(cursor.getColumnIndexOrThrow(BrowserContract.Combined.BOOKMARK_ID));
            final boolean isHistory = -1 != cursor.getLong(cursor.getColumnIndexOrThrow(BrowserContract.Combined.HISTORY_ID));

            if (isBookmark) {
                vSourceView.setText(R.string.activity_stream_highlight_label_bookmarked);
                vSourceView.setVisibility(View.VISIBLE);
            } else if (isHistory) {
                vSourceView.setText(R.string.activity_stream_highlight_label_visited);
                vSourceView.setVisibility(View.VISIBLE);
            } else {
                vSourceView.setVisibility(View.INVISIBLE);
            }

            vSourceView.setText(vSourceView.getText());
        }

        @Override
        public void onIconResponse(IconResponse response) {
            vIconView.updateImage(response);
        }
    }
}
