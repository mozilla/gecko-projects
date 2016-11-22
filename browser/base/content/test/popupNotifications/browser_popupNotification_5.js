/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function test() {
  waitForExplicitFinish();

  ok(PopupNotifications, "PopupNotifications object exists");
  ok(PopupNotifications.panel, "PopupNotifications panel exists");

  setup();
  goNext();
}

var gNotification;

var tests = [
  // panel updates should fire the showing and shown callbacks again.
  { id: "Test#1",
    run: function() {
      this.notifyObj = new BasicNotification(this.id);
      this.notification = showNotification(this.notifyObj);
    },
    onShown: function(popup) {
      checkPopup(popup, this.notifyObj);

      this.notifyObj.showingCallbackTriggered = false;
      this.notifyObj.shownCallbackTriggered = false;

      // Force an update of the panel. This is typically called
      // automatically when receiving 'activate' or 'TabSelect' events,
      // but from a setTimeout, which is inconvenient for the test.
      PopupNotifications._update();

      checkPopup(popup, this.notifyObj);

      this.notification.remove();
    },
    onHidden: function() { }
  },
  // A first dismissed notification shouldn't stop _update from showing a second notification
  { id: "Test#2",
    run: function() {
      this.notifyObj1 = new BasicNotification(this.id);
      this.notifyObj1.id += "_1";
      this.notifyObj1.anchorID = "default-notification-icon";
      this.notifyObj1.options.dismissed = true;
      this.notification1 = showNotification(this.notifyObj1);

      this.notifyObj2 = new BasicNotification(this.id);
      this.notifyObj2.id += "_2";
      this.notifyObj2.anchorID = "geo-notification-icon";
      this.notifyObj2.options.dismissed = true;
      this.notification2 = showNotification(this.notifyObj2);

      this.notification2.dismissed = false;
      PopupNotifications._update();
    },
    onShown: function(popup) {
      checkPopup(popup, this.notifyObj2);
      this.notification1.remove();
      this.notification2.remove();
    },
    onHidden: function(popup) { }
  },
  // The anchor icon should be shown for notifications in background windows.
  { id: "Test#3",
    run: function() {
      let notifyObj = new BasicNotification(this.id);
      notifyObj.options.dismissed = true;
      let win = gBrowser.replaceTabWithWindow(gBrowser.addTab("about:blank"));
      whenDelayedStartupFinished(win, function() {
        showNotification(notifyObj);
        let anchor = document.getElementById("default-notification-icon");
        is(anchor.getAttribute("showing"), "true", "the anchor is shown");
        win.close();
        goNext();
      });
    }
  },
  // Test that persistent doesn't allow the notification to persist after
  // navigation.
  { id: "Test#4",
    run: function* () {
      this.oldSelectedTab = gBrowser.selectedTab;
      gBrowser.selectedTab = gBrowser.addTab("about:blank");
      yield promiseTabLoadEvent(gBrowser.selectedTab, "http://example.com/");
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.addOptions({
        persistent: true
      });
      this.notification = showNotification(this.notifyObj);
    },
    onShown: function* (popup) {
      this.complete = false;

      yield promiseTabLoadEvent(gBrowser.selectedTab, "http://example.org/");
      yield promiseTabLoadEvent(gBrowser.selectedTab, "http://example.com/");

      // This code should not be executed.
      ok(false, "Should have removed the notification after navigation");
      // Properly dismiss and cleanup in case the unthinkable happens.
      this.complete = true;
      triggerSecondaryCommand(popup, 0);
    },
    onHidden: function(popup) {
      ok(!this.complete, "Should have hidden the notification after navigation");
      this.notification.remove();
      gBrowser.removeTab(gBrowser.selectedTab);
      gBrowser.selectedTab = this.oldSelectedTab;
    }
  },
  // Test that persistent allows the notification to persist until explicitly
  // dismissed.
  { id: "Test#5",
    run: function* () {
      this.oldSelectedTab = gBrowser.selectedTab;
      gBrowser.selectedTab = gBrowser.addTab("about:blank");
      yield promiseTabLoadEvent(gBrowser.selectedTab, "http://example.com/");
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.addOptions({
        persistent: true
      });
      this.notification = showNotification(this.notifyObj);
    },
    onShown: function* (popup) {
      this.complete = false;

      // Notification should persist after attempt to dismiss by clicking on the
      // content area.
      let browser = gBrowser.selectedBrowser;
      yield BrowserTestUtils.synthesizeMouseAtCenter("body", {}, browser)

      // Notification should be hidden after dismissal via Don't Allow.
      this.complete = true;
      triggerSecondaryCommand(popup, 0);
    },
    onHidden: function(popup) {
      ok(this.complete, "Should have hidden the notification after clicking Not Now");
      this.notification.remove();
      gBrowser.removeTab(gBrowser.selectedTab);
      gBrowser.selectedTab = this.oldSelectedTab;
    }
  },
  // Test that persistent panels are still open after switching to another tab
  // and back.
  { id: "Test#6a",
    run: function* () {
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.options.persistent = true;
      gNotification = showNotification(this.notifyObj);
    },
    onShown: function* (popup) {
      this.oldSelectedTab = gBrowser.selectedTab;
      gBrowser.selectedTab = gBrowser.addTab("about:blank");
      info("Waiting for the new tab to load.");
      yield promiseTabLoadEvent(gBrowser.selectedTab, "http://example.com/");
    },
    onHidden: function(popup) {
      ok(true, "Should have hidden the notification after tab switch");
      gBrowser.removeTab(gBrowser.selectedTab);
      gBrowser.selectedTab = this.oldSelectedTab;
    }
  },
  // Second part of the previous test that compensates for the limitation in
  // runNextTest that expects a single onShown/onHidden invocation per test.
  { id: "Test#6b",
    run: function* () {
      let id = PopupNotifications.panel.firstChild.getAttribute("popupid");
      ok(id.endsWith("Test#6a"), "Should have found the notification from Test6a");
      ok(PopupNotifications.isPanelOpen, "Should have shown the popup again after getting back to the tab");
      gNotification.remove();
      gNotification = null;
      goNext();
    }
  },
  // Test that persistent panels are still open after switching to another
  // window and back.
  { id: "Test#7",
    run: function* () {
      this.oldSelectedTab = gBrowser.selectedTab;
      gBrowser.selectedTab = gBrowser.addTab("about:blank");
      let notifyObj = new BasicNotification(this.id);
      notifyObj.options.persistent = true;
      this.notification = showNotification(notifyObj);
      let win = gBrowser.replaceTabWithWindow(gBrowser.addTab("about:blank"));
      whenDelayedStartupFinished(win, () => {
        ok(notifyObj.shownCallbackTriggered, "Should have triggered the shown callback");
        let anchor = win.document.getElementById("default-notification-icon");
        win.PopupNotifications._reshowNotifications(anchor);
        ok(win.PopupNotifications.panel.childNodes.length == 0,
           "no notification displayed in new window");
        ok(PopupNotifications.isPanelOpen, "Should be still showing the popup in the first window");
        win.close();
        let id = PopupNotifications.panel.firstChild.getAttribute("popupid");
        ok(id.endsWith("Test#7"), "Should have found the notification from Test7");
        ok(PopupNotifications.isPanelOpen, "Should have shown the popup again after getting back to the window");
        this.notification.remove();
        gBrowser.removeTab(gBrowser.selectedTab);
        gBrowser.selectedTab = this.oldSelectedTab;
        goNext();
      });
    }
  },
  // Test that only the first persistent notification is shown on update
  { id: "Test#8",
    run: function() {
      this.notifyObj1 = new BasicNotification(this.id);
      this.notifyObj1.id += "_1";
      this.notifyObj1.anchorID = "default-notification-icon";
      this.notifyObj1.options.persistent = true;
      this.notification1 = showNotification(this.notifyObj1);

      this.notifyObj2 = new BasicNotification(this.id);
      this.notifyObj2.id += "_2";
      this.notifyObj2.anchorID = "geo-notification-icon";
      this.notifyObj2.options.persistent = true;
      this.notification2 = showNotification(this.notifyObj2);

      PopupNotifications._update();
    },
    onShown: function(popup) {
      checkPopup(popup, this.notifyObj1);
      this.notification1.remove();
      this.notification2.remove();
    },
    onHidden: function(popup) { }
  },
  // Test that persistent notifications are shown stacked by anchor on update
  { id: "Test#9",
    run: function() {
      this.notifyObj1 = new BasicNotification(this.id);
      this.notifyObj1.id += "_1";
      this.notifyObj1.anchorID = "default-notification-icon";
      this.notifyObj1.options.persistent = true;
      this.notification1 = showNotification(this.notifyObj1);

      this.notifyObj2 = new BasicNotification(this.id);
      this.notifyObj2.id += "_2";
      this.notifyObj2.anchorID = "geo-notification-icon";
      this.notifyObj2.options.persistent = true;
      this.notification2 = showNotification(this.notifyObj2);

      this.notifyObj3 = new BasicNotification(this.id);
      this.notifyObj3.id += "_3";
      this.notifyObj3.anchorID = "default-notification-icon";
      this.notifyObj3.options.persistent = true;
      this.notification3 = showNotification(this.notifyObj3);

      PopupNotifications._update();
    },
    onShown: function(popup) {
      let notifications = popup.childNodes;
      is(notifications.length, 2, "two notifications displayed");
      let [notification1, notification2] = notifications;
      is(notification1.id, this.notifyObj1.id + "-notification", "id 1 matches");
      is(notification2.id, this.notifyObj3.id + "-notification", "id 2 matches");

      this.notification1.remove();
      this.notification2.remove();
      this.notification3.remove();
    },
    onHidden: function(popup) { }
  },
];
