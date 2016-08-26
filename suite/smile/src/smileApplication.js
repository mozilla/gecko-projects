/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const Ci = Components.interfaces;
const Cc = Components.classes;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

const APPLICATION_CID = Components.ID("{c9ba8f65-c936-4ac6-a859-8936832b0c12}");
const APPLICATION_CONTRACTID = "@mozilla.org/smile/application;1";

//=================================================
// Singleton that holds services and utilities
var Utilities = {
  get bookmarks() {
    let bookmarks =
      Components.classes["@mozilla.org/browser/nav-bookmarks-service;1"]
                .getService(Components.interfaces.nsINavBookmarksService);
    this.__defineGetter__("bookmarks", () => bookmarks);
    return this.bookmarks;
  },

  get bookmarksObserver() {
    let bookmarksObserver = new BookmarksObserver();
    this.__defineGetter__("bookmarksObserver", () => bookmarksObserver);
    return this.bookmarksObserver;
  },

  get annotations() {
    let annotations =
      Components.classes["@mozilla.org/browser/annotation-service;1"]
                .getService(Components.interfaces.nsIAnnotationService);
    this.__defineGetter__("annotations", () => annotations);
    return this.annotations;
  },

  get history() {
    let history =
      Components.classes["@mozilla.org/browser/nav-history-service;1"]
                .getService(Components.interfaces.nsINavHistoryService);
    this.__defineGetter__("history", () => history);
    return this.history;
  },

  get windowMediator() {
    let windowMediator =
      Components.classes["@mozilla.org/appshell/window-mediator;1"]
                .getService(Components.interfaces.nsIWindowMediator);
    this.__defineGetter__("windowMediator", () => windowMediator);
    return this.windowMediator;
  },

  makeURI: function smileutil_makeURI(aSpec) {
    if (!aSpec)
      return null;
    var ios = Components.classes["@mozilla.org/network/io-service;1"]
                        .getService(Components.interfaces.nsIIOService);
    return ios.newURI(aSpec, null, null);
  },

  free: function smileutil_free() {
    delete this.bookmarks;
    delete this.bookmarksObserver;
    delete this.annotations;
    delete this.history;
    delete this.windowMediator;
  }
};


//=================================================
// Window implementation

var smileWindowMap = new WeakMap();

function getWindow(aWindow) {
  let smileWindow = smileWindowMap.get(aWindow);
  if (!smileWindow) {
    smileWindow = new Window(aWindow);
    smileWindowMap.set(aWindow, smileWindow);
  }
  return smileWindow;
}

// Don't call new Window() directly; use getWindow instead.
function Window(aWindow) {
  this._window = aWindow;
  this._events = new Events();

  this._watch("TabOpen");
  this._watch("TabMove");
  this._watch("TabClose");
  this._watch("TabSelect");
}

Window.prototype = {
  get events() {
    return this._events;
  },

  get _tabbrowser() {
    return this._window.getBrowser();
  },

  /*
   * Helper used to setup event handlers on the XBL element. Note that the events
   * are actually dispatched to tabs, so we capture them.
   */
  _watch: function win_watch(aType) {
    this._tabbrowser.addEventListener(aType, this, true);
  },

  handleEvent: function win_handleEvent(aEvent) {
    this._events.dispatch(aEvent.type, getBrowserTab(this, aEvent.originalTarget));
  },

  get tabs() {
    var tabs = [];
    var tabNodes = this._tabbrowser.tabs;
    for (var i = 0; i < tabNodes.length; i++)
      tabs.push(getBrowserTab(this, tabNodes[i]));
    return tabs;
  },

  get activeTab() {
    return getBrowserTab(this, this._tabbrowser.selectedTab);
  },

  open: function win_open(aURI) {
    return getBrowserTab(this, this._tabbrowser.addTab(aURI.spec));
  },

  QueryInterface: XPCOMUtils.generateQI([Components.interfaces.smileIWindow])
};


//=================================================
// BrowserTab implementation
// SMILE deals with tabs whereas FUEL deals with browsers.

var smileBrowserTabMap = new WeakMap();

function getBrowserTab(aSMILEWindow, aTab) {
  let smileBrowserTab = smileBrowserTabMap.get(aTab);
  if (!smileBrowserTab) {
    smileBrowserTab = new BrowserTab(aSMILEWindow, aTab);
    smileBrowserTabMap.set(aTab, smileBrowserTab);
  }
  return smileBrowserTab;
}

// Don't call new BrowserTab() directly; call getBrowserTab instead.
function BrowserTab(aSMILEWindow, aTab) {
  this._window = aSMILEWindow;
  this._browser = aTab.linkedBrowser;
  this._tab = aTab;
  this._events = new Events();

  this._watch("load");
}

BrowserTab.prototype = {
  get _tabbrowser() {
    return this._window._tabbrowser;
  },

  get uri() {
    return this._browser.currentURI;
  },

  get index() {
    try {
      return this._tabbrowser.getTabIndex(this._tab);
    }
    catch (e) {
      return -1;
    }
  },

  get events() {
    return this._events;
  },

  get window() {
    return this._window;
  },

  get document() {
    return this._browser.contentDocument;
  },

  /*
   * Helper used to setup event handlers on the XBL element
   */
  _watch: function bt_watch(aType) {
    this._browser.addEventListener(aType, this, true);
  },

  handleEvent: function bt_handleEvent(aEvent) {
    if (aEvent.type == "load") {
      if (!(aEvent.originalTarget instanceof Components.interfaces.nsIDOMDocument))
        return;

      if (aEvent.originalTarget.defaultView instanceof Components.interfaces.nsIDOMWindow &&
          aEvent.originalTarget.defaultView.frameElement)
        return;
    }

    this._events.dispatch(aEvent.type, this);
  },

  load: function bt_load(aURI) {
    this._browser.loadURI(aURI.spec, null, null);
  },

  focus: function bt_focus() {
    this._tabbrowser.selectedTab = this._tab;
    this._tabbrowser.focus();
  },

  close: function bt_close() {
    this._tabbrowser.removeTab(this._tab);
  },

  moveBefore: function bt_movebefore(aBefore) {
    this._tabbrowser.moveTabTo(this._tab, aBefore.index);
  },

  moveToEnd: function bt_moveend() {
    this._tabbrowser.moveTabTo(this._tab, this._tabbrowser.browsers.length);
  },

  QueryInterface: XPCOMUtils.generateQI([Components.interfaces.smileIBrowserTab])
};


//=================================================
// Annotations implementation
function Annotations(aId) {
  this._id = aId;
}

Annotations.prototype = {
  get names() {
    return Utilities.annotations.getItemAnnotationNames(this._id);
  },

  has: function ann_has(aName) {
    return Utilities.annotations.itemHasAnnotation(this._id, aName);
  },

  get: function ann_get(aName) {
    if (this.has(aName))
      return Utilities.annotations.getItemAnnotation(this._id, aName);

    return null;
  },

  set: function ann_set(aName, aValue, aExpiration) {
    Utilities.annotations.setItemAnnotation(this._id, aName, aValue, 0, aExpiration);
  },

  remove: function ann_remove(aName) {
    if (aName)
      Utilities.annotations.removeItemAnnotation(this._id, aName);
  },

  QueryInterface: XPCOMUtils.generateQI([Components.interfaces.smileIAnnotations])
};


//=================================================
// BookmarksObserver implementation (internal class)
//
// BookmarksObserver is a global singleton which watches the browser's
// bookmarks and sends you events when things change.
//
// You can register three different kinds of event listeners on
// BookmarksObserver, using addListener, addFolderListener, and
// addRootlistener.
//
//  - addListener(aId, aEvent, aListener) lets you listen to a specific
//    bookmark.  You can listen to the "change", "move", and "remove" events.
//
//  - addFolderListener(aId, aEvent, aListener) lets you listen to a specific
//    bookmark folder.  You can listen to "addchild" and "removechild".
//
//  - addRootListener(aEvent, aListener) lets you listen to the root bookmark
//    node.  This lets you hear "add", "remove", and "change" events on all
//    bookmarks.
//

function BookmarksObserver() {
  this._eventsDict = {};
  this._folderEventsDict = {};
  this._rootEvents = new Events();
  Utilities.bookmarks.addObserver(this, /* ownsWeak = */ true);
}

BookmarksObserver.prototype = {
  onBeginUpdateBatch: function () {},
  onEndUpdateBatch: function () {},
  onItemVisited: function () {},

  onItemAdded: function bo_onItemAdded(aId, aFolder, aIndex, aItemType, aURI) {
    this._rootEvents.dispatch("add", aId);
    this._dispatchToEvents("addchild", aId, this._folderEventsDict[aFolder]);
  },

  onItemRemoved: function bo_onItemRemoved(aId, aFolder, aIndex) {
    this._rootEvents.dispatch("remove", aId);
    this._dispatchToEvents("remove", aId, this._eventsDict[aId]);
    this._dispatchToEvents("removechild", aId, this._folderEventsDict[aFolder]);
  },

  onItemChanged: function bo_onItemChanged(aId, aProperty, aIsAnnotationProperty, aValue) {
    this._rootEvents.dispatch("change", aProperty);
    this._dispatchToEvents("change", aProperty, this._eventsDict[aId]);
  },

  onItemMoved: function bo_onItemMoved(aId, aOldParent, aOldIndex, aNewParent, aNewIndex) {
    this._dispatchToEvents("move", aId, this._eventsDict[aId]);
  },

  _dispatchToEvents: function bo_dispatchToEvents(aEvent, aData, aEvents) {
    if (aEvents) {
      aEvents.dispatch(aEvent, aData);
    }
  },

  _addListenerToDict: function bo_addListenerToDict(aId, aEvent, aListener, aDict) {
    var events = aDict[aId];
    if (!events) {
      events = new Events();
      aDict[aId] = events;
    }
    events.addListener(aEvent, aListener);
  },

  _removeListenerFromDict: function bo_removeListenerFromDict(aId, aEvent, aListener, aDict) {
    var events = aDict[aId];
    if (!events) {
      return;
    }

    events.removeListener(aEvent, aListener);
    if (events._listeners.length == 0) {
      delete aDict[aId];
    }
  },

  addListener: function bo_addListener(aId, aEvent, aListener) {
    this._addListenerToDict(aId, aEvent, aListener, this._eventsDict);
  },

  removeListener: function bo_removeListener(aId, aEvent, aListener) {
    this._removeListenerFromDict(aId, aEvent, aListener, this._eventsDict);
  },

  addFolderListener: function bo_addFolderListener(aId, aEvent, aListener) {
    this._addListenerToDict(aId, aEvent, aListener, this._folderEventsDict);
  },

  removeFolderListener: function bo_removeFolderListener(aId, aEvent, aListener) {
    this._removeListenerFromDict(aId, aEvent, aListener, this._folderEventsDict);
  },

  addRootListener: function bo_addRootListener(aEvent, aListener) {
    this._rootEvents.addListener(aEvent, aListener);
  },

  removeRootListener: function bo_removeRootListener(aEvent, aListener) {
    this._rootEvents.removeListener(aEvent, aListener);
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsINavBookmarksObserver,
                                         Ci.nsISupportsWeakReference])
};

//=================================================
// Bookmark implementation
//
// Bookmark event listeners are stored in BookmarksObserver, not in the
// Bookmark objects themselves.  Thus, you don't have to hold on to a Bookmark
// object in order for your event listener to stay valid, and Bookmark objects
// not kept alive by the extension can be GC'ed.
//
// A consequence of this is that if you have two different Bookmark objects x
// and y for the same bookmark (i.e., x != y but x.id == y.id), and you do
//
//   x.addListener("foo", fun);
//   y.removeListener("foo", fun);
//
// the second line will in fact remove the listener added in the first line.
//

function Bookmark(aId, aParent, aType) {
  this._id = aId;
  this._parent = aParent;
  this._type = aType || "bookmark";
  this._annotations = new Annotations(this._id);

  // Our _events object forwards to bookmarksObserver.
  var self = this;
  this._events = {
    addListener: function bookmarkevents_al(aEvent, aListener) {
      Utilities.bookmarksObserver.addListener(self._id, aEvent, aListener);
    },

    removeListener: function bookmarkevents_rl(aEvent, aListener) {
      Utilities.bookmarksObserver.removeListener(self._id, aEvent, aListener);
    },

    QueryInterface: XPCOMUtils.generateQI([Ci.extIEvents])
  };

  // For our onItemMoved listener, which updates this._parent.
  Utilities.bookmarks.addObserver(this, /* ownsWeak = */ true);
}

Bookmark.prototype = {
  get id() {
    return this._id;
  },

  get title() {
    return Utilities.bookmarks.getItemTitle(this._id);
  },

  set title(aTitle) {
    Utilities.bookmarks.setItemTitle(this._id, aTitle);
  },

  get uri() {
    return Utilities.bookmarks.getBookmarkURI(this._id);
  },

  set uri(aURI) {
    return Utilities.bookmarks.changeBookmarkURI(this._id, aURI);
  },

  get description() {
    return this._annotations.get("bookmarkProperties/description");
  },

  set description(aDesc) {
    this._annotations.set("bookmarkProperties/description", aDesc, Components.interfaces.nsIAnnotationService.EXPIRE_NEVER);
  },

  get keyword() {
    return Utilities.bookmarks.getKeywordForBookmark(this._id);
  },

  set keyword(aKeyword) {
    Utilities.bookmarks.setKeywordForBookmark(this._id, aKeyword);
  },

  get type() {
    return this._type;
  },

  get parent() {
    return this._parent;
  },

  set parent(aFolder) {
    Utilities.bookmarks.moveItem(this._id, aFolder.id, Utilities.bookmarks.DEFAULT_INDEX);
    // this._parent is updated in onItemMoved
  },

  get annotations() {
    return this._annotations;
  },

  get events() {
    return this._events;
  },

  remove: function bm_remove() {
    Utilities.bookmarks.removeItem(this._id);
  },

  onBeginUpdateBatch: function () {},
  onEndUpdateBatch: function () {},
  onItemAdded: function () {},
  onItemVisited: function () {},
  onItemRemoved: function () {},
  onItemChanged: function () {},

  onItemMoved: function bm_onItemMoved(aId, aOldParent, aOldIndex, aNewParent, aNewIndex) {
    if (aId == this._id) {
      this._parent = new BookmarkFolder(aNewParent, Utilities.bookmarks.getFolderIdForItem(aNewParent));
    }
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.smileIBookmark,
                                         Ci.nsINavBookmarksObserver,
                                         Ci.nsISupportsWeakReference])
};


//=================================================
// BookmarkFolder implementation
//
// As with Bookmark, events on BookmarkFolder are handled by the
// BookmarksObserver singleton.
//

function BookmarkFolder(aId, aParent) {
  this._id = aId;
  this._parent = aParent;
  this._annotations = new Annotations(this._id);

  // Our event listeners are handled by the BookmarksObserver singleton.  This
  // is a bit complicated because there are three different kinds of events we
  // might want to listen to here:
  //
  //  - If this._parent is null, we're the root bookmark folder, and all our
  //    listeners should be root listeners.
  //
  //  - Otherwise, events ending with "child" (addchild, removechild) are
  //    handled by a folder listener.
  //
  //  - Other events are handled by a vanilla bookmark listener.

  var self = this;
  this._events = {
    addListener: function bmfevents_al(aEvent, aListener) {
      if (self._parent) {
        if (/child$/.test(aEvent)) {
          Utilities.bookmarksObserver.addFolderListener(self._id, aEvent, aListener);
        } else {
          Utilities.bookmarksObserver.addListener(self._id, aEvent, aListener);
        }
      } else {
        Utilities.bookmarksObserver.addRootListener(aEvent, aListener);
      }
    },

    removeListener: function bmfevents_rl(aEvent, aListener) {
      if (self._parent) {
        if (/child$/.test(aEvent)) {
          Utilities.bookmarksObserver.removeFolderListener(self._id, aEvent, aListener);
        } else {
          Utilities.bookmarksObserver.removeListener(self._id, aEvent, aListener);
        }
      } else {
        Utilities.bookmarksObserver.removeRootListener(aEvent, aListener);
      }
    },

    QueryInterface: XPCOMUtils.generateQI([Ci.extIEvents])
  };

  // For our onItemMoved listener, which updates this._parent.
  Utilities.bookmarks.addObserver(this, /* ownsWeak = */ true);
}

BookmarkFolder.prototype = {
  get id() {
    return this._id;
  },

  get title() {
    return Utilities.bookmarks.getItemTitle(this._id);
  },

  set title(aTitle) {
    Utilities.bookmarks.setItemTitle(this._id, aTitle);
  },

  get description() {
    return this._annotations.get("bookmarkProperties/description");
  },

  set description(aDesc) {
    this._annotations.set("bookmarkProperties/description", aDesc, Components.interfaces.nsIAnnotationService.EXPIRE_NEVER);
  },

  get type() {
    return "folder";
  },

  get parent() {
    return this._parent;
  },

  set parent(aFolder) {
    Utilities.bookmarks.moveItem(this._id, aFolder.id, Utilities.bookmarks.DEFAULT_INDEX);
    // this._parent is updated in onItemMoved
  },

  get annotations() {
    return this._annotations;
  },

  get events() {
    return this._events;
  },

  get children() {
    var items = [];

    var options = Utilities.history.getNewQueryOptions();
    var query = Utilities.history.getNewQuery();
    query.setFolders([this._id], 1);
    var result = Utilities.history.executeQuery(query, options);
    var rootNode = result.root;
    rootNode.containerOpen = true;
    var cc = rootNode.childCount;
    for (var i = 0; i < cc; ++i) {
      var node = rootNode.getChild(i);
      if (node.type == node.RESULT_TYPE_FOLDER) {
        var folder = new BookmarkFolder(node.itemId, this._id);
        items.push(folder);
      }
      else if (node.type == node.RESULT_TYPE_SEPARATOR) {
        var separator = new Bookmark(node.itemId, this._id, "separator");
        items.push(separator);
      }
      else {
        var bookmark = new Bookmark(node.itemId, this._id, "bookmark");
        items.push(bookmark);
      }
    }
    rootNode.containerOpen = false;

    return items;
  },

  addBookmark: function bmf_addbm(aTitle, aUri) {
    var newBookmarkID = Utilities.bookmarks.insertBookmark(this._id, aUri, Utilities.bookmarks.DEFAULT_INDEX, aTitle);
    var newBookmark = new Bookmark(newBookmarkID, this, "bookmark");
    return newBookmark;
  },

  addSeparator: function bmf_addsep() {
    var newBookmarkID = Utilities.bookmarks.insertSeparator(this._id, Utilities.bookmarks.DEFAULT_INDEX);
    var newBookmark = new Bookmark(newBookmarkID, this, "separator");
    return newBookmark;
  },

  addFolder: function bmf_addfolder(aTitle) {
    var newFolderID = Utilities.bookmarks.createFolder(this._id, aTitle, Utilities.bookmarks.DEFAULT_INDEX);
    var newFolder = new BookmarkFolder(newFolderID, this);
    return newFolder;
  },

  remove: function bmf_remove() {
    Utilities.bookmarks.removeItem(this._id);
  },

  // observer
  onBeginUpdateBatch: function () {},
  onEndUpdateBatch: function () {},
  onItemAdded: function () {},
  onItemRemoved: function () {},
  onItemChanged: function () {},

  onItemMoved: function bf_onItemMoved(aId, aOldParent, aOldIndex, aNewParent, aNewIndex) {
    if (this._id == aId) {
      this._parent = new BookmarkFolder(aNewParent, Utilities.bookmarks.getFolderIdForItem(aNewParent));
    }
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.smileIBookmarkFolder,
                                         Ci.nsINavBookmarksObserver,
                                         Ci.nsISupportsWeakReference])
};


//=================================================
// BookmarkRoots implementation
function BookmarkRoots() {
}

BookmarkRoots.prototype = {
  get menu() {
    if (!this._menu)
      this._menu = new BookmarkFolder(Utilities.bookmarks.bookmarksMenuFolder, null);

    return this._menu;
  },

  get toolbar() {
    if (!this._toolbar)
      this._toolbar = new BookmarkFolder(Utilities.bookmarks.toolbarFolder, null);

    return this._toolbar;
  },

  get tags() {
    if (!this._tags)
      this._tags = new BookmarkFolder(Utilities.bookmarks.tagsFolder, null);

    return this._tags;
  },

  get unfiled() {
    if (!this._unfiled)
      this._unfiled = new BookmarkFolder(Utilities.bookmarks.unfiledBookmarksFolder, null);

    return this._unfiled;
  },

  QueryInterface: XPCOMUtils.generateQI([Components.interfaces.smileIBookmarkRoots])
};


//=================================================
// Factory - Treat Application as a singleton
// XXX This is required, because we're registered for the 'JavaScript global
// privileged property' category, whose handler always calls createInstance.
// See bug 386535.
var gSingleton = null;
var ApplicationFactory = {
  createInstance: function af_ci(aOuter, aIID) {
    if (aOuter != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;

    if (gSingleton == null) {
      gSingleton = new Application();
    }

    return gSingleton.QueryInterface(aIID);
  }
};

#include ../../../toolkit/components/exthelper/extApplication.js

//=================================================
// Application constructor
function Application() {
  this.initToolkitHelpers();
}

//=================================================
// Application implementation
Application.prototype = {
  // set the proto, defined in extApplication.js
  __proto__: extApplication.prototype,

  // for XPCOMUtils
  classID: APPLICATION_CID,

  // redefine the default factory for XPCOMUtils
  _xpcom_factory: ApplicationFactory,

  // for nsISupports
  QueryInterface: XPCOMUtils.generateQI(
                    [Components.interfaces.smileIApplication,
                     Components.interfaces.extIApplication,
                     Components.interfaces.nsIObserver,
                     Components.interfaces.nsISupportsWeakReference]),

  // for nsIClassInfo
  classInfo: XPCOMUtils.generateCI({
    classID: APPLICATION_CID,
    contractID: APPLICATION_CONTRACTID,
    interfaces: [Components.interfaces.smileIApplication,
                 Components.interfaces.extIApplication,
                 Components.interfaces.nsIObserver],
    flags: Components.interfaces.nsIClassInfo.SINGLETON}),

  // for nsIObserver
  observe: function app_observe(aSubject, aTopic, aData) {
    // Call the extApplication version of this function first
    this.__proto__.__proto__.observe.call(this, aSubject, aTopic, aData);
    if (aTopic == "xpcom-shutdown") {
      this._obs.removeObserver(this, "xpcom-shutdown");
      delete this.bookmarks;
      Utilities.free();
    }
  },

  get bookmarks() {
    let bookmarks = new BookmarkRoots();
    this.__defineGetter__("bookmarks", () => bookmarks);
    return this.bookmarks;
  },

  get windows() {
    var win = [];
    var browserEnum = Utilities.windowMediator.getEnumerator("navigator:browser");

    while (browserEnum.hasMoreElements())
      win.push(getWindow(browserEnum.getNext()));

    return win;
  },

  get activeWindow() {
    return getWindow(Utilities.windowMediator.getMostRecentWindow("navigator:browser"));
  },

  // For steelIApplication compatibility.
  platformIsMac: "nsILocalFileMac" in Components.interfaces
};

var NSGetFactory = XPCOMUtils.generateNSGetFactory([Application]);
