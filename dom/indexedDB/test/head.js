/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

var gActiveListeners = {};

function registerPopupEventHandler(eventName, callback) {
  gActiveListeners[eventName] = function (event) {
    if (event.target != PopupNotifications.panel)
      return;
    PopupNotifications.panel.removeEventListener(eventName,
                                                 gActiveListeners[eventName]);
    delete gActiveListeners[eventName];

    callback.call(PopupNotifications.panel);
  }
  PopupNotifications.panel.addEventListener(eventName,
                                            gActiveListeners[eventName]);
}

function unregisterPopupEventHandler(eventName)
{
  PopupNotifications.panel.removeEventListener(eventName,
                                               gActiveListeners[eventName]);
  delete gActiveListeners[eventName];
}

function unregisterAllPopupEventHandlers()
{
  for (let eventName in gActiveListeners) {
    PopupNotifications.panel.removeEventListener(eventName,
                                                 gActiveListeners[eventName]);
  }
  gActiveListeners = {};
}

function triggerMainCommand(popup)
{
  info("triggering main command");
  let notifications = popup.childNodes;
  ok(notifications.length > 0, "at least one notification displayed");
  let notification = notifications[0];
  info("triggering command: " + notification.getAttribute("buttonlabel"));

  EventUtils.synthesizeMouseAtCenter(notification.button, {});
}

function triggerSecondaryCommand(popup)
{
  info("triggering secondary command");
  let notifications = popup.childNodes;
  ok(notifications.length > 0, "at least one notification displayed");
  let notification = notifications[0];
  EventUtils.synthesizeMouseAtCenter(notification.secondaryButton, {});
}

function dismissNotification(popup)
{
  info("dismissing notification");
  executeSoon(function () {
    EventUtils.synthesizeKey("VK_ESCAPE", {});
  });
}

function setFinishedCallback(callback, win)
{
  if (!win) {
    win = window;
  }
  ContentTask.spawn(win.gBrowser.selectedBrowser, null, function*() {
    return yield new Promise(resolve => {
      content.wrappedJSObject.testFinishedCallback = (result, exception) => {
        info("got finished callback");
        resolve({result, exception});
      };
    });
  }).then(({result, exception}) => {
    callback(result, exception);
  });
}

function dispatchEvent(eventName)
{
  info("dispatching event: " + eventName);
  let event = document.createEvent("Events");
  event.initEvent(eventName, false, false);
  gBrowser.selectedBrowser.contentWindow.dispatchEvent(event);
}

function setPermission(url, permission)
{
  const nsIPermissionManager = Components.interfaces.nsIPermissionManager;

  let uri = Components.classes["@mozilla.org/network/io-service;1"]
                      .getService(Components.interfaces.nsIIOService)
                      .newURI(url);
  let ssm = Components.classes["@mozilla.org/scriptsecuritymanager;1"]
                      .getService(Ci.nsIScriptSecurityManager);
  let principal = ssm.createCodebasePrincipal(uri, {});

  Components.classes["@mozilla.org/permissionmanager;1"]
            .getService(nsIPermissionManager)
            .addFromPrincipal(principal, permission,
                              nsIPermissionManager.ALLOW_ACTION);
}

function removePermission(url, permission)
{
  let uri = Components.classes["@mozilla.org/network/io-service;1"]
                      .getService(Components.interfaces.nsIIOService)
                      .newURI(url);
  let ssm = Components.classes["@mozilla.org/scriptsecuritymanager;1"]
                      .getService(Ci.nsIScriptSecurityManager);
  let principal = ssm.createCodebasePrincipal(uri, {});

  Components.classes["@mozilla.org/permissionmanager;1"]
            .getService(Components.interfaces.nsIPermissionManager)
            .removeFromPrincipal(principal, permission);
}

function getPermission(url, permission)
{
  let uri = Components.classes["@mozilla.org/network/io-service;1"]
                      .getService(Components.interfaces.nsIIOService)
                      .newURI(url);
  let ssm = Components.classes["@mozilla.org/scriptsecuritymanager;1"]
                      .getService(Ci.nsIScriptSecurityManager);
  let principal = ssm.createCodebasePrincipal(uri, {});

  return Components.classes["@mozilla.org/permissionmanager;1"]
                   .getService(Components.interfaces.nsIPermissionManager)
                   .testPermissionFromPrincipal(principal, permission);
}
