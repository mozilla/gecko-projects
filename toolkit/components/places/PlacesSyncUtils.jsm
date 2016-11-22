/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["PlacesSyncUtils"];

const { classes: Cc, interfaces: Ci, results: Cr, utils: Cu } = Components;

Cu.importGlobalProperties(["URL", "URLSearchParams"]);

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Log",
                                  "resource://gre/modules/Log.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PlacesUtils",
                                  "resource://gre/modules/PlacesUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Preferences",
                                  "resource://gre/modules/Preferences.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Task",
                                  "resource://gre/modules/Task.jsm");

/**
 * This module exports functions for Sync to use when applying remote
 * records. The calls are similar to those in `Bookmarks.jsm` and
 * `nsINavBookmarksService`, with special handling for smart bookmarks,
 * tags, keywords, synced annotations, and missing parents.
 */
var PlacesSyncUtils = {};

const { SOURCE_SYNC } = Ci.nsINavBookmarksService;

const MICROSECONDS_PER_SECOND = 1000000;

// These are defined as lazy getters to defer initializing the bookmarks
// service until it's needed.
XPCOMUtils.defineLazyGetter(this, "ROOT_SYNC_ID_TO_GUID", () => ({
  menu: PlacesUtils.bookmarks.menuGuid,
  places: PlacesUtils.bookmarks.rootGuid,
  tags: PlacesUtils.bookmarks.tagsGuid,
  toolbar: PlacesUtils.bookmarks.toolbarGuid,
  unfiled: PlacesUtils.bookmarks.unfiledGuid,
  mobile: PlacesUtils.bookmarks.mobileGuid,
}));

XPCOMUtils.defineLazyGetter(this, "ROOT_GUID_TO_SYNC_ID", () => ({
  [PlacesUtils.bookmarks.menuGuid]: "menu",
  [PlacesUtils.bookmarks.rootGuid]: "places",
  [PlacesUtils.bookmarks.tagsGuid]: "tags",
  [PlacesUtils.bookmarks.toolbarGuid]: "toolbar",
  [PlacesUtils.bookmarks.unfiledGuid]: "unfiled",
  [PlacesUtils.bookmarks.mobileGuid]: "mobile",
}));

XPCOMUtils.defineLazyGetter(this, "ROOTS", () =>
  Object.keys(ROOT_SYNC_ID_TO_GUID)
);

const BookmarkSyncUtils = PlacesSyncUtils.bookmarks = Object.freeze({
  SMART_BOOKMARKS_ANNO: "Places/SmartBookmark",
  DESCRIPTION_ANNO: "bookmarkProperties/description",
  SIDEBAR_ANNO: "bookmarkProperties/loadInSidebar",
  SYNC_PARENT_ANNO: "sync/parent",
  SYNC_MOBILE_ROOT_ANNO: "mobile/bookmarksRoot",

  KINDS: {
    BOOKMARK: "bookmark",
    // Microsummaries were removed from Places in bug 524091. For now, Sync
    // treats them identically to bookmarks. Bug 745410 tracks removing them
    // entirely.
    MICROSUMMARY: "microsummary",
    QUERY: "query",
    FOLDER: "folder",
    LIVEMARK: "livemark",
    SEPARATOR: "separator",
  },

  get ROOTS() {
    return ROOTS;
  },

  /**
   * Converts a Places GUID to a Sync ID. Sync IDs are identical to Places
   * GUIDs for all items except roots.
   */
  guidToSyncId(guid) {
    return ROOT_GUID_TO_SYNC_ID[guid] || guid;
  },

  /**
   * Converts a Sync record ID to a Places GUID.
   */
  syncIdToGuid(syncId) {
    return ROOT_SYNC_ID_TO_GUID[syncId] || syncId;
  },

  /**
   * Fetches the sync IDs for a folder's children, ordered by their position
   * within the folder.
   */
  fetchChildSyncIds: Task.async(function* (parentSyncId) {
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(parentSyncId);
    let parentGuid = BookmarkSyncUtils.syncIdToGuid(parentSyncId);

    let db = yield PlacesUtils.promiseDBConnection();
    let childGuids = yield fetchChildGuids(db, parentGuid);
    return childGuids.map(guid =>
      BookmarkSyncUtils.guidToSyncId(guid)
    );
  }),

  /**
   * Migrates an array of `{ syncId, modified }` tuples from the old JSON-based
   * tracker to the new sync change counter. `modified` is when the change was
   * added to the old tracker, in milliseconds.
   *
   * Sync calls this method before the first bookmark sync after the Places
   * schema migration.
   */
  migrateOldTrackerEntries(entries) {
    return PlacesUtils.withConnectionWrapper(
      "BookmarkSyncUtils: migrateOldTrackerEntries", function(db) {
        return db.executeTransaction(function* () {
          // Mark all existing bookmarks as synced, and clear their change
          // counters to avoid a full upload on the next sync. Note that
          // this means we'll miss changes made between startup and the first
          // post-migration sync, as well as changes made on a new release
          // channel that weren't synced before the user downgraded. This is
          // unfortunate, but no worse than the behavior of the old tracker.
          //
          // We also likely have bookmarks that don't exist on the server,
          // because the old tracker missed them. We'll eventually fix the
          // server once we decide on a repair strategy.
          yield db.executeCached(`
            WITH RECURSIVE
            syncedItems(id) AS (
              SELECT b.id FROM moz_bookmarks b
              WHERE b.guid IN ('menu________', 'toolbar_____', 'unfiled_____',
                               'mobile______')
              UNION ALL
              SELECT b.id FROM moz_bookmarks b
              JOIN syncedItems s ON b.parent = s.id
            )
            UPDATE moz_bookmarks SET
              syncStatus = :syncStatus,
              syncChangeCounter = 0
            WHERE id IN syncedItems`,
            { syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NORMAL });

          yield db.executeCached(`DELETE FROM moz_bookmarks_deleted`);

          yield db.executeCached(`CREATE TEMP TABLE moz_bookmarks_tracked (
            guid TEXT PRIMARY KEY,
            time INTEGER
          )`);

          try {
            for (let { syncId, modified } of entries) {
              let guid = BookmarkSyncUtils.syncIdToGuid(syncId);
              if (!PlacesUtils.isValidGuid(guid)) {
                BookmarkSyncLog.warn(`migrateOldTrackerEntries: Ignoring ` +
                                     `change for invalid item ${guid}`);
                continue;
              }
              let time = PlacesUtils.toPRTime(Number.isFinite(modified) ?
                                              modified : Date.now());
              yield db.executeCached(`
                INSERT OR IGNORE INTO moz_bookmarks_tracked (guid, time)
                VALUES (:guid, :time)`,
                { guid, time });
            }

            // Bump the change counter for existing tracked items.
            yield db.executeCached(`
              INSERT OR REPLACE INTO moz_bookmarks (id, fk, type, parent,
                                                    position, title,
                                                    dateAdded, lastModified,
                                                    guid, syncChangeCounter,
                                                    syncStatus)
              SELECT b.id, b.fk, b.type, b.parent, b.position, b.title,
                     b.dateAdded, MAX(b.lastModified, t.time), b.guid,
                     b.syncChangeCounter + 1, b.syncStatus
              FROM moz_bookmarks b
              JOIN moz_bookmarks_tracked t ON b.guid = t.guid`);

            // Insert tombstones for nonexistent tracked items, using the most
            // recent deletion date for more accurate reconciliation. We assume
            // the tracked item belongs to a synced root.
            yield db.executeCached(`
              INSERT OR REPLACE INTO moz_bookmarks_deleted (guid, dateRemoved)
              SELECT t.guid, MAX(IFNULL((SELECT dateRemoved FROM moz_bookmarks_deleted
                                         WHERE guid = t.guid), 0), t.time)
              FROM moz_bookmarks_tracked t
              LEFT JOIN moz_bookmarks b ON t.guid = b.guid
              WHERE b.guid IS NULL`);
          } finally {
            yield db.executeCached(`DROP TABLE moz_bookmarks_tracked`);
          }
        });
      }
    );
  },

  /**
   * Reorders a folder's children, based on their order in the array of sync
   * IDs.
   *
   * Sync uses this method to reorder all synced children after applying all
   * incoming records.
   *
   */
  order: Task.async(function* (parentSyncId, childSyncIds) {
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(parentSyncId);
    if (!childSyncIds.length) {
      return undefined;
    }
    let parentGuid = BookmarkSyncUtils.syncIdToGuid(parentSyncId);
    if (parentGuid == PlacesUtils.bookmarks.rootGuid) {
      // Reordering roots doesn't make sense, but Sync will do this on the
      // first sync.
      return undefined;
    }
    let orderedChildrenGuids = childSyncIds.map(BookmarkSyncUtils.syncIdToGuid);
    return PlacesUtils.bookmarks.reorder(parentGuid, orderedChildrenGuids,
                                         { source: SOURCE_SYNC });
  }),

  /**
   * Returns a changeset containing local bookmark changes since the last sync.
   * Updates the sync status of all "NEW" bookmarks to "NORMAL", so that Sync
   * can recover correctly after an interrupted sync.
   *
   * @return {Promise} resolved once all items have been fetched.
   * @resolves to an object containing records for changed bookmarks, keyed by
   *           the sync ID.
   * @see pullSyncChanges for the implementation, and markChangesAsSyncing for
   *      an explanation of why we update the sync status.
   */
  pullChanges() {
    return PlacesUtils.withConnectionWrapper("BookmarkSyncUtils: pullChanges",
      db => pullSyncChanges(db));
  },

  /**
   * Decrements the sync change counter, updates the sync status, and cleans up
   * tombstones for successfully synced items. Sync calls this method at the
   * end of each bookmark sync.
   *
   * @param changeRecords
   *        A changeset containing sync change records, as returned by
   *        `pull{All, New}Changes`.
   * @return {Promise} resolved once all records have been updated.
   */
  pushChanges(changeRecords) {
    return PlacesUtils.withConnectionWrapper(
      "BookmarkSyncUtils.pushChanges", Task.async(function* (db) {
        let skippedCount = 0;
        let syncedTombstoneGuids = [];
        let syncedChanges = [];

        for (let syncId in changeRecords) {
          // Validate change records to catch coding errors.
          let changeRecord = validateChangeRecord(changeRecords[syncId], {
            tombstone: { required: true },
            counter: { required: true },
            synced: { required: true },
          });

          // Sync sets the `synced` flag for reconciled or successfully
          // uploaded items. If upload failed, ignore the change; we'll
          // try again on the next sync.
          if (!changeRecord.synced) {
            skippedCount++;
            continue;
          }

          let guid = BookmarkSyncUtils.syncIdToGuid(syncId);
          if (changeRecord.tombstone) {
            syncedTombstoneGuids.push(guid);
          } else {
            syncedChanges.push([guid, changeRecord]);
          }
        }

        if (syncedChanges.length || syncedTombstoneGuids.length) {
          yield db.executeTransaction(function* () {
            for (let [guid, changeRecord] of syncedChanges) {
              // Reduce the change counter and update the sync status for
              // reconciled and uploaded items. If the bookmark was updated
              // during the sync, its change counter will still be > 0 for the
              // next sync.
              yield db.executeCached(`
                UPDATE moz_bookmarks
                SET syncChangeCounter = MAX(syncChangeCounter - :syncChangeDelta, 0),
                    syncStatus = :syncStatus
                WHERE guid = :guid`,
                { guid, syncChangeDelta: changeRecord.counter,
                  syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NORMAL });
            }

            yield removeTombstones(db, syncedTombstoneGuids);
          });
        }

        BookmarkSyncLog.debug(`pushChanges: Processed change records`,
                              { skipped: skippedCount,
                                updated: syncedChanges.length,
                                tombstones: syncedTombstoneGuids.length });
      })
    );
  },

  /**
   * Removes items from the database. Sync buffers incoming tombstones, and
   * calls this method to apply them at the end of each sync. Deletion
   * happens in three steps:
   *
   *  1. Remove all non-folder items. Deleting a folder on a remote client
   *     uploads tombstones for the folder and its children at the time of
   *     deletion. This preserves any new children we've added locally since
   *     the last sync.
   *  2. Reparent remaining children to the tombstoned folder's parent. This
   *     bumps the change counter for the children and their new parent.
   *  3. Remove the tombstoned folder. Because we don't do this in a
   *     transaction, the user might move new items into the folder before we
   *     can remove it. In that case, we keep the folder and upload the new
   *     subtree to the server.
   *
   * See the comment above `BookmarksStore::deletePending` for the details on
   * why delete works the way it does.
   */
  remove: Task.async(function* (syncIds) {
    if (!syncIds.length) {
      return null;
    }

    let folderGuids = [];
    for (let syncId of syncIds) {
      if (syncId in ROOT_SYNC_ID_TO_GUID) {
        BookmarkSyncLog.warn(`remove: Refusing to remove root ${syncId}`);
        continue;
      }
      let guid = BookmarkSyncUtils.syncIdToGuid(syncId);
      let bookmarkItem = yield PlacesUtils.bookmarks.fetch(guid);
      if (!bookmarkItem) {
        BookmarkSyncLog.trace(`remove: Item ${guid} already removed`);
        continue;
      }
      let kind = yield getKindForItem(bookmarkItem);
      if (kind == BookmarkSyncUtils.KINDS.FOLDER) {
        folderGuids.push(bookmarkItem.guid);
        continue;
      }
      let wasRemoved = yield deleteSyncedAtom(bookmarkItem);
      if (wasRemoved) {
         BookmarkSyncLog.trace(`remove: Removed item ${guid} with ` +
                               `kind ${kind}`);
      }
    }

    for (let guid of folderGuids) {
      let bookmarkItem = yield PlacesUtils.bookmarks.fetch(guid);
      if (!bookmarkItem) {
        BookmarkSyncLog.trace(`remove: Folder ${guid} already removed`);
        continue;
      }
      let wasRemoved = yield deleteSyncedFolder(bookmarkItem);
      if (wasRemoved) {
        BookmarkSyncLog.trace(`remove: Removed folder ${bookmarkItem.guid}`);
      }
    }

    // TODO (Bug 1313890): Refactor the bookmarks engine to pull change records
    // before uploading, instead of returning records to merge into the engine's
    // initial changeset.
    return PlacesUtils.withConnectionWrapper("BookmarkSyncUtils: remove",
      db => pullSyncChanges(db));
  }),

  /**
   * Increments the change counter of a non-folder item and its parent. Sync
   * calls this method to override a remote deletion for an item that's changed
   * locally.
   *
   * @param syncId
   *        The sync ID to revive.
   * @return {Promise} resolved once the change counters have been updated.
   * @resolves to `null` if the item doesn't exist or is a folder. Otherwise,
   *           resolves to an object containing new change records for the item
   *           and its parent. The bookmarks engine merges these records into
   *           the changeset for the current sync.
   */
  touch: Task.async(function* (syncId) {
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(syncId);
    let guid = BookmarkSyncUtils.syncIdToGuid(syncId);

    let bookmarkItem = yield PlacesUtils.bookmarks.fetch(guid);
    if (!bookmarkItem) {
      return null;
    }
    let kind = yield getKindForItem(bookmarkItem);
    if (kind == BookmarkSyncUtils.KINDS.FOLDER) {
      // We avoid reviving folders since reviving them properly would require
      // reviving their children as well. Unfortunately, this is the wrong
      // choice in the case of a bookmark restore where the bookmarks engine
      // fails to wipe the server. In that case, if the server has the folder
      // as deleted, we *would* want to reupload this folder. This is mitigated
      // by the fact that `remove` moves any undeleted children to the
      // grandparent when deleting the parent.
      return null;
    }
    return PlacesUtils.withConnectionWrapper("BookmarkSyncUtils: touch",
      db => touchSyncBookmark(db, bookmarkItem));
  }),

  /**
   * Returns true for sync IDs that are considered roots.
   */
  isRootSyncID(syncID) {
    return ROOT_SYNC_ID_TO_GUID.hasOwnProperty(syncID);
  },

  /**
   * Removes all bookmarks and tombstones from the database. Sync calls this
   * method when it receives a command from a remote client to wipe all stored
   * data, or when replacing stored data with remote data on a first sync.
   *
   * @return {Promise} resolved once all items have been removed.
   */
  wipe: Task.async(function* () {
    // Remove all children from all roots.
    yield PlacesUtils.bookmarks.eraseEverything({
      source: SOURCE_SYNC,
    });
    // Remove tombstones and reset change tracking info for the roots.
    yield BookmarkSyncUtils.reset();
  }),

  /**
   * Marks all bookmarks as "NEW" and removes all tombstones. Unlike `wipe`,
   * this keeps all existing bookmarks, and only clears their sync change
   * tracking info.
   *
   * @return {Promise} resolved once all items have been updated.
   */
  reset: Task.async(function* () {
    return PlacesUtils.withConnectionWrapper(
      "BookmarkSyncUtils: reset", function(db) {
        return db.executeTransaction(function* () {
          // Reset change counters and statuses for all bookmarks.
          yield db.executeCached(`
            UPDATE moz_bookmarks
            SET syncChangeCounter = 1,
                syncStatus = :syncStatus`,
            { syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NEW });

          // The orphan anno isn't meaningful when Sync is disconnected.
          yield db.execute(`
            DELETE FROM moz_items_annos
            WHERE anno_attribute_id = (SELECT id FROM moz_anno_attributes
                                       WHERE name = :orphanAnno)`,
            { orphanAnno: BookmarkSyncUtils.SYNC_PARENT_ANNO });

          // Drop stale tombstones.
          yield db.executeCached("DELETE FROM moz_bookmarks_deleted");
        });
      }
    );
  }),

  /**
   * De-dupes an item by changing its sync ID to match the ID on the server.
   * Sync calls this method when it detects an incoming item is a duplicate of
   * an existing local item.
   *
   * Note that this method doesn't move the item if the local and remote sync
   * IDs are different. That happens after de-duping, when the bookmarks engine
   * calls `update` to update the item.
   *
   * @param localSyncId
   *        The local ID to change.
   * @param remoteSyncId
   *        The remote ID that should replace the local ID.
   * @param remoteParentSyncId
   *        The remote record's parent ID.
   * @return {Promise} resolved once the ID has been changed.
   * @resolves to an object containing new change records for the old item,
   *           the local parent, and the remote parent if different from the
   *           local parent. The bookmarks engine merges these records into the
   *           changeset for the current sync.
   */
  dedupe: Task.async(function* (localSyncId, remoteSyncId, remoteParentSyncId) {
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(localSyncId);
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(remoteSyncId);
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(remoteParentSyncId);

    return PlacesUtils.withConnectionWrapper("BookmarkSyncUtils: dedupe", db =>
      dedupeSyncBookmark(db, BookmarkSyncUtils.syncIdToGuid(localSyncId),
                         BookmarkSyncUtils.syncIdToGuid(remoteSyncId),
                         BookmarkSyncUtils.syncIdToGuid(remoteParentSyncId))
    );
  }),

  /**
   * Updates a bookmark with synced properties. Only Sync should call this
   * method; other callers should use `Bookmarks.update`.
   *
   * The following properties are supported:
   *  - kind: Optional.
   *  - guid: Required.
   *  - parentGuid: Optional; reparents the bookmark if specified.
   *  - title: Optional.
   *  - url: Optional.
   *  - tags: Optional; replaces all existing tags.
   *  - keyword: Optional.
   *  - description: Optional.
   *  - loadInSidebar: Optional.
   *  - query: Optional.
   *
   * @param info
   *        object representing a bookmark-item, as defined above.
   *
   * @return {Promise} resolved when the update is complete.
   * @resolves to an object representing the updated bookmark.
   * @rejects if it's not possible to update the given bookmark.
   * @throws if the arguments are invalid.
   */
  update: Task.async(function* (info) {
    let updateInfo = validateSyncBookmarkObject(info,
      { syncId: { required: true }
      });

    return updateSyncBookmark(updateInfo);
  }),

  /**
   * Inserts a synced bookmark into the tree. Only Sync should call this
   * method; other callers should use `Bookmarks.insert`.
   *
   * The following properties are supported:
   *  - kind: Required.
   *  - guid: Required.
   *  - parentGuid: Required.
   *  - url: Required for bookmarks.
   *  - query: A smart bookmark query string, optional.
   *  - tags: An optional array of tag strings.
   *  - keyword: An optional keyword string.
   *  - description: An optional description string.
   *  - loadInSidebar: An optional boolean; defaults to false.
   *
   * Sync doesn't set the index, since it appends and reorders children
   * after applying all incoming items.
   *
   * @param info
   *        object representing a synced bookmark.
   *
   * @return {Promise} resolved when the creation is complete.
   * @resolves to an object representing the created bookmark.
   * @rejects if it's not possible to create the requested bookmark.
   * @throws if the arguments are invalid.
   */
  insert: Task.async(function* (info) {
    let insertInfo = validateNewBookmark(info);
    return insertSyncBookmark(insertInfo);
  }),

  /**
   * Fetches a Sync bookmark object for an item in the tree. The object contains
   * the following properties, depending on the item's kind:
   *
   *  - kind (all): A string representing the item's kind.
   *  - syncId (all): The item's sync ID.
   *  - parentSyncId (all): The sync ID of the item's parent.
   *  - parentTitle (all): The title of the item's parent, used for de-duping.
   *    Omitted for the Places root and parents with empty titles.
   *  - title ("bookmark", "folder", "livemark", "query"): The item's title.
   *    Omitted if empty.
   *  - url ("bookmark", "query"): The item's URL.
   *  - tags ("bookmark", "query"): An array containing the item's tags.
   *  - keyword ("bookmark"): The bookmark's keyword, if one exists.
   *  - description ("bookmark", "folder", "livemark"): The item's description.
   *    Omitted if one isn't set.
   *  - loadInSidebar ("bookmark", "query"): Whether to load the bookmark in
   *    the sidebar. Always `false` for queries.
   *  - feed ("livemark"): A `URL` object pointing to the livemark's feed URL.
   *  - site ("livemark"): A `URL` object pointing to the livemark's site URL,
   *    or `null` if one isn't set.
   *  - childSyncIds ("folder"): An array containing the sync IDs of the item's
   *    children, used to determine child order.
   *  - folder ("query"): The tag folder name, if this is a tag query.
   *  - query ("query"): The smart bookmark query name, if this is a smart
   *    bookmark.
   *  - index ("separator"): The separator's position within its parent.
   */
  fetch: Task.async(function* (syncId) {
    let guid = BookmarkSyncUtils.syncIdToGuid(syncId);
    let bookmarkItem = yield PlacesUtils.bookmarks.fetch(guid);
    if (!bookmarkItem) {
      return null;
    }

    // Convert the Places bookmark object to a Sync bookmark and add
    // kind-specific properties. Titles are required for bookmarks,
    // folders, and livemarks; optional for queries, and omitted for
    // separators.
    let kind = yield getKindForItem(bookmarkItem);
    let item;
    switch (kind) {
      case BookmarkSyncUtils.KINDS.BOOKMARK:
      case BookmarkSyncUtils.KINDS.MICROSUMMARY:
        item = yield fetchBookmarkItem(bookmarkItem);
        break;

      case BookmarkSyncUtils.KINDS.QUERY:
        item = yield fetchQueryItem(bookmarkItem);
        break;

      case BookmarkSyncUtils.KINDS.FOLDER:
        item = yield fetchFolderItem(bookmarkItem);
        break;

      case BookmarkSyncUtils.KINDS.LIVEMARK:
        item = yield fetchLivemarkItem(bookmarkItem);
        break;

      case BookmarkSyncUtils.KINDS.SEPARATOR:
        item = yield placesBookmarkToSyncBookmark(bookmarkItem);
        item.index = bookmarkItem.index;
        break;

      default:
        throw new Error(`Unknown bookmark kind: ${kind}`);
    }

    // Sync uses the parent title for de-duping. All Sync bookmark objects
    // except the Places root should have this property.
    if (bookmarkItem.parentGuid) {
      let parent = yield PlacesUtils.bookmarks.fetch(bookmarkItem.parentGuid);
      item.parentTitle = parent.title || "";
    }

    return item;
  }),

  /**
   * Get the sync record kind for the record with provided sync id.
   *
   * @param syncId
   *        Sync ID for the item in question
   *
   * @returns {Promise} A promise that resolves with the sync record kind (e.g.
   *                    something under `PlacesSyncUtils.bookmarks.KIND`), or
   *                    with `null` if no item with that guid exists.
   * @throws if `guid` is invalid.
   */
  getKindForSyncId(syncId) {
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS.syncId(syncId);
    let guid = BookmarkSyncUtils.syncIdToGuid(syncId);
    return PlacesUtils.bookmarks.fetch(guid)
    .then(item => {
      if (!item) {
        return null;
      }
      return getKindForItem(item)
    });
  },

  /**
   * Returns the sync change counter increment for a change source constant.
   */
  determineSyncChangeDelta(source) {
    // Don't bump the change counter when applying changes made by Sync, to
    // avoid sync loops.
    return source == PlacesUtils.bookmarks.SOURCES.SYNC ? 0 : 1;
  },

  /**
   * Returns the sync status for a new item inserted by a change source.
   */
  determineInitialSyncStatus(source) {
    if (source == PlacesUtils.bookmarks.SOURCES.SYNC) {
      // Incoming bookmarks are "NORMAL", since they already exist on the server.
      return PlacesUtils.bookmarks.SYNC_STATUS.NORMAL;
    }
    if (source == PlacesUtils.bookmarks.SOURCES.IMPORT_REPLACE) {
      // If the user restores from a backup, or Places automatically recovers
      // from a corrupt database, all prior sync tracking is lost. Setting the
      // status to "UNKNOWN" allows Sync to reconcile restored bookmarks with
      // those on the server.
      return PlacesUtils.bookmarks.SYNC_STATUS.UNKNOWN;
    }
    // For all other sources, mark items as "NEW". We'll update their statuses
    // to "NORMAL" after the first sync.
    return PlacesUtils.bookmarks.SYNC_STATUS.NEW;
  },

  /**
   * An internal helper that bumps the change counter for all bookmarks with
   * a given URL. This is used to update bookmarks when adding or changing a
   * tag or keyword entry.
   *
   * @param db
   *        the Sqlite.jsm connection handle.
   * @param url
   *        the bookmark URL object.
   * @param syncChangeDelta
   *        the sync change counter increment.
   * @return {Promise} resolved when the counters have been updated.
   */
  addSyncChangesForBookmarksWithURL(db, url, syncChangeDelta) {
    if (!url || !syncChangeDelta) {
      return Promise.resolve();
    }
    return db.executeCached(`
      UPDATE moz_bookmarks
        SET syncChangeCounter = syncChangeCounter + :syncChangeDelta
      WHERE type = :type AND
            fk = (SELECT id FROM moz_places WHERE url_hash = hash(:url) AND
                  url = :url)`,
      { syncChangeDelta, type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        url: url.href });
  },
});

XPCOMUtils.defineLazyGetter(this, "BookmarkSyncLog", () => {
  return Log.repository.getLogger("BookmarkSyncUtils");
});

function validateSyncBookmarkObject(input, behavior) {
  return PlacesUtils.validateItemProperties(
    PlacesUtils.SYNC_BOOKMARK_VALIDATORS, input, behavior);
}

// Validates a sync change record as returned by `pullChanges` and passed to
// `pushChanges`.
function validateChangeRecord(changeRecord, behavior) {
  return PlacesUtils.validateItemProperties(
    PlacesUtils.SYNC_CHANGE_RECORD_VALIDATORS, changeRecord, behavior);
}

// Similar to the private `fetchBookmarksByParent` implementation in
// `Bookmarks.jsm`.
var fetchChildGuids = Task.async(function* (db, parentGuid) {
  let rows = yield db.executeCached(`
    SELECT guid
    FROM moz_bookmarks
    WHERE parent = (
      SELECT id FROM moz_bookmarks WHERE guid = :parentGuid
    )
    ORDER BY position`,
    { parentGuid }
  );
  return rows.map(row => row.getResultByName("guid"));
});

// A helper for whenever we want to know if a GUID doesn't exist in the places
// database. Primarily used to detect orphans on incoming records.
var GUIDMissing = Task.async(function* (guid) {
  try {
    yield PlacesUtils.promiseItemId(guid);
    return false;
  } catch (ex) {
    if (ex.message == "no item found for the given GUID") {
      return true;
    }
    throw ex;
  }
});

// Tag queries use a `place:` URL that refers to the tag folder ID. When we
// apply a synced tag query from a remote client, we need to update the URL to
// point to the local tag folder.
var updateTagQueryFolder = Task.async(function* (info) {
  if (info.kind != BookmarkSyncUtils.KINDS.QUERY || !info.folder || !info.url ||
      info.url.protocol != "place:") {
    return info;
  }

  let params = new URLSearchParams(info.url.pathname);
  let type = +params.get("type");

  if (type != Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAG_CONTENTS) {
    return info;
  }

  let id = yield getOrCreateTagFolder(info.folder);
  BookmarkSyncLog.debug(`updateTagQueryFolder: Tag query folder: ${
    info.folder} = ${id}`);

  // Rewrite the query to reference the new ID.
  params.set("folder", id);
  info.url = new URL(info.url.protocol + params);

  return info;
});

var annotateOrphan = Task.async(function* (item, requestedParentSyncId) {
  let guid = BookmarkSyncUtils.syncIdToGuid(item.syncId);
  let itemId = yield PlacesUtils.promiseItemId(guid);
  PlacesUtils.annotations.setItemAnnotation(itemId,
    BookmarkSyncUtils.SYNC_PARENT_ANNO, requestedParentSyncId, 0,
    PlacesUtils.annotations.EXPIRE_NEVER,
    SOURCE_SYNC);
});

var reparentOrphans = Task.async(function* (item) {
  if (item.kind != BookmarkSyncUtils.KINDS.FOLDER) {
    return;
  }
  let orphanGuids = yield fetchGuidsWithAnno(BookmarkSyncUtils.SYNC_PARENT_ANNO,
                                             item.syncId);
  let folderGuid = BookmarkSyncUtils.syncIdToGuid(item.syncId);
  BookmarkSyncLog.debug(`reparentOrphans: Reparenting ${
    JSON.stringify(orphanGuids)} to ${item.syncId}`);
  for (let i = 0; i < orphanGuids.length; ++i) {
    try {
      // Reparenting can fail if we have a corrupted or incomplete tree
      // where an item's parent is one of its descendants.
      BookmarkSyncLog.trace(`reparentOrphans: Attempting to move item ${
        orphanGuids[i]} to new parent ${item.syncId}`);
      yield PlacesUtils.bookmarks.update({
        guid: orphanGuids[i],
        parentGuid: folderGuid,
        index: PlacesUtils.bookmarks.DEFAULT_INDEX,
        source: SOURCE_SYNC,
      });
    } catch (ex) {
      BookmarkSyncLog.error(`reparentOrphans: Failed to reparent item ${
        orphanGuids[i]} to ${item.syncId}`, ex);
    }
  }
});

// Inserts a synced bookmark into the database.
var insertSyncBookmark = Task.async(function* (insertInfo) {
  let requestedParentSyncId = insertInfo.parentSyncId;
  let requestedParentGuid =
    BookmarkSyncUtils.syncIdToGuid(insertInfo.parentSyncId);
  let isOrphan = yield GUIDMissing(requestedParentGuid);

  // Default to "unfiled" for new bookmarks if the parent doesn't exist.
  if (!isOrphan) {
    BookmarkSyncLog.debug(`insertSyncBookmark: Item ${
      insertInfo.syncId} is not an orphan`);
  } else {
    BookmarkSyncLog.debug(`insertSyncBookmark: Item ${
      insertInfo.syncId} is an orphan: parent ${
      insertInfo.parentSyncId} doesn't exist; reparenting to unfiled`);
    insertInfo.parentSyncId = "unfiled";
  }

  // If we're inserting a tag query, make sure the tag exists and fix the
  // folder ID to refer to the local tag folder.
  insertInfo = yield updateTagQueryFolder(insertInfo);

  let newItem;
  if (insertInfo.kind == BookmarkSyncUtils.KINDS.LIVEMARK) {
    newItem = yield insertSyncLivemark(insertInfo);
  } else {
    let bookmarkInfo = syncBookmarkToPlacesBookmark(insertInfo);
    let bookmarkItem = yield PlacesUtils.bookmarks.insert(bookmarkInfo);
    newItem = yield insertBookmarkMetadata(bookmarkItem, insertInfo);
  }

  if (!newItem) {
    return null;
  }

  // If the item is an orphan, annotate it with its real parent sync ID.
  if (isOrphan) {
    yield annotateOrphan(newItem, requestedParentSyncId);
  }

  // Reparent all orphans that expect this folder as the parent.
  yield reparentOrphans(newItem);

  return newItem;
});

// Inserts a synced livemark.
var insertSyncLivemark = Task.async(function* (insertInfo) {
  if (!insertInfo.feed) {
    BookmarkSyncLog.debug(`insertSyncLivemark: ${
      insertInfo.syncId} missing feed URL`);
    return null;
  }
  let livemarkInfo = syncBookmarkToPlacesBookmark(insertInfo);
  let parentIsLivemark = yield getAnno(livemarkInfo.parentGuid,
                                       PlacesUtils.LMANNO_FEEDURI);
  if (parentIsLivemark) {
    // A livemark can't be a descendant of another livemark.
    BookmarkSyncLog.debug(`insertSyncLivemark: Invalid parent ${
      insertInfo.parentSyncId}; skipping livemark record ${
      insertInfo.syncId}`);
    return null;
  }

  let livemarkItem = yield PlacesUtils.livemarks.addLivemark(livemarkInfo);

  return insertBookmarkMetadata(livemarkItem, insertInfo);
});

// Sets annotations, keywords, and tags on a new bookmark. Returns a Sync
// bookmark object.
var insertBookmarkMetadata = Task.async(function* (bookmarkItem, insertInfo) {
  let itemId = yield PlacesUtils.promiseItemId(bookmarkItem.guid);
  let newItem = yield placesBookmarkToSyncBookmark(bookmarkItem);

  if (insertInfo.query) {
    PlacesUtils.annotations.setItemAnnotation(itemId,
      BookmarkSyncUtils.SMART_BOOKMARKS_ANNO, insertInfo.query, 0,
      PlacesUtils.annotations.EXPIRE_NEVER,
      SOURCE_SYNC);
    newItem.query = insertInfo.query;
  }

  try {
    newItem.tags = yield tagItem(bookmarkItem, insertInfo.tags);
  } catch (ex) {
    BookmarkSyncLog.warn(`insertBookmarkMetadata: Error tagging item ${
      insertInfo.syncId}`, ex);
  }

  if (insertInfo.keyword) {
    yield PlacesUtils.keywords.insert({
      keyword: insertInfo.keyword,
      url: bookmarkItem.url.href,
      source: SOURCE_SYNC,
    });
    newItem.keyword = insertInfo.keyword;
  }

  if (insertInfo.description) {
    PlacesUtils.annotations.setItemAnnotation(itemId,
      BookmarkSyncUtils.DESCRIPTION_ANNO, insertInfo.description, 0,
      PlacesUtils.annotations.EXPIRE_NEVER,
      SOURCE_SYNC);
    newItem.description = insertInfo.description;
  }

  if (insertInfo.loadInSidebar) {
    PlacesUtils.annotations.setItemAnnotation(itemId,
      BookmarkSyncUtils.SIDEBAR_ANNO, insertInfo.loadInSidebar, 0,
      PlacesUtils.annotations.EXPIRE_NEVER,
      SOURCE_SYNC);
    newItem.loadInSidebar = insertInfo.loadInSidebar;
  }

  return newItem;
});

// Determines the Sync record kind for an existing bookmark.
var getKindForItem = Task.async(function* (item) {
  switch (item.type) {
    case PlacesUtils.bookmarks.TYPE_FOLDER: {
      let isLivemark = yield getAnno(item.guid,
                                     PlacesUtils.LMANNO_FEEDURI);
      return isLivemark ? BookmarkSyncUtils.KINDS.LIVEMARK :
                          BookmarkSyncUtils.KINDS.FOLDER;
    }
    case PlacesUtils.bookmarks.TYPE_BOOKMARK:
      return item.url.protocol == "place:" ?
             BookmarkSyncUtils.KINDS.QUERY :
             BookmarkSyncUtils.KINDS.BOOKMARK;

    case PlacesUtils.bookmarks.TYPE_SEPARATOR:
      return BookmarkSyncUtils.KINDS.SEPARATOR;
  }
  return null;
});

// Returns the `nsINavBookmarksService` bookmark type constant for a Sync
// record kind.
function getTypeForKind(kind) {
  switch (kind) {
    case BookmarkSyncUtils.KINDS.BOOKMARK:
    case BookmarkSyncUtils.KINDS.MICROSUMMARY:
    case BookmarkSyncUtils.KINDS.QUERY:
      return PlacesUtils.bookmarks.TYPE_BOOKMARK;

    case BookmarkSyncUtils.KINDS.FOLDER:
    case BookmarkSyncUtils.KINDS.LIVEMARK:
      return PlacesUtils.bookmarks.TYPE_FOLDER;

    case BookmarkSyncUtils.KINDS.SEPARATOR:
      return PlacesUtils.bookmarks.TYPE_SEPARATOR;
  }
  throw new Error(`Unknown bookmark kind: ${kind}`);
}

// Determines if a livemark should be reinserted. Returns true if `updateInfo`
// specifies different feed or site URLs; false otherwise.
var shouldReinsertLivemark = Task.async(function* (updateInfo) {
  let hasFeed = updateInfo.hasOwnProperty("feed");
  let hasSite = updateInfo.hasOwnProperty("site");
  if (!hasFeed && !hasSite) {
    return false;
  }
  let guid = BookmarkSyncUtils.syncIdToGuid(updateInfo.syncId);
  let livemark = yield PlacesUtils.livemarks.getLivemark({
    guid,
  });
  if (hasFeed) {
    let feedURI = PlacesUtils.toURI(updateInfo.feed);
    if (!livemark.feedURI.equals(feedURI)) {
      return true;
    }
  }
  if (hasSite) {
    if (!updateInfo.site) {
      return !!livemark.siteURI;
    }
    let siteURI = PlacesUtils.toURI(updateInfo.site);
    if (!livemark.siteURI || !siteURI.equals(livemark.siteURI)) {
      return true;
    }
  }
  return false;
});

var updateSyncBookmark = Task.async(function* (updateInfo) {
  let guid = BookmarkSyncUtils.syncIdToGuid(updateInfo.syncId);
  let oldBookmarkItem = yield PlacesUtils.bookmarks.fetch(guid);
  if (!oldBookmarkItem) {
    throw new Error(`Bookmark with sync ID ${
      updateInfo.syncId} does not exist`);
  }

  let shouldReinsert = false;
  let oldKind = yield getKindForItem(oldBookmarkItem);
  if (updateInfo.hasOwnProperty("kind") && updateInfo.kind != oldKind) {
    // If the item's aren't the same kind, we can't update the record;
    // we must remove and reinsert.
    shouldReinsert = true;
    if (BookmarkSyncLog.level <= Log.Level.Warn) {
      let oldSyncId = BookmarkSyncUtils.guidToSyncId(oldBookmarkItem.guid);
      BookmarkSyncLog.warn(`updateSyncBookmark: Local ${
        oldSyncId} kind = ${oldKind}; remote ${
        updateInfo.syncId} kind = ${
        updateInfo.kind}. Deleting and recreating`);
    }
  } else if (oldKind == BookmarkSyncUtils.KINDS.LIVEMARK) {
    // Similarly, if we're changing a livemark's site or feed URL, we need to
    // reinsert.
    shouldReinsert = yield shouldReinsertLivemark(updateInfo);
    if (BookmarkSyncLog.level <= Log.Level.Debug) {
      let oldSyncId = BookmarkSyncUtils.guidToSyncId(oldBookmarkItem.guid);
      BookmarkSyncLog.debug(`updateSyncBookmark: Local ${
        oldSyncId} and remote ${
        updateInfo.syncId} livemarks have different URLs`);
    }
  }

  if (shouldReinsert) {
    let newInfo = validateNewBookmark(updateInfo);
    yield PlacesUtils.bookmarks.remove({
      guid,
      source: SOURCE_SYNC,
    });
    // A reinsertion likely indicates a confused client, since there aren't
    // public APIs for changing livemark URLs or an item's kind (e.g., turning
    // a folder into a separator while preserving its annos and position).
    // This might be a good case to repair later; for now, we assume Sync has
    // passed a complete record for the new item, and don't try to merge
    // `oldBookmarkItem` with `updateInfo`.
    return insertSyncBookmark(newInfo);
  }

  let isOrphan = false, requestedParentSyncId;
  if (updateInfo.hasOwnProperty("parentSyncId")) {
    requestedParentSyncId = updateInfo.parentSyncId;
    let oldParentSyncId =
      BookmarkSyncUtils.guidToSyncId(oldBookmarkItem.parentGuid);
    if (requestedParentSyncId != oldParentSyncId) {
      let oldId = yield PlacesUtils.promiseItemId(oldBookmarkItem.guid);
      if (PlacesUtils.isRootItem(oldId)) {
        throw new Error(`Cannot move Places root ${oldId}`);
      }
      let requestedParentGuid =
        BookmarkSyncUtils.syncIdToGuid(requestedParentSyncId);
      isOrphan = yield GUIDMissing(requestedParentGuid);
      if (!isOrphan) {
        BookmarkSyncLog.debug(`updateSyncBookmark: Item ${
          updateInfo.syncId} is not an orphan`);
      } else {
        // Don't move the item if the new parent doesn't exist. Instead, mark
        // the item as an orphan. We'll annotate it with its real parent after
        // updating.
        BookmarkSyncLog.trace(`updateSyncBookmark: Item ${
          updateInfo.syncId} is an orphan: could not find parent ${
          requestedParentSyncId}`);
        delete updateInfo.parentSyncId;
      }
    } else {
      // If the parent is the same, just omit it so that `update` doesn't do
      // extra work.
      delete updateInfo.parentSyncId;
    }
  }

  updateInfo = yield updateTagQueryFolder(updateInfo);

  let bookmarkInfo = syncBookmarkToPlacesBookmark(updateInfo);
  let newBookmarkItem = shouldUpdateBookmark(bookmarkInfo) ?
                        yield PlacesUtils.bookmarks.update(bookmarkInfo) :
                        oldBookmarkItem;
  let newItem = yield updateBookmarkMetadata(oldBookmarkItem, newBookmarkItem,
                                             updateInfo);

  // If the item is an orphan, annotate it with its real parent sync ID.
  if (isOrphan) {
    yield annotateOrphan(newItem, requestedParentSyncId);
  }

  // Reparent all orphans that expect this folder as the parent.
  yield reparentOrphans(newItem);

  return newItem;
});

// Updates tags, keywords, and annotations for an existing bookmark. Returns a
// Sync bookmark object.
var updateBookmarkMetadata = Task.async(function* (oldBookmarkItem,
                                                   newBookmarkItem,
                                                   updateInfo) {
  let itemId = yield PlacesUtils.promiseItemId(newBookmarkItem.guid);
  let newItem = yield placesBookmarkToSyncBookmark(newBookmarkItem);

  try {
    newItem.tags = yield tagItem(newBookmarkItem, updateInfo.tags);
  } catch (ex) {
    BookmarkSyncLog.warn(`updateBookmarkMetadata: Error tagging item ${
      updateInfo.syncId}`, ex);
  }

  if (updateInfo.hasOwnProperty("keyword")) {
    // Unconditionally remove the old keyword.
    let entry = yield PlacesUtils.keywords.fetch({
      url: oldBookmarkItem.url.href,
    });
    if (entry) {
      yield PlacesUtils.keywords.remove({
        keyword: entry.keyword,
        source: SOURCE_SYNC,
      });
    }
    if (updateInfo.keyword) {
      yield PlacesUtils.keywords.insert({
        keyword: updateInfo.keyword,
        url: newItem.url.href,
        source: SOURCE_SYNC,
      });
    }
    newItem.keyword = updateInfo.keyword;
  }

  if (updateInfo.hasOwnProperty("description")) {
    if (updateInfo.description) {
      PlacesUtils.annotations.setItemAnnotation(itemId,
        BookmarkSyncUtils.DESCRIPTION_ANNO, updateInfo.description, 0,
        PlacesUtils.annotations.EXPIRE_NEVER,
        SOURCE_SYNC);
    } else {
      PlacesUtils.annotations.removeItemAnnotation(itemId,
        BookmarkSyncUtils.DESCRIPTION_ANNO, SOURCE_SYNC);
    }
    newItem.description = updateInfo.description;
  }

  if (updateInfo.hasOwnProperty("loadInSidebar")) {
    if (updateInfo.loadInSidebar) {
      PlacesUtils.annotations.setItemAnnotation(itemId,
        BookmarkSyncUtils.SIDEBAR_ANNO, updateInfo.loadInSidebar, 0,
        PlacesUtils.annotations.EXPIRE_NEVER,
        SOURCE_SYNC);
    } else {
      PlacesUtils.annotations.removeItemAnnotation(itemId,
        BookmarkSyncUtils.SIDEBAR_ANNO, SOURCE_SYNC);
    }
    newItem.loadInSidebar = updateInfo.loadInSidebar;
  }

  if (updateInfo.hasOwnProperty("query")) {
    PlacesUtils.annotations.setItemAnnotation(itemId,
      BookmarkSyncUtils.SMART_BOOKMARKS_ANNO, updateInfo.query, 0,
      PlacesUtils.annotations.EXPIRE_NEVER,
      SOURCE_SYNC);
    newItem.query = updateInfo.query;
  }

  return newItem;
});

function validateNewBookmark(info) {
  let insertInfo = validateSyncBookmarkObject(info,
    { kind: { required: true }
    , syncId: { required: true }
    , url: { requiredIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                              , BookmarkSyncUtils.KINDS.MICROSUMMARY
                              , BookmarkSyncUtils.KINDS.QUERY ].includes(b.kind)
           , validIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                           , BookmarkSyncUtils.KINDS.MICROSUMMARY
                           , BookmarkSyncUtils.KINDS.QUERY ].includes(b.kind) }
    , parentSyncId: { required: true }
    , title: { validIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                             , BookmarkSyncUtils.KINDS.MICROSUMMARY
                             , BookmarkSyncUtils.KINDS.QUERY
                             , BookmarkSyncUtils.KINDS.FOLDER
                             , BookmarkSyncUtils.KINDS.LIVEMARK ].includes(b.kind) }
    , query: { validIf: b => b.kind == BookmarkSyncUtils.KINDS.QUERY }
    , folder: { validIf: b => b.kind == BookmarkSyncUtils.KINDS.QUERY }
    , tags: { validIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                            , BookmarkSyncUtils.KINDS.MICROSUMMARY
                            , BookmarkSyncUtils.KINDS.QUERY ].includes(b.kind) }
    , keyword: { validIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                               , BookmarkSyncUtils.KINDS.MICROSUMMARY
                               , BookmarkSyncUtils.KINDS.QUERY ].includes(b.kind) }
    , description: { validIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                                   , BookmarkSyncUtils.KINDS.MICROSUMMARY
                                   , BookmarkSyncUtils.KINDS.QUERY
                                   , BookmarkSyncUtils.KINDS.FOLDER
                                   , BookmarkSyncUtils.KINDS.LIVEMARK ].includes(b.kind) }
    , loadInSidebar: { validIf: b => [ BookmarkSyncUtils.KINDS.BOOKMARK
                                     , BookmarkSyncUtils.KINDS.MICROSUMMARY
                                     , BookmarkSyncUtils.KINDS.QUERY ].includes(b.kind) }
    , feed: { validIf: b => b.kind == BookmarkSyncUtils.KINDS.LIVEMARK }
    , site: { validIf: b => b.kind == BookmarkSyncUtils.KINDS.LIVEMARK }
    });

  return insertInfo;
}

// Returns an array of GUIDs for items that have an `anno` with the given `val`.
var fetchGuidsWithAnno = Task.async(function* (anno, val) {
  let db = yield PlacesUtils.promiseDBConnection();
  let rows = yield db.executeCached(`
    SELECT b.guid FROM moz_items_annos a
    JOIN moz_anno_attributes n ON n.id = a.anno_attribute_id
    JOIN moz_bookmarks b ON b.id = a.item_id
    WHERE n.name = :anno AND
          a.content = :val`,
    { anno, val });
  return rows.map(row => row.getResultByName("guid"));
});

// Returns the value of an item's annotation, or `null` if it's not set.
var getAnno = Task.async(function* (guid, anno) {
  let db = yield PlacesUtils.promiseDBConnection();
  let rows = yield db.executeCached(`
    SELECT a.content FROM moz_items_annos a
    JOIN moz_anno_attributes n ON n.id = a.anno_attribute_id
    JOIN moz_bookmarks b ON b.id = a.item_id
    WHERE b.guid = :guid AND
          n.name = :anno`,
    { guid, anno });
  return rows.length ? rows[0].getResultByName("content") : null;
});

var tagItem = Task.async(function(item, tags) {
  if (!item.url) {
    return [];
  }

  // Remove leading and trailing whitespace, then filter out empty tags.
  let newTags = tags.map(tag => tag.trim()).filter(Boolean);

  // Removing the last tagged item will also remove the tag. To preserve
  // tag IDs, we temporarily tag a dummy URI, ensuring the tags exist.
  let dummyURI = PlacesUtils.toURI("about:weave#BStore_tagURI");
  let bookmarkURI = PlacesUtils.toURI(item.url.href);
  PlacesUtils.tagging.tagURI(dummyURI, newTags, SOURCE_SYNC);
  PlacesUtils.tagging.untagURI(bookmarkURI, null, SOURCE_SYNC);
  PlacesUtils.tagging.tagURI(bookmarkURI, newTags, SOURCE_SYNC);
  PlacesUtils.tagging.untagURI(dummyURI, null, SOURCE_SYNC);

  return newTags;
});

// `PlacesUtils.bookmarks.update` checks if we've supplied enough properties,
// but doesn't know about additional livemark properties. We check this to avoid
// having it throw in case we only pass properties like `{ guid, feedURI }`.
function shouldUpdateBookmark(bookmarkInfo) {
  return bookmarkInfo.hasOwnProperty("parentGuid") ||
         bookmarkInfo.hasOwnProperty("title") ||
         bookmarkInfo.hasOwnProperty("url");
}

// Returns the folder ID for `tag`, or `null` if the tag doesn't exist.
var getTagFolder = Task.async(function* (tag) {
  let db = yield PlacesUtils.promiseDBConnection();
  let results = yield db.executeCached(`
    SELECT id
    FROM moz_bookmarks
    WHERE type = :type AND
          parent = :tagsFolderId AND
          title = :tag`,
    { type: PlacesUtils.bookmarks.TYPE_FOLDER,
      tagsFolderId: PlacesUtils.tagsFolderId, tag });
  return results.length ? results[0].getResultByName("id") : null;
});

// Returns the folder ID for `tag`, creating one if it doesn't exist.
var getOrCreateTagFolder = Task.async(function* (tag) {
  let id = yield getTagFolder(tag);
  if (id) {
    return id;
  }
  // Create the tag if it doesn't exist.
  let item = yield PlacesUtils.bookmarks.insert({
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    parentGuid: PlacesUtils.bookmarks.tagsGuid,
    title: tag,
    source: SOURCE_SYNC,
  });
  return PlacesUtils.promiseItemId(item.guid);
});

// Converts a Places bookmark or livemark to a Sync bookmark. This function
// maps Places GUIDs to sync IDs and filters out extra Places properties like
// date added, last modified, and index.
var placesBookmarkToSyncBookmark = Task.async(function* (bookmarkItem) {
  let item = {};

  for (let prop in bookmarkItem) {
    switch (prop) {
      // Sync IDs are identical to Places GUIDs for all items except roots.
      case "guid":
        item.syncId = BookmarkSyncUtils.guidToSyncId(bookmarkItem.guid);
        break;

      case "parentGuid":
        item.parentSyncId =
          BookmarkSyncUtils.guidToSyncId(bookmarkItem.parentGuid);
        break;

      // Sync uses kinds instead of types, which distinguish between folders,
      // livemarks, bookmarks, and queries.
      case "type":
        item.kind = yield getKindForItem(bookmarkItem);
        break;

      case "title":
      case "url":
        item[prop] = bookmarkItem[prop];
        break;

      // Livemark objects contain additional properties. The feed URL is
      // required; the site URL is optional.
      case "feedURI":
        item.feed = new URL(bookmarkItem.feedURI.spec);
        break;

      case "siteURI":
        if (bookmarkItem.siteURI) {
          item.site = new URL(bookmarkItem.siteURI.spec);
        }
        break;
    }
  }

  return item;
});

// Converts a Sync bookmark object to a Places bookmark or livemark object.
// This function maps sync IDs to Places GUIDs, and filters out extra Sync
// properties like keywords, tags, and descriptions. Returns an object that can
// be passed to `PlacesUtils.livemarks.addLivemark` or
// `PlacesUtils.bookmarks.{insert, update}`.
function syncBookmarkToPlacesBookmark(info) {
  let bookmarkInfo = {
    source: SOURCE_SYNC,
  };

  for (let prop in info) {
    switch (prop) {
      case "kind":
        bookmarkInfo.type = getTypeForKind(info.kind);
        break;

      // Convert sync IDs to Places GUIDs for roots.
      case "syncId":
        bookmarkInfo.guid = BookmarkSyncUtils.syncIdToGuid(info.syncId);
        break;

      case "parentSyncId":
        bookmarkInfo.parentGuid =
          BookmarkSyncUtils.syncIdToGuid(info.parentSyncId);
        // Instead of providing an index, Sync reorders children at the end of
        // the sync using `BookmarkSyncUtils.order`. We explicitly specify the
        // default index here to prevent `PlacesUtils.bookmarks.update` and
        // `PlacesUtils.livemarks.addLivemark` from throwing.
        bookmarkInfo.index = PlacesUtils.bookmarks.DEFAULT_INDEX;
        break;

      case "title":
      case "url":
        bookmarkInfo[prop] = info[prop];
        break;

      // Livemark-specific properties.
      case "feed":
        bookmarkInfo.feedURI = PlacesUtils.toURI(info.feed);
        break;

      case "site":
        if (info.site) {
          bookmarkInfo.siteURI = PlacesUtils.toURI(info.site);
        }
        break;
    }
  }

  return bookmarkInfo;
}

// Creates and returns a Sync bookmark object containing the bookmark's
// tags, keyword, description, and whether it loads in the sidebar.
var fetchBookmarkItem = Task.async(function* (bookmarkItem) {
  let item = yield placesBookmarkToSyncBookmark(bookmarkItem);

  if (!item.title) {
    item.title = "";
  }

  item.tags = PlacesUtils.tagging.getTagsForURI(
    PlacesUtils.toURI(bookmarkItem.url), {});

  let keywordEntry = yield PlacesUtils.keywords.fetch({
    url: bookmarkItem.url,
  });
  if (keywordEntry) {
    item.keyword = keywordEntry.keyword;
  }

  let description = yield getAnno(bookmarkItem.guid,
                                  BookmarkSyncUtils.DESCRIPTION_ANNO);
  if (description) {
    item.description = description;
  }

  item.loadInSidebar = !!(yield getAnno(bookmarkItem.guid,
                                        BookmarkSyncUtils.SIDEBAR_ANNO));

  return item;
});

// Creates and returns a Sync bookmark object containing the folder's
// description and children.
var fetchFolderItem = Task.async(function* (bookmarkItem) {
  let item = yield placesBookmarkToSyncBookmark(bookmarkItem);

  if (!item.title) {
    item.title = "";
  }

  let description = yield getAnno(bookmarkItem.guid,
                                  BookmarkSyncUtils.DESCRIPTION_ANNO);
  if (description) {
    item.description = description;
  }

  let db = yield PlacesUtils.promiseDBConnection();
  let childGuids = yield fetchChildGuids(db, bookmarkItem.guid);
  item.childSyncIds = childGuids.map(guid =>
    BookmarkSyncUtils.guidToSyncId(guid)
  );

  return item;
});

// Creates and returns a Sync bookmark object containing the livemark's
// description, children (none), feed URI, and site URI.
var fetchLivemarkItem = Task.async(function* (bookmarkItem) {
  let item = yield placesBookmarkToSyncBookmark(bookmarkItem);

  if (!item.title) {
    item.title = "";
  }

  let description = yield getAnno(bookmarkItem.guid,
                                  BookmarkSyncUtils.DESCRIPTION_ANNO);
  if (description) {
    item.description = description;
  }

  let feedAnno = yield getAnno(bookmarkItem.guid, PlacesUtils.LMANNO_FEEDURI);
  item.feed = new URL(feedAnno);

  let siteAnno = yield getAnno(bookmarkItem.guid, PlacesUtils.LMANNO_SITEURI);
  if (siteAnno) {
    item.site = new URL(siteAnno);
  }

  return item;
});

// Creates and returns a Sync bookmark object containing the query's tag
// folder name and smart bookmark query ID.
var fetchQueryItem = Task.async(function* (bookmarkItem) {
  let item = yield placesBookmarkToSyncBookmark(bookmarkItem);

  let description = yield getAnno(bookmarkItem.guid,
                                  BookmarkSyncUtils.DESCRIPTION_ANNO);
  if (description) {
    item.description = description;
  }

  let folder = null;
  let params = new URLSearchParams(bookmarkItem.url.pathname);
  let tagFolderId = +params.get("folder");
  if (tagFolderId) {
    try {
      let tagFolderGuid = yield PlacesUtils.promiseItemGuid(tagFolderId);
      let tagFolder = yield PlacesUtils.bookmarks.fetch(tagFolderGuid);
      folder = tagFolder.title;
    } catch (ex) {
      BookmarkSyncLog.warn("fetchQueryItem: Query " + bookmarkItem.url.href +
                           " points to nonexistent folder " + tagFolderId, ex);
    }
  }
  if (folder != null) {
    item.folder = folder;
  }

  let query = yield getAnno(bookmarkItem.guid,
                            BookmarkSyncUtils.SMART_BOOKMARKS_ANNO);
  if (query) {
    item.query = query;
  }

  return item;
});

function addRowToChangeRecords(row, changeRecords) {
  let syncId = BookmarkSyncUtils.guidToSyncId(row.getResultByName("guid"));
  let modified = row.getResultByName("modified") / MICROSECONDS_PER_SECOND;
  changeRecords[syncId] = {
    modified,
    counter: row.getResultByName("syncChangeCounter"),
    status: row.getResultByName("syncStatus"),
    tombstone: !!row.getResultByName("tombstone"),
    synced: false,
  };
}

/**
 * Queries the database for synced bookmarks and tombstones, updates the sync
 * status of all "NEW" bookmarks to "NORMAL", and returns a changeset for the
 * Sync bookmarks engine.
 *
 * @param db
 *        The Sqlite.jsm connection handle.
 * @return {Promise} resolved once all items have been fetched.
 * @resolves to an object containing records for changed bookmarks, keyed by
 *           the sync ID.
 */
var pullSyncChanges = Task.async(function* (db) {
  let changeRecords = {};

  yield db.executeCached(`
    WITH RECURSIVE
    syncedItems(id, guid, modified, syncChangeCounter, syncStatus) AS (
      SELECT b.id, b.guid, b.lastModified, b.syncChangeCounter, b.syncStatus
       FROM moz_bookmarks b
       WHERE b.guid IN ('menu________', 'toolbar_____', 'unfiled_____',
                        'mobile______')
      UNION ALL
      SELECT b.id, b.guid, b.lastModified, b.syncChangeCounter, b.syncStatus
      FROM moz_bookmarks b
      JOIN syncedItems s ON b.parent = s.id
    )
    SELECT guid, modified, syncChangeCounter, syncStatus, 0 AS tombstone
    FROM syncedItems
    WHERE syncChangeCounter >= 1
    UNION ALL
    SELECT guid, dateRemoved AS modified, 1 AS syncChangeCounter,
           :deletedSyncStatus, 1 AS tombstone
    FROM moz_bookmarks_deleted`,
    { deletedSyncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NORMAL },
    row => addRowToChangeRecords(row, changeRecords));

  yield markChangesAsSyncing(db, changeRecords);

  return changeRecords;
});

var touchSyncBookmark = Task.async(function* (db, bookmarkItem) {
  if (BookmarkSyncLog.level <= Log.Level.Trace) {
    BookmarkSyncLog.trace(
      `touch: Reviving item "${bookmarkItem.guid}" and marking parent ` +
      BookmarkSyncUtils.guidToSyncId(bookmarkItem.parentGuid) + ` as modified`);
  }

  // Bump the change counter of the item and its parent, so that we upload
  // both.
  yield db.executeCached(`
    UPDATE moz_bookmarks SET
      syncChangeCounter = syncChangeCounter + 1
    WHERE guid IN (:guid, :parentGuid)`,
    { guid: bookmarkItem.guid, parentGuid: bookmarkItem.parentGuid });

  // TODO (Bug 1313890): Refactor the bookmarks engine to pull change records
  // before uploading, instead of returning records to merge into the engine's
  // initial changeset.
  return pullSyncChanges(db);
});

var dedupeSyncBookmark = Task.async(function* (db, localGuid, remoteGuid,
                                               remoteParentGuid) {
  let rows = yield db.executeCached(`
    SELECT b.id, p.guid AS parentGuid, b.syncStatus
    FROM moz_bookmarks b
    JOIN moz_bookmarks p ON p.id = b.parent
    WHERE b.guid = :localGuid`,
    { localGuid });
  if (!rows.length) {
    throw new Error(`Local item ${localGuid} does not exist`);
  }

  let localId = rows[0].getResultByName("id");
  if (PlacesUtils.isRootItem(localId)) {
    throw new Error(`Cannot de-dupe local root ${localGuid}`);
  }

  let localParentGuid = rows[0].getResultByName("parentGuid");
  let sameParent = localParentGuid == remoteParentGuid;
  let modified = PlacesUtils.toPRTime(Date.now());

  yield db.executeTransaction(function* () {
    // Change the item's old GUID to the new remote GUID. This will throw a
    // constraint error if the remote GUID already exists locally.
    BookmarkSyncLog.debug("dedupeSyncBookmark: Switching local GUID " +
                          localGuid + " to incoming GUID " + remoteGuid);
    yield db.executeCached(`UPDATE moz_bookmarks
      SET guid = :remoteGuid
      WHERE id = :localId`,
      { remoteGuid, localId });
    PlacesUtils.invalidateCachedGuidFor(localId);

    // And mark the parent as being modified. Given we de-dupe based on the
    // parent *name* it's possible the item having its GUID changed has a
    // different parent from the incoming record.
    // So we need to return a change record for the parent, and bump its
    // counter to ensure we don't lose the change if the current sync is
    // interrupted.
    yield db.executeCached(`UPDATE moz_bookmarks
      SET syncChangeCounter = syncChangeCounter + 1
      WHERE guid = :localParentGuid`,
      { localParentGuid });

    // And we also add the parent as reflected in the incoming record as the
    // de-dupe process might have used an existing item in a different folder.
    // This statement is a no-op if we don't have the new parent yet, but that's
    // fine: applying the record will add our special SYNC_PARENT_ANNO
    // annotation and move it to unfiled. If the parent arrives in the future
    // (either this Sync or a later one), the item will be reparented. Note that
    // this scenario will still leave us with inconsistent client and server
    // states; the incoming record on the server references a parent that isn't
    // the actual parent locally - see bug 1297955.
    if (!sameParent) {
      yield db.executeCached(`UPDATE moz_bookmarks
        SET syncChangeCounter = syncChangeCounter + 1
        WHERE guid = :remoteParentGuid`,
        { remoteParentGuid });
    }

    // The local, duplicate ID is always deleted on the server - but for
    // bookmarks it is a logical delete.
    let localSyncStatus = rows[0].getResultByName("syncStatus");
    if (localSyncStatus == PlacesUtils.bookmarks.SYNC_STATUS.NORMAL) {
      yield db.executeCached(`
        INSERT INTO moz_bookmarks_deleted (guid, dateRemoved)
        VALUES (:localGuid, :modified)`,
        { localGuid, modified });
    }
  });

  // TODO (Bug 1313890): Refactor the bookmarks engine to pull change records
  // before uploading, instead of returning records to merge into the engine's
  // initial changeset.
  let changeRecords = yield pullSyncChanges(db);

  if (BookmarkSyncLog.level <= Log.Level.Debug && !sameParent) {
    let remoteParentSyncId = BookmarkSyncUtils.guidToSyncId(remoteParentGuid);
    if (!changeRecords.hasOwnProperty(remoteParentSyncId)) {
      BookmarkSyncLog.debug("dedupeSyncBookmark: Incoming duplicate item " +
                            remoteGuid + " specifies non-existing parent " +
                            remoteParentGuid);
    }
  }

  return changeRecords;
});

// Moves a synced folder's remaining children to its parent, and deletes the
// folder if it's empty.
var deleteSyncedFolder = Task.async(function* (bookmarkItem) {
  // At this point, any member in the folder that remains is either a folder
  // pending deletion (which we'll get to in this function), or an item that
  // should not be deleted. To avoid deleting these items, we first move them
  // to the parent of the folder we're about to delete.
  let db = yield PlacesUtils.promiseDBConnection();
  let childGuids = yield fetchChildGuids(db, bookmarkItem.guid);
  if (!childGuids.length) {
    // No children -- just delete the folder.
    return deleteSyncedAtom(bookmarkItem);
  }

  if (BookmarkSyncLog.level <= Log.Level.Trace) {
    BookmarkSyncLog.trace(
      `deleteSyncedFolder: Moving ${JSON.stringify(childGuids)} children of ` +
      `"${bookmarkItem.guid}" to grandparent
      "${BookmarkSyncUtils.guidToSyncId(bookmarkItem.parentGuid)}" before ` +
      `deletion`);
  }

  // Move children out of the parent and into the grandparent
  for (let guid of childGuids) {
    yield PlacesUtils.bookmarks.update({
      guid,
      parentGuid: bookmarkItem.parentGuid,
      index: PlacesUtils.bookmarks.DEFAULT_INDEX,
      // `SYNC_REPARENT_REMOVED_FOLDER_CHILDREN` bumps the change counter for
      // the child and its new parent, without incrementing the bookmark
      // tracker's score.
      //
      // We intentionally don't check if the child is one we'll remove later,
      // so it's possible we'll bump the change counter of the closest living
      // ancestor when it's not needed. This avoids inconsistency if removal
      // is interrupted, since we don't run this operation in a transaction.
      source: PlacesUtils.bookmarks.SOURCES.SYNC_REPARENT_REMOVED_FOLDER_CHILDREN,
    });
  }

  // Delete the (now empty) parent
  try {
    yield PlacesUtils.bookmarks.remove(bookmarkItem.guid, {
      preventRemovalOfNonEmptyFolders: true,
      // We don't want to bump the change counter for this deletion, because
      // a tombstone for the folder is already on the server.
      source: SOURCE_SYNC,
    });
  } catch (e) {
    // We failed, probably because someone added something to this folder
    // between when we got the children and now (or the database is corrupt,
    // or something else happened...) This is unlikely, but possible. To
    // avoid corruption in this case, we need to reupload the record to the
    // server.
    //
    // (Ideally this whole operation would be done in a transaction, and this
    // wouldn't be possible).
    BookmarkSyncLog.trace(`deleteSyncedFolder: Error removing parent ` +
                          `${bookmarkItem.guid} after reparenting children`, e);
    return false;
  }

  return true;
});

// Removes a synced bookmark or empty folder from the database.
var deleteSyncedAtom = Task.async(function* (bookmarkItem) {
  try {
    yield PlacesUtils.bookmarks.remove(bookmarkItem.guid, {
      preventRemovalOfNonEmptyFolders: true,
      source: SOURCE_SYNC,
    });
  } catch (ex) {
    // Likely already removed.
    BookmarkSyncLog.trace(`deleteSyncedAtom: Error removing ` +
                          bookmarkItem.guid, ex);
    return false;
  }

  return true;
});

/**
 * Updates the sync status on all "NEW" and "UNKNOWN" bookmarks to "NORMAL".
 *
 * We do this when pulling changes instead of in `pushChanges` to make sure
 * we write tombstones if a new item is deleted after an interrupted sync. (For
 * example, if a "NEW" record is uploaded or reconciled, then the app is closed
 * before Sync calls `pushChanges`).
 */
function markChangesAsSyncing(db, changeRecords) {
  let unsyncedGuids = [];
  for (let syncId in changeRecords) {
    if (changeRecords[syncId].tombstone) {
      continue;
    }
    if (changeRecords[syncId].status ==
        PlacesUtils.bookmarks.SYNC_STATUS.NORMAL) {
      continue;
    }
    let guid = BookmarkSyncUtils.syncIdToGuid(syncId);
    unsyncedGuids.push(JSON.stringify(guid));
  }
  if (!unsyncedGuids.length) {
    return Promise.resolve();
  }
  return db.execute(`
    UPDATE moz_bookmarks
    SET syncStatus = :syncStatus
    WHERE guid IN (${unsyncedGuids.join(",")})`,
    { syncStatus: PlacesUtils.bookmarks.SYNC_STATUS.NORMAL });
}

// Removes tombstones for successfully synced items.
var removeTombstones = Task.async(function* (db, guids) {
  if (!guids.length) {
    return Promise.resolve();
  }
  return db.execute(`
    DELETE FROM moz_bookmarks_deleted
    WHERE guid IN (${guids.map(guid => JSON.stringify(guid)).join(",")})`);
});
