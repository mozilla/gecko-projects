/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDevToolsAPI

import android.support.test.filters.MediumTest
import android.support.test.InstrumentationRegistry
import android.support.test.runner.AndroidJUnit4

import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityNodeProvider
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityRecord
import android.view.View
import android.view.ViewGroup

import android.widget.FrameLayout

import org.hamcrest.Matchers.*
import org.junit.Test
import org.junit.Before
import org.junit.After
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
@MediumTest
@WithDisplay(width = 480, height = 640)
@WithDevToolsAPI
class AccessibilityTest : BaseSessionTest() {
    lateinit var view: View
    val provider: AccessibilityNodeProvider get() = view.getAccessibilityNodeProvider()

    // Given a child ID, return the virtual descendent ID.
    private fun getVirtualDescendantId(childId: Long): Int {
        try {
            var getVirtualDescendantIdMethod =
                AccessibilityNodeInfo::class.java.getMethod("getVirtualDescendantId", Long::class.java)
            return getVirtualDescendantIdMethod.invoke(null, childId) as Int
        } catch (ex: Exception) {
            return 0
        }
    }

    // Retrieve the virtual descendent ID of the event's source.
    private fun getSourceId(event: AccessibilityEvent): Int {
        try {
            var getSourceIdMethod =
                AccessibilityRecord::class.java.getMethod("getSourceNodeId")
            return getVirtualDescendantId(getSourceIdMethod.invoke(event) as Long)
        } catch (ex: Exception) {
            return 0
        }
    }

    private interface EventDelegate {
        fun onAccessibilityFocused(event: AccessibilityEvent) { }
        fun onFocused(event: AccessibilityEvent) { }
    }

    @Before fun setup() {
        // We initialize a view with a parent and grandparent so that the
        // accessibility events propogate up at least to the parent.
        view = FrameLayout(InstrumentationRegistry.getTargetContext())
        FrameLayout(InstrumentationRegistry.getTargetContext()).addView(view)
        FrameLayout(InstrumentationRegistry.getTargetContext()).addView(view.parent as View)

        // Force on accessibility and assign the session's accessibility
        // object a view.
        sessionRule.setPrefsUntilTestEnd(mapOf("accessibility.force_disabled" to -1))
        mainSession.accessibility.view = view

        // Set up an external delegate that will intercept accessibility events.
        sessionRule.addExternalDelegateUntilTestEnd(
            EventDelegate::class,
        { newDelegate -> (view.parent as View).setAccessibilityDelegate(object : View.AccessibilityDelegate() {
            override fun onRequestSendAccessibilityEvent(host: ViewGroup, child: View, event: AccessibilityEvent): Boolean {
                when (event.getEventType()) {
                    AccessibilityEvent.TYPE_VIEW_FOCUSED -> newDelegate.onFocused(event)
                    AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED -> newDelegate.onAccessibilityFocused(event)
                    else -> {}
                }
                return false
            }
        }) },
        { (view.parent as View).setAccessibilityDelegate(null) },
        object : EventDelegate { })
    }

    @After fun teardown() {
        sessionRule.session.accessibility.view = null
    }

    @Test fun testRootNode() {
        assertThat("provider is not null", provider, notNullValue())
        var node = provider.createAccessibilityNodeInfo(AccessibilityNodeProvider.HOST_VIEW_ID)
        assertThat("Root node should have WebView class name",
            node.getClassName().toString(), equalTo("android.webkit.WebView"))
    }

    @Test fun testPageLoad() {
        sessionRule.session.loadTestPath(INPUTS_PATH)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) { }
        })
    }

    @Test fun testAccessibilityFocus() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        sessionRule.session.loadTestPath(INPUTS_PATH)
        sessionRule.waitForPageStop()

        provider.performAction(AccessibilityNodeProvider.HOST_VIEW_ID,
            AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Text node should not be focusable", node.isFocusable(), equalTo(false))
            }
        })

        provider.performAction(nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Entry node should be focusable", node.isFocusable(), equalTo(true))
            }
        })
    }
}
