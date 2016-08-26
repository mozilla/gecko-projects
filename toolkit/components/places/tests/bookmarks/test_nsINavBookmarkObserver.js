/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that each nsINavBookmarksObserver method gets the correct input.

var gBookmarksObserver = {
  expected: [],
  validate: function (aMethodName, aArguments) {
    do_check_eq(this.expected[0].name, aMethodName);

    let args = this.expected.shift().args;
    do_check_eq(aArguments.length, args.length);
    for (let i = 0; i < aArguments.length; i++) {
      do_print(aMethodName + "(args[" + i + "]: " + args[i].name + ")");
      do_check_true(args[i].check(aArguments[i]));
    }

    if (this.expected.length == 0) {
      run_next_test();
    }
  },

  // nsINavBookmarkObserver
  onBeginUpdateBatch() {
    return this.validate("onBeginUpdateBatch", arguments);
  },
  onEndUpdateBatch() {
    return this.validate("onEndUpdateBatch", arguments);
  },
  onItemAdded() {
    return this.validate("onItemAdded", arguments);
  },
  onItemRemoved() {
    return this.validate("onItemRemoved", arguments);
  },
  onItemChanged() {
    return this.validate("onItemChanged", arguments);
  },
  onItemVisited() {
    return this.validate("onItemVisited", arguments);
  },
  onItemMoved() {
    return this.validate("onItemMoved", arguments);
  },

  // nsISupports
  QueryInterface: XPCOMUtils.generateQI([Ci.nsINavBookmarkObserver]),
}

add_test(function batch() {
  gBookmarksObserver.expected = [
    { name: "onBeginUpdateBatch",
     args: [] },
    { name: "onEndUpdateBatch",
     args: [] },
  ];
  PlacesUtils.bookmarks.runInBatchMode({
    runBatched: function () {
      // Nothing.
    }
  }, null);
});

add_test(function onItemAdded_bookmark() {
  const TITLE = "Bookmark 1";
  let uri = NetUtil.newURI("http://1.mozilla.org/");
  gBookmarksObserver.expected = [
    { name: "onItemAdded",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "uri", check: v => v instanceof Ci.nsIURI && v.equals(uri) },
        { name: "title", check: v => v === TITLE },
        { name: "dateAdded", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.insertBookmark(PlacesUtils.unfiledBookmarksFolderId,
                                       uri, PlacesUtils.bookmarks.DEFAULT_INDEX,
                                       TITLE);
});

add_test(function onItemAdded_separator() {
  gBookmarksObserver.expected = [
    { name: "onItemAdded",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "index", check: v => v === 1 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_SEPARATOR },
        { name: "uri", check: v => v === null },
        { name: "title", check: v => v === null },
        { name: "dateAdded", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.insertSeparator(PlacesUtils.unfiledBookmarksFolderId,
                                        PlacesUtils.bookmarks.DEFAULT_INDEX);
});

add_test(function onItemAdded_folder() {
  const TITLE = "Folder 1";
  gBookmarksObserver.expected = [
    { name: "onItemAdded",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "index", check: v => v === 2 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_FOLDER },
        { name: "uri", check: v => v === null },
        { name: "title", check: v => v === TITLE },
        { name: "dateAdded", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.createFolder(PlacesUtils.unfiledBookmarksFolderId,
                                     TITLE,
                                     PlacesUtils.bookmarks.DEFAULT_INDEX);
});

add_test(function onItemChanged_title_bookmark() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  let uri = PlacesUtils.bookmarks.getBookmarkURI(id);
  const TITLE = "New title";
  gBookmarksObserver.expected = [
    { name: "onItemChanged", // This is an unfortunate effect of bug 653910.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "property", check: v => v === "title" },
        { name: "isAnno", check: v => v === false },
        { name: "newValue", check: v => v === TITLE },
        { name: "lastModified", check: v => typeof(v) == "number" && v > 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldValue", check: v => typeof(v) == "string" },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.setItemTitle(id, TITLE);
});

add_test(function onItemChanged_tags_bookmark() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  let uri = PlacesUtils.bookmarks.getBookmarkURI(id);
  const TITLE = "New title";
  const TAG = "tag"
  gBookmarksObserver.expected = [
    { name: "onItemAdded", // This is the tag folder.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => v === PlacesUtils.tagsFolderId },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_FOLDER },
        { name: "uri", check: v => v === null },
        { name: "title", check: v => v === TAG },
        { name: "dateAdded", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemAdded", // This is the tag.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => typeof(v) == "number" && v > 0 },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "uri", check: v => v instanceof Ci.nsIURI && v.equals(uri) },
        { name: "title", check: v => v === null },
        { name: "dateAdded", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemChanged",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "property", check: v => v === "tags" },
        { name: "isAnno", check: v => v === false },
        { name: "newValue", check: v => v === "" },
        { name: "lastModified", check: v => typeof(v) == "number" && v > 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldValue", check: v => typeof(v) == "string" },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemRemoved", // This is the tag.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => typeof(v) == "number" && v > 0 },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "uri", check: v => v instanceof Ci.nsIURI && v.equals(uri) },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemRemoved", // This is the tag folder.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => v === PlacesUtils.tagsFolderId },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_FOLDER },
        { name: "uri", check: v => v === null },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemChanged",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "property", check: v => v === "tags" },
        { name: "isAnno", check: v => v === false },
        { name: "newValue", check: v => v === "" },
        { name: "lastModified", check: v => typeof(v) == "number" && v > 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldValue", check: v => typeof(v) == "string" },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.tagging.tagURI(uri, [TAG]);
  PlacesUtils.tagging.untagURI(uri, [TAG]);
});

add_test(function onItemMoved_bookmark() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  let uri = PlacesUtils.bookmarks.getBookmarkURI(id);
  gBookmarksObserver.expected = [
    { name: "onItemMoved",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "oldParentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "oldIndex", check: v => v === 0 },
        { name: "newParentId", check: v => v === PlacesUtils.toolbarFolderId },
        { name: "newIndex", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldParentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "newParentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemMoved",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "oldParentId", check: v => v === PlacesUtils.toolbarFolderId },
        { name: "oldIndex", check: v => v === 0 },
        { name: "newParentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "newIndex", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldParentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "newParentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.moveItem(id, PlacesUtils.toolbarFolderId, 0);
  PlacesUtils.bookmarks.moveItem(id, PlacesUtils.unfiledBookmarksFolderId, 0);
});

add_test(function onItemMoved_bookmark() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  let uri = PlacesUtils.bookmarks.getBookmarkURI(id);
  gBookmarksObserver.expected = [
    { name: "onItemVisited",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "visitId", check: v => typeof(v) == "number" && v > 0 },
        { name: "time", check: v => typeof(v) == "number" && v > 0 },
        { name: "transitionType", check: v => v === PlacesUtils.history.TRANSITION_TYPED },
        { name: "uri", check: v => v instanceof Ci.nsIURI && v.equals(uri) },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
      ] },
  ];
  PlacesTestUtils.addVisits({ uri: uri, transition: TRANSITION_TYPED });
});

add_test(function onItemRemoved_bookmark() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  let uri = PlacesUtils.bookmarks.getBookmarkURI(id);
  gBookmarksObserver.expected = [
    { name: "onItemChanged", // This is an unfortunate effect of bug 653910.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "property", check: v => v === "" },
        { name: "isAnno", check: v => v === true },
        { name: "newValue", check: v => v === "" },
        { name: "lastModified", check: v => typeof(v) == "number" && v > 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldValue", check: v => typeof(v) == "string" },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemRemoved",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => v === PlacesUtils.unfiledBookmarksFolderId },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_BOOKMARK },
        { name: "uri", check: v => v instanceof Ci.nsIURI && v.equals(uri) },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.removeItem(id);
});

add_test(function onItemRemoved_separator() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  gBookmarksObserver.expected = [
    { name: "onItemChanged", // This is an unfortunate effect of bug 653910.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "property", check: v => v === "" },
        { name: "isAnno", check: v => v === true },
        { name: "newValue", check: v => v === "" },
        { name: "lastModified", check: v => typeof(v) == "number" && v > 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_SEPARATOR },
        { name: "parentId", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldValue", check: v => typeof(v) == "string" },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemRemoved",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => typeof(v) == "number" && v > 0 },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_SEPARATOR },
        { name: "uri", check: v => v === null },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.removeItem(id);
});

add_test(function onItemRemoved_folder() {
  let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUtils.unfiledBookmarksFolderId, 0);
  const TITLE = "Folder 2";
  gBookmarksObserver.expected = [
    { name: "onItemChanged", // This is an unfortunate effect of bug 653910.
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "property", check: v => v === "" },
        { name: "isAnno", check: v => v === true },
        { name: "newValue", check: v => v === "" },
        { name: "lastModified", check: v => typeof(v) == "number" && v > 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_FOLDER },
        { name: "parentId", check: v => typeof(v) == "number" && v > 0 },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "oldValue", check: v => typeof(v) == "string" },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
    { name: "onItemRemoved",
      args: [
        { name: "itemId", check: v => typeof(v) == "number" && v > 0 },
        { name: "parentId", check: v => typeof(v) == "number" && v > 0 },
        { name: "index", check: v => v === 0 },
        { name: "itemType", check: v => v === PlacesUtils.bookmarks.TYPE_FOLDER },
        { name: "uri", check: v => v === null },
        { name: "guid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "parentGuid", check: v => typeof(v) == "string" && /^[a-zA-Z0-9\-_]{12}$/.test(v) },
        { name: "source", check: v => Object.values(PlacesUtils.bookmarks.SOURCES).includes(v) },
      ] },
  ];
  PlacesUtils.bookmarks.removeItem(id);
});

function run_test() {
  PlacesUtils.bookmarks.addObserver(gBookmarksObserver, false);
  run_next_test();
}

do_register_cleanup(function () {
  PlacesUtils.bookmarks.removeObserver(gBookmarksObserver);
});
