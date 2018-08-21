/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ReuseSession
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDevToolsAPI

import android.graphics.Rect

import android.os.Build
import android.os.Bundle

import android.support.test.filters.MediumTest
import android.support.test.InstrumentationRegistry
import android.support.test.runner.AndroidJUnit4
import android.text.InputType
import android.util.SparseLongArray

import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityNodeProvider
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityRecord
import android.view.View
import android.view.ViewGroup
import android.widget.EditText

import android.widget.FrameLayout

import org.hamcrest.Matchers.*
import org.junit.Test
import org.junit.Before
import org.junit.After
import org.junit.runner.RunWith

const val DISPLAY_WIDTH = 480
const val DISPLAY_HEIGHT = 640

@RunWith(AndroidJUnit4::class)
@MediumTest
@WithDisplay(width = DISPLAY_WIDTH, height = DISPLAY_HEIGHT)
@WithDevToolsAPI
class AccessibilityTest : BaseSessionTest() {
    lateinit var view: View
    val screenRect = Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT)
    val provider: AccessibilityNodeProvider get() = view.accessibilityNodeProvider

    // Given a child ID, return the virtual descendent ID.
    private fun getVirtualDescendantId(childId: Long): Int {
        try {
            val getVirtualDescendantIdMethod =
                AccessibilityNodeInfo::class.java.getMethod("getVirtualDescendantId", Long::class.java)
            return getVirtualDescendantIdMethod.invoke(null, childId) as Int
        } catch (ex: Exception) {
            return 0
        }
    }

    // Retrieve the virtual descendent ID of the event's source.
    private fun getSourceId(event: AccessibilityEvent): Int {
        try {
            val getSourceIdMethod =
                AccessibilityRecord::class.java.getMethod("getSourceNodeId")
            return getVirtualDescendantId(getSourceIdMethod.invoke(event) as Long)
        } catch (ex: Exception) {
            return 0
        }
    }

    // Get a child ID by index.
    private fun AccessibilityNodeInfo.getChildId(index: Int): Int =
            getVirtualDescendantId(
                    if (Build.VERSION.SDK_INT >= 21)
                        AccessibilityNodeInfo::class.java.getMethod(
                                "getChildId", Int::class.java).invoke(this, index) as Long
                    else
                        (AccessibilityNodeInfo::class.java.getMethod("getChildNodeIds")
                                .invoke(this) as SparseLongArray).get(index))

    private interface EventDelegate {
        fun onAccessibilityFocused(event: AccessibilityEvent) { }
        fun onClicked(event: AccessibilityEvent) { }
        fun onFocused(event: AccessibilityEvent) { }
        fun onSelected(event: AccessibilityEvent) { }
        fun onScrolled(event: AccessibilityEvent) { }
        fun onTextSelectionChanged(event: AccessibilityEvent) { }
        fun onTextChanged(event: AccessibilityEvent) { }
        fun onTextTraversal(event: AccessibilityEvent) { }
        fun onWinContentChanged(event: AccessibilityEvent) { }
    }

    @Before fun setup() {
        // We initialize a view with a parent and grandparent so that the
        // accessibility events propagate up at least to the parent.
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
                when (event.eventType) {
                    AccessibilityEvent.TYPE_VIEW_FOCUSED -> newDelegate.onFocused(event)
                    AccessibilityEvent.TYPE_VIEW_CLICKED -> newDelegate.onClicked(event)
                    AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED -> newDelegate.onAccessibilityFocused(event)
                    AccessibilityEvent.TYPE_VIEW_SELECTED -> newDelegate.onSelected(event)
                    AccessibilityEvent.TYPE_VIEW_SCROLLED -> newDelegate.onScrolled(event)
                    AccessibilityEvent.TYPE_VIEW_TEXT_SELECTION_CHANGED -> newDelegate.onTextSelectionChanged(event)
                    AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED -> newDelegate.onTextChanged(event)
                    AccessibilityEvent.TYPE_VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY -> newDelegate.onTextTraversal(event)
                    AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED -> newDelegate.onWinContentChanged(event)
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

    private fun waitForInitialFocus() {
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) { }
        })
    }

    @Test fun testRootNode() {
        assertThat("provider is not null", provider, notNullValue())
        val node = provider.createAccessibilityNodeInfo(AccessibilityNodeProvider.HOST_VIEW_ID)
        assertThat("Root node should have WebView class name",
            node.className.toString(), equalTo("android.webkit.WebView"))
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
        waitForInitialFocus()

        provider.performAction(AccessibilityNodeProvider.HOST_VIEW_ID,
            AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Text node should not be focusable", node.isFocusable, equalTo(false))
            }
        })

        provider.performAction(nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Entry node should be focusable", node.isFocusable, equalTo(true))
            }
        })
    }

    @Test fun testTextEntryNode() {
        sessionRule.session.loadString("<input aria-label='Name' value='Tobias'>", "text/html")
        waitForInitialFocus()

        mainSession.evaluateJS("$('input').focus()")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                val nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Focused EditBox", node.className.toString(),
                        equalTo("android.widget.EditText"))
                if (Build.VERSION.SDK_INT >= 19) {
                    assertThat("Hint has field name",
                            node.extras.getString("AccessibilityNodeInfo.hint"),
                            equalTo("Name"))
                }
            }
        })
    }

    private fun waitUntilTextSelectionChanged(fromIndex: Int, toIndex: Int) {
        var eventFromIndex = 0;
        var eventToIndex = 0;
        do {
            sessionRule.waitUntilCalled(object : EventDelegate {
                override fun onTextSelectionChanged(event: AccessibilityEvent) {
                    eventFromIndex = event.fromIndex;
                    eventToIndex = event.toIndex;
                }
            })
        } while (fromIndex != eventFromIndex || toIndex != eventToIndex)
    }

    private fun waitUntilTextTraversed(fromIndex: Int, toIndex: Int) {
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onTextTraversal(event: AccessibilityEvent) {
              assertThat("fromIndex matches", event.fromIndex, equalTo(fromIndex))
              assertThat("toIndex matches", event.toIndex, equalTo(toIndex))
            }
        })
    }

    private fun waitUntilClick(checked: Boolean) {
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onClicked(event: AccessibilityEvent) {
                var nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Event's checked state matches", event.isChecked, equalTo(checked))
                assertThat("Checkbox node has correct checked state", node.isChecked, equalTo(checked))
            }
        })
    }

    private fun waitUntilSelect(selected: Boolean) {
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onSelected(event: AccessibilityEvent) {
                var nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Selectable node has correct selected state", node.isSelected, equalTo(selected))
            }
        })
    }

    private fun setSelectionArguments(start: Int, end: Int): Bundle {
        val arguments = Bundle(2)
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, start)
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, end)
        return arguments
    }

    private fun moveByGranularityArguments(granularity: Int, extendSelection: Boolean = false): Bundle {
        val arguments = Bundle(2)
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT, granularity)
        arguments.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, extendSelection)
        return arguments
    }

    @Test fun testClipboard() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID;
        sessionRule.session.loadString("<input value='hello cruel world' id='input'>", "text/html")
        waitForInitialFocus()

        mainSession.evaluateJS("$('input').focus()")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Focused EditBox", node.className.toString(),
                        equalTo("android.widget.EditText"))
            }

            @AssertCalled(count = 1)
            override fun onTextSelectionChanged(event: AccessibilityEvent) {
                assertThat("fromIndex should be at start", event.fromIndex, equalTo(0))
                assertThat("toIndex should be at start", event.toIndex, equalTo(0))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(5, 11))
        waitUntilTextSelectionChanged(5, 11)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_COPY, null)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(11, 11))
        waitUntilTextSelectionChanged(11, 11)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_PASTE, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onTextChanged(event: AccessibilityEvent) {
                assertThat("text should be pasted", event.text[0].toString(), equalTo("hello cruel cruel world"))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(17, 23))
        waitUntilTextSelectionChanged(17, 23)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_PASTE, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onTextChanged(event: AccessibilityEvent) {
                assertThat("text should be pasted", event.text[0].toString(), equalTo("hello cruel cruel cruel"))
            }
        })
    }

    @Test fun testMoveByCharacter() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        sessionRule.session.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus()

        provider.performAction(AccessibilityNodeProvider.HOST_VIEW_ID,
                AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Accessibility focus on first paragraph", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER))
        waitUntilTextTraversed(0, 1) // "L"

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER))
        waitUntilTextTraversed(1, 2) // "o"

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER))
        waitUntilTextTraversed(0, 1) // "L"
    }

    @Test fun testMoveByWord() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        sessionRule.session.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus()

        provider.performAction(AccessibilityNodeProvider.HOST_VIEW_ID,
                AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Accessibility focus on first paragraph", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD))
        waitUntilTextTraversed(0, 5) // "Lorem"

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD))
        waitUntilTextTraversed(6, 11) // "ipsum"

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD))
        waitUntilTextTraversed(0, 5) // "Lorem"
    }

    @Test fun testMoveByLine() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        sessionRule.session.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus()

        provider.performAction(AccessibilityNodeProvider.HOST_VIEW_ID,
                AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Accessibility focus on first paragraph", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE))
        waitUntilTextTraversed(0, 18) // "Lorem ipsum dolor "

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE))
        waitUntilTextTraversed(18, 28) // "sit amet, "

        provider.performAction(nodeId,
                AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE))
        waitUntilTextTraversed(0, 18) // "Lorem ipsum dolor "
    }

    @Test fun testCheckbox() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID;
        sessionRule.session.loadString("<label><input id='checkbox' type='checkbox'>many option</label>", "text/html")
        waitForInitialFocus()

        mainSession.evaluateJS("$('#checkbox').focus()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Checkbox node is checkable", node.isCheckable, equalTo(true))
                assertThat("Checkbox node is clickable", node.isClickable, equalTo(true))
                assertThat("Checkbox node is focusable", node.isFocusable, equalTo(true))
                assertThat("Checkbox node is not checked", node.isChecked, equalTo(false))
                assertThat("Checkbox node has correct role", node.text.toString(), equalTo("many option check button"))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilClick(true)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilClick(false)
    }

    @Test fun testSelectable() {
        var nodeId = View.NO_ID
        sessionRule.session.loadString(
                """<ul style="list-style-type: none;" role="listbox">
                        <li id="li" role="option" onclick="this.setAttribute('aria-selected',
                            this.getAttribute('aria-selected') == 'true' ? 'false' : 'true')">1</li>
                </ul>""","text/html")
        waitForInitialFocus()

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                assertThat("Selectable node is clickable", node.isClickable, equalTo(true))
                assertThat("Selectable node is not selected", node.isSelected, equalTo(false))
                assertThat("Selectable node has correct role", node.text.toString(), equalTo("1 option list box"))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilSelect(true)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilSelect(false)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SELECT, null)
        waitUntilSelect(true)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SELECT, null)
        waitUntilSelect(false)
    }

    private fun screenContainsNode(nodeId: Int): Boolean {
        var node = provider.createAccessibilityNodeInfo(nodeId)
        var nodeBounds = Rect()
        node.getBoundsInScreen(nodeBounds)
        return screenRect.contains(nodeBounds)
    }

    @Test fun testScroll() {
        var nodeId = View.NO_ID
        sessionRule.session.loadString(
                """<body style="margin: 0;">
                        <div style="height: 100vh;"></div>
                        <button>Hello</button>
                        <p style="margin: 0;">Lorem ipsum dolor sit amet, consectetur adipiscing elit,
                            sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.</p>
                </body>""",
                "text/html")
        sessionRule.waitForPageStop()

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = provider.createAccessibilityNodeInfo(nodeId)
                var nodeBounds = Rect()
                node.getBoundsInParent(nodeBounds)
                assertThat("Default root node bounds are correct", nodeBounds, equalTo(screenRect))
            }
        })

        provider.performAction(View.NO_ID, AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled for focused node to be onscreen", event.scrollY, greaterThan(0))
                assertThat("View is not scrolled to the end", event.scrollY, lessThan(event.maxScrollY))
            }

            @AssertCalled(count = 1, order = [3])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled to the end", event.scrollY, equalTo(event.maxScrollY))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is still onscreen", screenContainsNode(nodeId), equalTo(true))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled to the beginning", event.scrollY, equalTo(0))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is offscreen", screenContainsNode(nodeId), equalTo(false))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled to the end", event.scrollY, equalTo(event.maxScrollY))
            }

            @AssertCalled(count = 1, order = [3])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }
        })
    }

    @ReuseSession(false) // XXX automation crash fix (bug 1485107)
    @WithDevToolsAPI
    @Test fun autoFill() {
        // Wait for the accessibility nodes to populate.
        mainSession.loadTestPath(FORMS_HTML_PATH)
        sessionRule.waitUntilCalled(object : EventDelegate {
            // For the root document and the iframe document, each has a form group and
            // a group for inputs outside of forms, so the total count is 4.
            @AssertCalled(count = 4)
            override fun onWinContentChanged(event: AccessibilityEvent) {
            }
        })

        val autoFills = mapOf(
                "#user1" to "bar", "#pass1" to "baz", "#user2" to "bar", "#pass2" to "baz") +
                if (Build.VERSION.SDK_INT >= 19) mapOf(
                        "#email1" to "a@b.c", "#number1" to "24", "#tel1" to "42")
                else mapOf(
                        "#email1" to "bar", "#number1" to "", "#tel1" to "bar")

        // Set up promises to monitor the values changing.
        val promises = autoFills.flatMap { entry ->
            // Repeat each test with both the top document and the iframe document.
            arrayOf("document", "$('#iframe').contentDocument").map { doc ->
                mainSession.evaluateJS("""new Promise(resolve =>
                    $doc.querySelector('${entry.key}').addEventListener(
                        'input', event => resolve([event.target.value, '${entry.value}']),
                        { once: true }))""").asJSPromise()
            }
        }

        // Perform auto-fill and return number of auto-fills performed.
        fun autoFillChild(id: Int, child: AccessibilityNodeInfo) {
            // Seal the node info instance so we can perform actions on it.
            if (child.childCount > 0) {
                for (i in 0 until child.childCount) {
                    val childId = child.getChildId(i)
                    autoFillChild(childId, provider.createAccessibilityNodeInfo(childId))
                }
            }

            if (EditText::class.java.name == child.className) {
                assertThat("Input should be enabled", child.isEnabled, equalTo(true))
                assertThat("Input should be focusable", child.isFocusable, equalTo(true))
                if (Build.VERSION.SDK_INT >= 19) {
                    assertThat("Password type should match", child.isPassword, equalTo(
                            child.inputType == InputType.TYPE_CLASS_TEXT or
                                    InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD))
                }

                val args = Bundle(1)
                val value = if (child.isPassword) "baz" else
                    if (Build.VERSION.SDK_INT < 19) "bar" else
                        when (child.inputType) {
                            InputType.TYPE_CLASS_TEXT or
                                    InputType.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS -> "a@b.c"
                            InputType.TYPE_CLASS_NUMBER -> "24"
                            InputType.TYPE_CLASS_PHONE -> "42"
                            else -> "bar"
                        }

                val ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE = if (Build.VERSION.SDK_INT >= 21)
                    AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE else
                    "ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE"
                val ACTION_SET_TEXT = if (Build.VERSION.SDK_INT >= 21)
                    AccessibilityNodeInfo.ACTION_SET_TEXT else 0x200000

                args.putCharSequence(ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, value)
                assertThat("Can perform auto-fill",
                           provider.performAction(id, ACTION_SET_TEXT, args), equalTo(true))
            }
            child.recycle()
        }

        autoFillChild(View.NO_ID, provider.createAccessibilityNodeInfo(View.NO_ID))

        // Wait on the promises and check for correct values.
        for ((actual, expected) in promises.map { it.value.asJSList<String>() }) {
            assertThat("Auto-filled value must match", actual, equalTo(expected))
        }
    }

    @ReuseSession(false) // XXX automation crash fix (bug 1485107)
    @Test fun autoFill_navigation() {
        fun countAutoFillNodes(cond: (AccessibilityNodeInfo) -> Boolean =
                                       { it.className == "android.widget.EditText" },
                               id: Int = View.NO_ID): Int {
            val info = provider.createAccessibilityNodeInfo(id)
            try {
                return (if (cond(info)) 1 else 0) + (if (info.childCount > 0)
                    (0 until info.childCount).sumBy {
                        countAutoFillNodes(cond, info.getChildId(it))
                    } else 0)
            } finally {
                info.recycle()
            }
        }

        // Wait for the accessibility nodes to populate.
        mainSession.loadTestPath(FORMS_HTML_PATH)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 4)
            override fun onWinContentChanged(event: AccessibilityEvent) {
            }
        })
        assertThat("Initial auto-fill count should match",
                   countAutoFillNodes(), equalTo(14))
        assertThat("Password auto-fill count should match",
                   countAutoFillNodes({ it.isPassword }), equalTo(4))

        // Now wait for the nodes to clear.
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        assertThat("Should not have auto-fill fields",
                   countAutoFillNodes(), equalTo(0))

        // Now wait for the nodes to reappear.
        mainSession.goBack()
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 4)
            override fun onWinContentChanged(event: AccessibilityEvent) {
            }
        })
        assertThat("Should have auto-fill fields again",
                   countAutoFillNodes(), equalTo(14))
        assertThat("Should not have focused field",
                   countAutoFillNodes({ it.isFocused }), equalTo(0))

        mainSession.evaluateJS("$('#pass1').focus()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onFocused(event: AccessibilityEvent) {
            }
        })
        assertThat("Should have one focused field",
                   countAutoFillNodes({ it.isFocused }), equalTo(1))
        // The focused field, its siblings, and its parent should be visible.
        assertThat("Should have at least six visible fields",
                   countAutoFillNodes({ node -> node.isVisibleToUser &&
                           !(Rect().also({ node.getBoundsInScreen(it) }).isEmpty) }),
                   greaterThanOrEqualTo(6))

        mainSession.evaluateJS("$('#pass1').blur()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onFocused(event: AccessibilityEvent) {
            }
        })
        assertThat("Should not have focused field",
                   countAutoFillNodes({ it.isFocused }), equalTo(0))
    }
}
