/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.tabs;

import org.mozilla.gecko.R;

import android.content.Context;
import android.support.v7.widget.GridLayoutManager;
import android.support.v7.widget.helper.ItemTouchHelper;
import android.util.AttributeSet;

abstract class TabsGridLayout extends TabsLayout {
    TabsGridLayout(Context context, AttributeSet attrs, int spanCount) {
        super(context, attrs, R.layout.tabs_layout_item_view);

        setLayoutManager(new GridLayoutManager(context, spanCount));

        setClipToPadding(false);
        setScrollBarStyle(SCROLLBARS_OUTSIDE_OVERLAY);

        setItemAnimator(new TabsGridLayoutAnimator());

        // A TouchHelper handler for swipe to close.
        final TabsTouchHelperCallback callback = new TabsTouchHelperCallback(this) {
            @Override
            protected float alphaForItemSwipeDx(float dX, int distanceToAlphaMin) {
                return 1f - 2f * Math.abs(dX) / distanceToAlphaMin;
            }
        };
        final ItemTouchHelper touchHelper = new ItemTouchHelper(callback);
        touchHelper.attachToRecyclerView(this);
    }

    @Override
    public void closeAll() {
        autoHidePanel();

        closeAllTabs();
    }

    @Override
    protected boolean addAtIndexRequiresScroll(int index) {
        final GridLayoutManager layoutManager = (GridLayoutManager) getLayoutManager();
        final int spanCount = layoutManager.getSpanCount();
        final int firstVisibleIndex = layoutManager.findFirstVisibleItemPosition();
        // When you add an item at the first visible position to a GridLayoutManager and there's
        // room to scroll, RecyclerView scrolls the new position to anywhere from near the bottom of
        // its row to completely offscreen (for unknown reasons), so we need to scroll to fix that.
        // We also scroll when the item being added is the only item on the final row.
        return index == firstVisibleIndex ||
                (index == getAdapter().getItemCount() - 1 && index % spanCount == 0);
    }
}
