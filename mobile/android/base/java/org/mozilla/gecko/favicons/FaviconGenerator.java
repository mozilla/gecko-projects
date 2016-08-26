/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.favicons;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.net.Uri;
import android.support.annotation.NonNull;
import android.text.TextUtils;
import android.util.Log;
import android.util.TypedValue;

import org.mozilla.gecko.R;
import org.mozilla.gecko.util.ThreadUtils;

import java.util.Arrays;
import java.util.Locale;

/**
 * Generate favicons based on the page URL.
 */
public class FaviconGenerator {
    // Mozilla's Visual Design Colour Palette
    // http://firefoxux.github.io/StyleGuide/#/visualDesign/colours
    private static final int[] COLORS = {
            0xFFc33c32,
            0xFFf25820,
            0xFFff9216,
            0xFFffcb00,
            0xFF57bd35,
            0xFF01bdad,
            0xFF0996f8,
            0xFF02538b,
            0xFF1f386e,
            0xFF7a2f7a,
            0xFFea385e,
    };

    // List of common prefixes of host names. Those prefixes will be striped before a prepresentative
    // character for an URL is determined.
    private static final String[] COMMON_PREFIXES = {
            "www.",
            "m.",
            "mobile.",
    };

    private static final int TEXT_SIZE_DP = 12;

    /**
     * Asynchronously generate default favicon for the given page URL.
     */
    public static void generate(final Context context, final String pageURL, final OnFaviconLoadedListener listener) {
        ThreadUtils.postToBackgroundThread(new Runnable() {
            @Override
            public void run() {
                final Bitmap bitmap = generate(context, pageURL);
                ThreadUtils.postToUiThread(new Runnable() {
                    @Override
                    public void run() {
                        listener.onFaviconLoaded(pageURL, null, bitmap);
                    }
                });
            }
        });
    }

    /**
     * Generate default favicon for the given page URL.
     */
    public static Bitmap generate(Context context, String pageURL) {
        final Resources resources = context.getResources();
        final int widthAndHeight = resources.getDimensionPixelSize(R.dimen.favicon_bg);
        final int roundedCorners = resources.getDimensionPixelOffset(R.dimen.favicon_corner_radius);

        final Bitmap favicon = Bitmap.createBitmap(widthAndHeight, widthAndHeight, Bitmap.Config.ARGB_8888);
        final Canvas canvas = new Canvas(favicon);

        final Paint paint = new Paint();
        paint.setColor(pickColor(pageURL));

        canvas.drawRoundRect(new RectF(0, 0, widthAndHeight, widthAndHeight), roundedCorners, roundedCorners, paint);

        paint.setColor(Color.WHITE);

        final String character = getRepresentativeCharacter(pageURL);

        final float textSize = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, TEXT_SIZE_DP, context.getResources().getDisplayMetrics());

        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(textSize);
        paint.setAntiAlias(true);

        canvas.drawText(character,
                canvas.getWidth() / 2,
                (int) ((canvas.getHeight() / 2) - ((paint.descent() + paint.ascent()) / 2)),
                paint);

        return favicon;
    }

    /**
     * Get a representative character for the given URL.
     *
     * For example this method will return "f" for "http://m.facebook.com/foobar".
     */
    protected static String getRepresentativeCharacter(String url) {
        if (TextUtils.isEmpty(url)) {
            return "?";
        }

        final String snippet = getRepresentativeSnippet(url);
        for (int i = 0; i < snippet.length(); i++) {
            char c = snippet.charAt(i);

            if (Character.isLetterOrDigit(c)) {
                return String.valueOf(Character.toUpperCase(c));
            }
        }

        // Nothing found..
        return "?";
    }

    /**
     * Return a color for this URL. Colors will be based on the host. URLs with the same host will
     * return the same color.
     */
    protected static int pickColor(String url) {
        if (TextUtils.isEmpty(url)) {
            return COLORS[0];
        }

        final String snippet = getRepresentativeSnippet(url);
        final int color = Math.abs(snippet.hashCode() % COLORS.length);

        return COLORS[color];
    }

    /**
     * Get the representative part of the URL. Usually this is the host (without common prefixes).
     */
    private static String getRepresentativeSnippet(@NonNull String url) {
        Uri uri = Uri.parse(url);

        // Use the host if available
        String snippet = uri.getHost();

        if (TextUtils.isEmpty(snippet)) {
            // If the uri does not have a host (e.g. file:// uris) then use the path
            snippet = uri.getPath();
        }

        if (TextUtils.isEmpty(snippet)) {
            // If we still have no snippet then just return the question mark
            return "?";
        }

        // Strip common prefixes that we do not want to use to determine the representative character
        for (String prefix : COMMON_PREFIXES) {
            if (snippet.startsWith(prefix)) {
                snippet = snippet.substring(prefix.length());
            }
        }

        return snippet;
    }
}
