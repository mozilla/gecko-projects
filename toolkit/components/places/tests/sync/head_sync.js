/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.import("resource://gre/modules/Services.jsm");

// Import common head.
{
  /* import-globals-from ../head_common.js */
  let commonFile = do_get_file("../head_common.js", false);
  let uri = Services.io.newFileURI(commonFile);
  Services.scriptloader.loadSubScript(uri.spec, this);
}

// Put any other stuff relative to this test folder below.

ChromeUtils.import("resource://gre/modules/Log.jsm");
ChromeUtils.import("resource://gre/modules/ObjectUtils.jsm");
ChromeUtils.import("resource://gre/modules/PlacesSyncUtils.jsm");
ChromeUtils.import("resource://gre/modules/SyncedBookmarksMirror.jsm");
ChromeUtils.import("resource://testing-common/httpd.js");

// These titles are defined in Database::CreateBookmarkRoots
const BookmarksMenuTitle = "menu";
const BookmarksToolbarTitle = "toolbar";
const UnfiledBookmarksTitle = "unfiled";
const MobileBookmarksTitle = "mobile";

function run_test() {
  let bufLog = Log.repository.getLogger("Sync.Engine.Bookmarks.Mirror");
  bufLog.level = Log.Level.Error;

  let sqliteLog = Log.repository.getLogger("Sqlite");
  sqliteLog.level = Log.Level.Error;

  let formatter = new Log.BasicFormatter();
  let appender = new Log.DumpAppender(formatter);
  appender.level = Log.Level.All;

  for (let log of [bufLog, sqliteLog]) {
    log.addAppender(appender);
  }

  do_get_profile();
  run_next_test();
}

function inspectChangeRecords(changeRecords) {
  let results = { updated: [], deleted: [] };
  for (let [id, record] of Object.entries(changeRecords)) {
    (record.tombstone ? results.deleted : results.updated).push(id);
  }
  results.updated.sort();
  results.deleted.sort();
  return results;
}

async function assertLocalTree(rootGuid, expected, message) {
  function bookmarkNodeToInfo(node) {
    let { guid, index, title, typeCode: type } = node;
    let itemInfo = { guid, index, title, type };
    if (node.annos) {
      let syncableAnnos = node.annos.filter(anno => [
        PlacesSyncUtils.bookmarks.DESCRIPTION_ANNO,
        PlacesSyncUtils.bookmarks.SIDEBAR_ANNO,
        PlacesSyncUtils.bookmarks.SMART_BOOKMARKS_ANNO,
        PlacesUtils.LMANNO_FEEDURI,
        PlacesUtils.LMANNO_SITEURI,
      ].includes(anno.name));
      if (syncableAnnos.length) {
        itemInfo.annos = syncableAnnos;
      }
    }
    if (node.uri) {
      itemInfo.url = node.uri;
    }
    if (node.keyword) {
      itemInfo.keyword = node.keyword;
    }
    if (node.children) {
      itemInfo.children = node.children.map(bookmarkNodeToInfo);
    }
    return itemInfo;
  }
  let root = await PlacesUtils.promiseBookmarksTree(rootGuid);
  let actual = bookmarkNodeToInfo(root);
  if (!ObjectUtils.deepEqual(actual, expected)) {
    info(`Expected structure for ${rootGuid}: ${JSON.stringify(expected)}`);
    info(`Actual structure for ${rootGuid}: ${JSON.stringify(actual)}`);
    throw new Assert.constructor.AssertionError({ actual, expected, message });
  }
}

function makeLivemarkServer() {
  let server = new HttpServer();
  server.registerPrefixHandler("/feed/", do_get_file("./livemark.xml"));
  server.start(-1);
  return {
    server,
    get site() {
      let { identity } = server;
      let host = identity.primaryHost.includes(":") ?
        `[${identity.primaryHost}]` : identity.primaryHost;
      return `${identity.primaryScheme}://${host}:${identity.primaryPort}`;
    },
    stopServer() {
      return new Promise(resolve => server.stop(resolve));
    },
  };
}

function shuffle(array) {
  let results = [];
  for (let i = 0; i < array.length; ++i) {
    let randomIndex = Math.floor(Math.random() * (i + 1));
    results[i] = results[randomIndex];
    results[randomIndex] = array[i];
  }
  return results;
}

async function fetchAllKeywords(info) {
  let entries = [];
  await PlacesUtils.keywords.fetch(info, entry => entries.push(entry));
  return entries;
}

async function openMirror(name) {
  let path = OS.Path.join(OS.Constants.Path.profileDir, `${name}_buf.sqlite`);
  let buf = await SyncedBookmarksMirror.open({
    path,
    recordTelemetryEvent() {},
  });
  return buf;
}

function BookmarkObserver({ ignoreDates = true, skipTags = false } = {}) {
  this.notifications = [];
  this.ignoreDates = ignoreDates;
  this.skipTags = skipTags;
}

BookmarkObserver.prototype = {
  onBeginUpdateBatch() {},
  onEndUpdateBatch() {},
  onItemAdded(itemId, parentId, index, type, uri, title, dateAdded, guid,
              parentGuid, source) {
    let urlHref = uri ? uri.spec : null;
    let params = { itemId, parentId, index, type, urlHref, title, guid,
                   parentGuid, source };
    if (!this.ignoreDates) {
      params.dateAdded = dateAdded;
    }
    this.notifications.push({ name: "onItemAdded", params });
  },
  onItemRemoved(itemId, parentId, index, type, uri, guid, parentGuid, source) {
    let urlHref = uri ? uri.spec : null;
    this.notifications.push({
      name: "onItemRemoved",
      params: { itemId, parentId, index, type, urlHref, guid, parentGuid,
                 source },
    });
  },
  onItemChanged(itemId, property, isAnnoProperty, newValue, lastModified, type,
                parentId, guid, parentGuid, oldValue, source) {
    let params = { itemId, property, isAnnoProperty, newValue, type, parentId,
                   guid, parentGuid, oldValue, source };
    if (!this.ignoreDates) {
      params.lastModified = lastModified;
    }
    this.notifications.push({ name: "onItemChanged", params });
  },
  onItemVisited() {},
  onItemMoved(itemId, oldParentId, oldIndex, newParentId, newIndex, type, guid,
              oldParentGuid, newParentGuid, source) {
    this.notifications.push({
      name: "onItemMoved",
      params: { itemId, oldParentId, oldIndex, newParentId, newIndex, type,
                guid, oldParentGuid, newParentGuid, source },
    });
  },

  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsINavBookmarkObserver,
  ]),

  check(expectedNotifications) {
    PlacesUtils.bookmarks.removeObserver(this);
    if (!ObjectUtils.deepEqual(this.notifications, expectedNotifications)) {
      info(`Expected notifications: ${JSON.stringify(expectedNotifications)}`);
      info(`Actual notifications: ${JSON.stringify(this.notifications)}`);
      throw new Assert.constructor.AssertionError({
        actual: this.notifications,
        expected: expectedNotifications,
      });
    }
  },
};

function expectBookmarkChangeNotifications(options) {
  let observer = new BookmarkObserver(options);
  PlacesUtils.bookmarks.addObserver(observer);
  return observer;
}
