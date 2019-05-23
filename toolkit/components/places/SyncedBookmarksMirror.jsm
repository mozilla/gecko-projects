/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * This file implements a mirror and two-way merger for synced bookmarks. The
 * mirror matches the complete tree stored on the Sync server, and stages new
 * bookmarks changed on the server since the last sync. The merger walks the
 * local tree in Places and the mirrored remote tree, produces a new merged
 * tree, then updates the local tree to reflect the merged tree.
 *
 * Let's start with an overview of the different classes, and how they fit
 * together.
 *
 * - `SyncedBookmarksMirror` sets up the database, validates and upserts new
 *   incoming records, attaches to Places, and applies the changed records.
 *   During application, we fetch the local and remote bookmark trees, merge
 *   them, and update Places to match. Merging and application happen in a
 *   single transaction, so applying the merged tree won't collide with local
 *   changes. A failure at this point aborts the merge and leaves Places
 *   unchanged.
 *
 * - A `BookmarkTree` is a fully rooted tree that also notes deletions. A
 *   `BookmarkNode` represents a local item in Places, or a remote item in the
 *   mirror.
 *
 * - A `MergedBookmarkNode` holds a local node, a remote node, and a
 *   `MergeState` that indicates which node to prefer when updating Places and
 *   the server to match the merged tree.
 *
 * - `BookmarkObserverRecorder` records all changes made to Places during the
 *   merge, then dispatches `nsINavBookmarkObserver` notifications. Places uses
 *   these notifications to update the UI and internal caches. We can't dispatch
 *   during the merge because observers won't see the changes until the merge
 *   transaction commits and the database is consistent again.
 *
 * - After application, we flag all applied incoming items as merged, create
 *   Sync records for the locally new and updated items in Places, and upload
 *   the records to the server. At this point, all outgoing items are flagged as
 *   changed in Places, so the next sync can resume cleanly if the upload is
 *   interrupted or fails.
 *
 * - Once upload succeeds, we update the mirror with the uploaded records, so
 *   that the mirror matches the server again. An interruption or error here
 *   will leave the uploaded items flagged as changed in Places, so we'll merge
 *   them again on the next sync. This is redundant work, but shouldn't cause
 *   issues.
 */

var EXPORTED_SYMBOLS = ["SyncedBookmarksMirror"];

const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");
const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGlobalGetters(this, ["URL"]);

XPCOMUtils.defineLazyModuleGetters(this, {
  Async: "resource://services-common/async.js",
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.jsm",
  Log: "resource://gre/modules/Log.jsm",
  OS: "resource://gre/modules/osfile.jsm",
  PlacesSyncUtils: "resource://gre/modules/PlacesSyncUtils.jsm",
  PlacesUtils: "resource://gre/modules/PlacesUtils.jsm",
  Sqlite: "resource://gre/modules/Sqlite.jsm",
});

XPCOMUtils.defineLazyGetter(this, "MirrorLog", () =>
  Log.repository.getLogger("Sync.Engine.Bookmarks.Mirror")
);

const SyncedBookmarksMerger = Components.Constructor(
  "@mozilla.org/browser/synced-bookmarks-merger;1",
  "mozISyncedBookmarksMerger");

/**
 * A common table expression for all local items in Places, to be included in a
 * `WITH RECURSIVE` clause. We start at the roots, excluding tags (bug 424160),
 * and work our way down.
 *
 * Note that syncable items (`isSyncable`) descend from the four syncable roots.
 * Any other roots and their descendants, like the left pane root, left pane
 * queries, and custom roots, are non-syncable.
 *
 * Newer Desktops should never reupload non-syncable items (bug 1274496), and
 * should have removed them in Places migrations (bug 1310295). However, these
 * items might be orphaned in "unfiled", in which case they're seen as syncable
 * locally. If the server has the missing parents and roots, we'll determine
 * that the items are non-syncable when merging, remove them from Places, and
 * upload tombstones to the server.
 */
XPCOMUtils.defineLazyGetter(this, "LocalItemsSQLFragment", () => `
  localItems(id, guid, parentId, parentGuid, position, type, title,
             parentTitle, placeId, dateAdded, lastModified, syncChangeCounter,
             isSyncable, level) AS (
    SELECT b.id, b.guid, p.id, p.guid, b.position, b.type, b.title, p.title,
           b.fk, b.dateAdded, b.lastModified, b.syncChangeCounter,
           b.guid IN (${PlacesUtils.bookmarks.userContentRoots.map(v =>
             `'${v}'`
           ).join(",")}), 0
    FROM moz_bookmarks b
    JOIN moz_bookmarks p ON p.id = b.parent
    WHERE b.guid <> '${PlacesUtils.bookmarks.tagsGuid}' AND
          p.guid = '${PlacesUtils.bookmarks.rootGuid}'
    UNION ALL
    SELECT b.id, b.guid, s.id, s.guid, b.position, b.type, b.title, s.title,
           b.fk, b.dateAdded, b.lastModified, b.syncChangeCounter,
           s.isSyncable, s.level + 1
    FROM moz_bookmarks b
    JOIN localItems s ON s.id = b.parent
    WHERE b.guid <> '${PlacesUtils.bookmarks.rootGuid}'
  )
`);

// These can be removed once they're exposed in a central location (bug
// 1375896).
const DB_URL_LENGTH_MAX = 65536;
const DB_TITLE_LENGTH_MAX = 4096;

const SQLITE_MAX_VARIABLE_NUMBER = 999;

// The current mirror database schema version. Bump for migrations, then add
// migration code to `migrateMirrorSchema`.
const MIRROR_SCHEMA_VERSION = 5;

const DEFAULT_MAX_FRECENCIES_TO_RECALCULATE = 400;

// Use a shared jankYielder in these functions
XPCOMUtils.defineLazyGetter(this, "yieldState", () => Async.yieldState());

/** Adapts a `Log.jsm` logger to a `mozISyncedBookmarksMirrorLogger`. */
class MirrorLoggerAdapter {
  constructor(log) {
    this.log = log;
  }

  get maxLevel() {
    let level = this.log.level;
    if (level <= Log.Level.All) {
      return Ci.mozISyncedBookmarksMirrorLogger.LEVEL_TRACE;
    }
    if (level <= Log.Level.Info) {
      return Ci.mozISyncedBookmarksMirrorLogger.LEVEL_DEBUG;
    }
    if (level <= Log.Level.Warn) {
      return Ci.mozISyncedBookmarksMirrorLogger.LEVEL_WARN;
    }
    if (level <= Log.Level.Error) {
      return Ci.mozISyncedBookmarksMirrorLogger.LEVEL_ERROR;
    }
    return Ci.mozISyncedBookmarksMirrorLogger.LEVEL_OFF;
  }

  trace(message) {
    this.log.trace(message);
  }

  debug(message) {
    this.log.debug(message);
  }

  warn(message) {
    this.log.warn(message);
  }

  error(message) {
    this.log.error(message);
  }
}

/**
 * A mirror maintains a copy of the complete tree as stored on the Sync server.
 * It is persistent.
 *
 * The mirror schema is a hybrid of how Sync and Places represent bookmarks.
 * The `items` table contains item attributes (title, kind, URL, etc.), while
 * the `structure` table stores parent-child relationships and position.
 * This is similar to how iOS encodes "value" and "structure" state,
 * though we handle these differently when merging. See `BookmarkMerger` for
 * details.
 *
 * There's no guarantee that the remote state is consistent. We might be missing
 * parents or children, or a bookmark and its parent might disagree about where
 * it belongs. This means we need a strategy to handle missing parents and
 * children.
 *
 * We treat the `children` of the last parent we see as canonical, and ignore
 * the child's `parentid` entirely. We also ignore missing children, and
 * temporarily reparent bookmarks with missing parents to "unfiled". When we
 * eventually see the missing items, either during a later sync or as part of
 * repair, we'll fill in the mirror's gaps and fix up the local tree.
 *
 * During merging, we won't intentionally try to fix inconsistencies on the
 * server, and opt to build as complete a tree as we can from the remote state,
 * even if we diverge from what's in the mirror. See bug 1433512 for context.
 *
 * If a sync is interrupted, we resume downloading from the server collection
 * last modified time, or the server last modified time of the most recent
 * record if newer. New incoming records always replace existing records in the
 * mirror.
 *
 * We delete the mirror database on client reset, including when the sync ID
 * changes on the server, and when the user is node reassigned, disables the
 * bookmarks engine, or signs out.
 */
class SyncedBookmarksMirror {
  constructor(db, { recordTelemetryEvent, finalizeAt =
                      AsyncShutdown.profileBeforeChange } = {}) {
    this.db = db;
    this.recordTelemetryEvent = recordTelemetryEvent;

    this.merger = new SyncedBookmarksMerger();
    this.merger.db = db.unsafeRawConnection.QueryInterface(
      Ci.mozIStorageConnection);
    this.merger.logger = new MirrorLoggerAdapter(MirrorLog);

    // Automatically close the database connection on shutdown.
    this.finalizeAt = finalizeAt;
    this.finalizeBound = () => this.finalize();
    this.finalizeAt.addBlocker("SyncedBookmarksMirror: finalize",
                               this.finalizeBound);
  }

  /**
   * Sets up the mirror database connection and upgrades the mirror to the
   * newest schema version. Automatically recreates the mirror if it's corrupt;
   * throws on failure.
   *
   * @param  {String} options.path
   *         The path to the mirror database file, either absolute or relative
   *         to the profile path.
   * @param  {Function} options.recordTelemetryEvent
   *         A function with the signature `(object: String, method: String,
   *         value: String?, extra: Object?)`, used to emit telemetry events.
   * @param  {AsyncShutdown.Barrier} [options.finalizeAt]
   *         A shutdown phase, barrier, or barrier client that should
   *         automatically finalize the mirror when triggered. Exposed for
   *         testing.
   * @return {SyncedBookmarksMirror}
   *         A mirror ready for use.
   */
  static async open(options) {
    let db = await Sqlite.cloneStorageConnection({
      connection: PlacesUtils.history.DBConnection,
      readOnly: false,
    });
    let path = OS.Path.join(OS.Constants.Path.profileDir, options.path);
    let whyFailed = "initialize";
    try {
      await db.execute(`PRAGMA foreign_keys = ON`);
      try {
        await attachAndInitMirrorDatabase(db, path);
      } catch (ex) {
        if (isDatabaseCorrupt(ex)) {
          MirrorLog.warn("Error attaching mirror to Places; removing and " +
                         "recreating mirror", ex);
          options.recordTelemetryEvent("mirror", "open", "retry",
                                       { why: "corrupt" });

          whyFailed = "remove";
          await OS.File.remove(path);

          whyFailed = "replace";
          await attachAndInitMirrorDatabase(db, path);
        } else {
          MirrorLog.error("Unrecoverable error attaching mirror to Places", ex);
          throw ex;
        }
      }
      try {
        let info = await OS.File.stat(path);
        let size = Math.floor(info.size / 1024);
        options.recordTelemetryEvent("mirror", "open", "success", { size });
      } catch (ex) {
        MirrorLog.warn("Error recording stats for mirror database size", ex);
      }
    } catch (ex) {
      options.recordTelemetryEvent("mirror", "open", "error",
                                   { why: whyFailed });
      await db.close();
      throw ex;
    }
    return new SyncedBookmarksMirror(db, options);
  }

  /**
   * Returns the newer of the bookmarks collection last modified time, or the
   * server modified time of the newest record. The bookmarks engine uses this
   * timestamp as the "high water mark" for all downloaded records. Each sync
   * downloads and stores records that are strictly newer than this time.
   *
   * @return {Number}
   *         The high water mark time, in seconds.
   */
  async getCollectionHighWaterMark() {
    // The first case, where we have records with server modified times newer
    // than the collection last modified time, occurs when a sync is interrupted
    // before we call `setCollectionLastModified`. We subtract one second, the
    // maximum time precision guaranteed by the server, so that we don't miss
    // other records with the same time as the newest one we downloaded.
    let rows = await this.db.executeCached(`
      SELECT MAX(
        IFNULL((SELECT MAX(serverModified) - 1000 FROM items), 0),
        IFNULL((SELECT CAST(value AS INTEGER) FROM meta
                WHERE key = :modifiedKey), 0)
      ) AS highWaterMark`,
      { modifiedKey: SyncedBookmarksMirror.META_KEY.LAST_MODIFIED });
    let highWaterMark = rows[0].getResultByName("highWaterMark");
    return highWaterMark / 1000;
  }

  /**
   * Updates the bookmarks collection last modified time. Note that this may
   * be newer than the modified time of the most recent record.
   *
   * @param {Number|String} lastModifiedSeconds
   *        The collection last modified time, in seconds.
   */
  async setCollectionLastModified(lastModifiedSeconds) {
    let lastModified = Math.floor(lastModifiedSeconds * 1000);
    if (!Number.isInteger(lastModified)) {
      throw new TypeError("Invalid collection last modified time");
    }
    await this.db.executeBeforeShutdown(
      "SyncedBookmarksMirror: setCollectionLastModified",
      db => db.executeCached(`
        REPLACE INTO meta(key, value)
        VALUES(:modifiedKey, :lastModified)`,
        { modifiedKey: SyncedBookmarksMirror.META_KEY.LAST_MODIFIED,
          lastModified })
    );
  }

  /**
   * Returns the bookmarks collection sync ID. This corresponds to
   * `PlacesSyncUtils.bookmarks.getSyncId`.
   *
   * @return {String}
   *         The sync ID, or `""` if one isn't set.
   */
  async getSyncId() {
    let rows = await this.db.executeCached(`
      SELECT value FROM meta WHERE key = :syncIdKey`,
      { syncIdKey: SyncedBookmarksMirror.META_KEY.SYNC_ID });
    return rows.length ? rows[0].getResultByName("value") : "";
  }

  /**
   * Ensures that the sync ID in the mirror is up-to-date with the server and
   * Places, and discards the mirror on mismatch.
   *
   * The bookmarks engine store the same sync ID in Places and the mirror to
   * "tie" the two together. This allows Sync to do the right thing if the
   * database files are copied between profiles connected to different accounts.
   *
   * See `PlacesSyncUtils.bookmarks.ensureCurrentSyncId` for an explanation of
   * how Places handles sync ID mismatches.
   *
   * @param {String} newSyncId
   *        The server's sync ID.
   */
  async ensureCurrentSyncId(newSyncId) {
    if (!newSyncId || typeof newSyncId != "string") {
      throw new TypeError("Invalid new bookmarks sync ID");
    }
    let existingSyncId = await this.getSyncId();
    if (existingSyncId == newSyncId) {
      MirrorLog.trace("Sync ID up-to-date in mirror", { existingSyncId });
      return;
    }
    MirrorLog.info("Sync ID changed from ${existingSyncId} to " +
                   "${newSyncId}; resetting mirror",
                   { existingSyncId, newSyncId });
    await this.db.executeBeforeShutdown(
      "SyncedBookmarksMirror: ensureCurrentSyncId",
      db => db.executeTransaction(async function() {
        await resetMirror(db);
        await db.execute(`
          REPLACE INTO meta(key, value)
          VALUES(:syncIdKey, :newSyncId)`,
          { syncIdKey: SyncedBookmarksMirror.META_KEY.SYNC_ID, newSyncId });
      })
    );
  }

  /**
   * Stores incoming or uploaded Sync records in the mirror. Rejects if any
   * records are invalid.
   *
   * @param {PlacesItem[]} records
   *        Sync records to store in the mirror.
   * @param {Boolean} [options.needsMerge]
   *        Indicates if the records were changed remotely since the last sync,
   *        and should be merged into the local tree. This option is set to
   *        `true` for incoming records, and `false` for successfully uploaded
   *        records. Tests can also pass `false` to set up an existing mirror.
   */
  async store(records, { needsMerge = true } = {}) {
    let options = { needsMerge };
    await this.db.executeBeforeShutdown(
      "SyncedBookmarksMirror: store",
      db => db.executeTransaction(async () => {
        await Async.yieldingForEach(records, async (record) => {
          let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);
          if (guid == PlacesUtils.bookmarks.rootGuid) {
            // The engine should hard DELETE Places roots from the server.
            throw new TypeError("Can't store Places root");
          }
          MirrorLog.trace(`Storing in mirror: ${record.cleartextToString()}`);
          switch (record.type) {
            case "bookmark":
              await this.storeRemoteBookmark(record, options);
              return;

            case "query":
              await this.storeRemoteQuery(record, options);
              return;

            case "folder":
              await this.storeRemoteFolder(record, options);
              return;

            case "livemark":
              await this.storeRemoteLivemark(record, options);
              return;

            case "separator":
              await this.storeRemoteSeparator(record, options);
              return;

            default:
              if (record.deleted) {
                await this.storeRemoteTombstone(record, options);
                return;
              }
          }
          MirrorLog.warn("Ignoring record with unknown type", record.type);
        }, yieldState);
      }
    ));
  }

  /**
   * Builds a complete merged tree from the local and remote trees, resolves
   * value and structure conflicts, dedupes local items, applies the merged
   * tree back to Places, and notifies observers about the changes.
   *
   * Merging and application happen in a transaction, meaning code that uses the
   * main Places connection, including the UI, will fail to write to the
   * database until the transaction commits. Asynchronous consumers will retry
   * on `SQLITE_BUSY`; synchronous consumers will fail after waiting for 100ms.
   * See bug 1305563, comment 122 for details.
   *
   * @param  {Number} [options.localTimeSeconds]
   *         The current local time, in seconds.
   * @param  {Number} [options.remoteTimeSeconds]
   *         The current server time, in seconds.
   * @param  {String[]} [options.weakUpload]
   *         GUIDs of bookmarks to weakly upload.
   * @param  {Number} [options.maxFrecenciesToRecalculate]
   *         The maximum number of bookmark URL frecencies to recalculate after
   *         this merge. Frecency calculation blocks other Places writes, so we
   *         limit the number of URLs we process at once. We'll process either
   *         the next set of URLs after the next merge, or all remaining URLs
   *         when Places automatically fixes invalid frecencies on idle;
   *         whichever comes first.
   * @return {Object.<String, BookmarkChangeRecord>}
   *         A changeset containing locally changed and reconciled records to
   *         upload to the server, and to store in the mirror once upload
   *         succeeds.
   */
  async apply(options = {}) {
    let hasChanges = ("weakUpload" in options &&
                      options.weakUpload.length > 0) ||
                     (await this.hasChanges());
    if (!hasChanges) {
      MirrorLog.debug("No changes detected in both mirror and Places");
      let limit = "maxFrecenciesToRecalculate" in options ?
                  options.maxFrecenciesToRecalculate :
                  DEFAULT_MAX_FRECENCIES_TO_RECALCULATE;
      await updateFrecencies(this.db, limit);
      return {};
    }
    let changeRecords = await this.forceApply(options);
    return changeRecords;
  }

  // Forces a full merge, even if there are no local or remote changes, and
  // no items to weakly upload. Exposed for tests.
  async forceApply({ localTimeSeconds = Date.now() / 1000,
                     remoteTimeSeconds = 0,
                     weakUpload = [],
                     maxFrecenciesToRecalculate =
                       DEFAULT_MAX_FRECENCIES_TO_RECALCULATE } = {}) {
    // We intentionally don't use `executeBeforeShutdown` in this function,
    // since merging can take a while for large trees, and we don't want to
    // block shutdown. Since all new items are in the mirror, we'll just try
    // to merge again on the next sync.

    let observersToNotify = new BookmarkObserverRecorder(this.db,
      { maxFrecenciesToRecalculate });

    if (!(await this.validLocalRoots())) {
      throw new SyncedBookmarksMirror.MergeError(
        "Local tree has misparented root");
    }

    // The flow ID is used to correlate telemetry events for each sync.
    let flowID = PlacesUtils.history.makeGuid();

    let changeRecords;
    try {
      changeRecords = await this.tryApply(flowID, localTimeSeconds,
                                          remoteTimeSeconds,
                                          observersToNotify,
                                          weakUpload);
    } catch (ex) {
      // Include the error message in the event payload, since we can't
      // easily correlate event telemetry to engine errors in the Sync ping.
      let why = (typeof ex.message == "string" ? ex.message :
                 String(ex)).slice(0, 85);
      this.recordTelemetryEvent("mirror", "apply", "error", { flowID, why });
      throw ex;
    }

    return changeRecords;
  }

  async tryApply(flowID, localTimeSeconds, remoteTimeSeconds, observersToNotify,
                 weakUpload) {
    let result = await withTiming(
      "Merging bookmarks in Rust",
      () => {
        return new Promise((resolve, reject) => {
          let callback = {
            handleResult: resolve,
            handleError(code, message) {
              if (code == Cr.NS_ERROR_STORAGE_BUSY) {
                reject(new SyncedBookmarksMirror.MergeConflictError(
                  "Local tree changed during merge"));
              } else {
                reject(new SyncedBookmarksMirror.MergeError(message));
              }
            },
          };
          this.merger.merge(localTimeSeconds, remoteTimeSeconds, weakUpload,
                            callback);
        });
      },
    );
    let telem = result.QueryInterface(Ci.nsIPropertyBag);
    const telemPropToEventValue = [
      ["fetchLocalTreeTime", "fetchLocalTree"],
      ["fetchNewLocalContentsTime", "fetchNewLocalContents"],
      ["fetchRemoteTreeTime", "fetchRemoteTree"],
      ["fetchNewRemoteContentsTime", "fetchNewRemoteContents"],
    ];
    for (let [prop, value] of telemPropToEventValue) {
      this.recordTelemetryEvent("mirror", "apply", value,
        { flowID, time: telem.getProperty(prop) });
    }
    this.recordTelemetryEvent("mirror", "apply", "merge",
      { flowID, time: telem.getProperty("mergeTime"),
        nodes: telem.getProperty("mergedNodesCount"),
        deletions: telem.getProperty("mergedDeletionsCount"),
        dupes: telem.getProperty("dupesCount") });
    this.recordTelemetryEvent("mirror", "merge", "structure", {
      remoteRevives: telem.getProperty("remoteRevivesCount"),
      localDeletes: telem.getProperty("localDeletesCount"),
      localRevives: telem.getProperty("localRevivesCount"),
      remoteDeletes: telem.getProperty("remoteDeletesCount"),
    });

    // At this point, the database is consistent, so we can notify observers and
    // inflate records for outgoing items.

    await withTiming(
      "Notifying Places observers",
      async () => {
        try {
          // Note that we don't use a transaction when fetching info for
          // observers, so it's possible we might notify with stale info if the
          // main connection changes Places between the time we finish merging,
          // and the time we notify observers.
          await observersToNotify.notifyAll();
        } catch (ex) {
          MirrorLog.warn("Error notifying Places observers", ex);
        } finally {
          await this.db.executeTransaction(async () => {
            await this.db.execute(`DELETE FROM itemsAdded`);
            await this.db.execute(`DELETE FROM guidsChanged`);
            await this.db.execute(`DELETE FROM itemsChanged`);
            await this.db.execute(`DELETE FROM itemsRemoved`);
            await this.db.execute(`DELETE FROM itemsMoved`);
          });
        }
      },
      time => this.recordTelemetryEvent("mirror", "apply",
        "notifyObservers", { flowID, time })
    );

    return withTiming(
      "Fetching records for local items to upload",
      async () => {
        try {
          let changeRecords = await this.fetchLocalChangeRecords();
          return changeRecords;
        } finally {
          await this.db.execute(`DELETE FROM itemsToUpload`);
        }
      },
      time => this.recordTelemetryEvent("mirror", "apply",
        "fetchLocalChangeRecords", { flowID, time })
    );
  }

  /**
   * Discards the mirror contents. This is called when the user is node
   * reassigned, disables the bookmarks engine, or signs out.
   */
  async reset() {
    await this.db.executeBeforeShutdown(
      "SyncedBookmarksMirror: reset",
      db => db.executeTransaction(() => resetMirror(db))
    );
  }

  /**
   * Fetches the GUIDs of all items in the remote tree that need to be merged
   * into the local tree.
   *
   * @return {String[]}
   *         Remotely changed GUIDs that need to be merged into Places.
   */
  async fetchUnmergedGuids() {
    let rows = await this.db.execute(`SELECT guid FROM items WHERE needsMerge`);
    return rows.map(row => row.getResultByName("guid"));
  }

  async storeRemoteBookmark(record, { needsMerge }) {
    let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);

    let url = validateURL(record.bmkUri);
    if (url) {
      await this.maybeStoreRemoteURL(url);
    }

    let parentGuid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.parentid);
    let serverModified = determineServerModified(record);
    let dateAdded = determineDateAdded(record);
    let title = validateTitle(record.title);
    let keyword = validateKeyword(record.keyword);
    let validity = url ?
      Ci.mozISyncedBookmarksMerger.VALIDITY_VALID :
      Ci.mozISyncedBookmarksMerger.VALIDITY_REPLACE;

    await this.db.executeCached(`
      REPLACE INTO items(guid, parentGuid, serverModified, needsMerge, kind,
                         dateAdded, title, keyword, validity,
                         urlId)
      VALUES(:guid, :parentGuid, :serverModified, :needsMerge, :kind,
             :dateAdded, NULLIF(:title, ""), :keyword, :validity,
             (SELECT id FROM urls
              WHERE hash = hash(:url) AND
                    url = :url))`,
      { guid, parentGuid, serverModified, needsMerge,
        kind: Ci.mozISyncedBookmarksMerger.KIND_BOOKMARK, dateAdded, title,
        keyword, url: url ? url.href : null, validity });

    let tags = record.tags;
    if (tags && Array.isArray(tags)) {
      for (let rawTag of tags) {
        let tag = validateTag(rawTag);
        if (!tag) {
          continue;
        }
        await this.db.executeCached(`
          INSERT INTO tags(itemId, tag)
          SELECT id, :tag FROM items
          WHERE guid = :guid`,
          { tag, guid });
      }
    }
  }

  async storeRemoteQuery(record, { needsMerge }) {
    let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);

    let validity = Ci.mozISyncedBookmarksMerger.VALIDITY_VALID;

    let url = validateURL(record.bmkUri);
    if (url) {
      // The query has a valid URL. Determine if we need to rewrite and reupload
      // it.
      let params = new URLSearchParams(url.pathname);
      let type = +params.get("type");
      if (type == Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAG_CONTENTS) {
        // Legacy tag queries with this type use a `place:` URL with a `folder`
        // param that points to the tag folder ID. Rewrite the query to directly
        // reference the tag stored in its `folderName`, then flag the rewritten
        // query for reupload.
        let tagFolderName = validateTag(record.folderName);
        if (tagFolderName) {
          url.href = `place:tag=${tagFolderName}`;
          validity = Ci.mozISyncedBookmarksMerger.VALIDITY_REUPLOAD;
        } else {
          // The tag folder name is invalid, so replace or delete the remote
          // copy.
          url = null;
          validity = Ci.mozISyncedBookmarksMerger.VALIDITY_REPLACE;
        }
      } else {
        let folder = params.get("folder");
        if (folder && !params.has("excludeItems")) {
          // We don't sync enough information to rewrite other queries with a
          // `folder` param (bug 1377175). Referencing a nonexistent folder ID
          // causes the query to return all items in the database, so we add
          // `excludeItems=1` to stop it from doing so. We also flag the
          // rewritten query for reupload.
          url.href = `${url.href}&excludeItems=1`;
          validity = Ci.mozISyncedBookmarksMerger.VALIDITY_REUPLOAD;
        }
      }

      // Other queries are implicitly valid, and don't need to be reuploaded
      // or replaced.

      await this.maybeStoreRemoteURL(url);
    } else {
      // If the query doesn't have a valid URL, we must replace the remote copy
      // with either a valid local copy, or a tombstone if the query doesn't
      // exist locally.
      validity = Ci.mozISyncedBookmarksMerger.VALIDITY_REPLACE;
    }

    let parentGuid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.parentid);
    let serverModified = determineServerModified(record);
    let dateAdded = determineDateAdded(record);
    let title = validateTitle(record.title);

    await this.db.executeCached(`
      REPLACE INTO items(guid, parentGuid, serverModified, needsMerge, kind,
                         dateAdded, title,
                         urlId,
                         validity)
      VALUES(:guid, :parentGuid, :serverModified, :needsMerge, :kind,
             :dateAdded, NULLIF(:title, ""),
             (SELECT id FROM urls
              WHERE hash = hash(:url) AND
                    url = :url),
             :validity)`,
      { guid, parentGuid, serverModified, needsMerge,
        kind: Ci.mozISyncedBookmarksMerger.KIND_QUERY, dateAdded, title,
        url: url ? url.href : null, validity });
  }

  async storeRemoteFolder(record, { needsMerge }) {
    let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);
    let parentGuid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.parentid);
    let serverModified = determineServerModified(record);
    let dateAdded = determineDateAdded(record);
    let title = validateTitle(record.title);

    await this.db.executeCached(`
      REPLACE INTO items(guid, parentGuid, serverModified, needsMerge, kind,
                         dateAdded, title)
      VALUES(:guid, :parentGuid, :serverModified, :needsMerge, :kind,
             :dateAdded, NULLIF(:title, ""))`,
      { guid, parentGuid, serverModified, needsMerge,
        kind: Ci.mozISyncedBookmarksMerger.KIND_FOLDER, dateAdded, title });

    let children = record.children;
    if (children && Array.isArray(children)) {
      for (let [offset, chunk] of PlacesSyncUtils.chunkArray(children,
        SQLITE_MAX_VARIABLE_NUMBER - 1)) {
        // Builds a fragment like `(?2, ?1, 0), (?3, ?1, 1), ...`, where ?1 is
        // the folder's GUID, [?2, ?3] are the first and second child GUIDs
        // (SQLite binding parameters index from 1), and [0, 1] are the
        // positions. This lets us store the folder's children using as few
        // statements as possible.
        let valuesFragment = Array.from({ length: chunk.length },
          (_, index) => `(?${index + 2}, ?1, ${offset + index})`).join(",");
        await this.db.execute(`
          INSERT INTO structure(guid, parentGuid, position)
          VALUES ${valuesFragment}`,
          [guid, ...chunk.map(PlacesSyncUtils.bookmarks.recordIdToGuid)]
        );
      }
    }
  }

  async storeRemoteLivemark(record, { needsMerge }) {
    let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);
    let parentGuid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.parentid);
    let serverModified = determineServerModified(record);
    let feedURL = validateURL(record.feedUri);
    let dateAdded = determineDateAdded(record);
    let title = validateTitle(record.title);
    let siteURL = validateURL(record.siteUri);

    let validity = feedURL ?
      Ci.mozISyncedBookmarksMerger.VALIDITY_VALID :
      Ci.mozISyncedBookmarksMerger.VALIDITY_REPLACE;

    await this.db.executeCached(`
      REPLACE INTO items(guid, parentGuid, serverModified, needsMerge, kind,
                         dateAdded, title, feedURL, siteURL, validity)
      VALUES(:guid, :parentGuid, :serverModified, :needsMerge, :kind,
             :dateAdded, NULLIF(:title, ""), :feedURL, :siteURL, :validity)`,
      { guid, parentGuid, serverModified, needsMerge,
        kind: Ci.mozISyncedBookmarksMerger.KIND_LIVEMARK,
        dateAdded, title, feedURL: feedURL ? feedURL.href : null,
        siteURL: siteURL ? siteURL.href : null, validity });
  }

  async storeRemoteSeparator(record, { needsMerge }) {
    let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);
    let parentGuid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.parentid);
    let serverModified = determineServerModified(record);
    let dateAdded = determineDateAdded(record);

    await this.db.executeCached(`
      REPLACE INTO items(guid, parentGuid, serverModified, needsMerge, kind,
                         dateAdded)
      VALUES(:guid, :parentGuid, :serverModified, :needsMerge, :kind,
             :dateAdded)`,
      { guid, parentGuid, serverModified, needsMerge,
        kind: Ci.mozISyncedBookmarksMerger.KIND_SEPARATOR, dateAdded });
  }

  async storeRemoteTombstone(record, { needsMerge }) {
    let guid = PlacesSyncUtils.bookmarks.recordIdToGuid(record.id);
    let serverModified = determineServerModified(record);

    await this.db.executeCached(`
      REPLACE INTO items(guid, serverModified, needsMerge, isDeleted)
      VALUES(:guid, :serverModified, :needsMerge, 1)`,
      { guid, serverModified, needsMerge });
  }

  async maybeStoreRemoteURL(url) {
    await this.db.executeCached(`
      INSERT OR IGNORE INTO urls(guid, url, hash, revHost)
      VALUES(IFNULL((SELECT guid FROM urls
                     WHERE hash = hash(:url) AND
                                  url = :url),
                    GENERATE_GUID()), :url, hash(:url), :revHost)`,
      { url: url.href, revHost: PlacesUtils.getReversedHost(url) });
  }

  /*
   * Checks if Places or mirror have any unsynced/unmerged changes.
   *
   * @return {Boolean}
   *         `true` if something has changed.
   */
  async hasChanges() {
    // In the first subquery, we check incoming items with needsMerge = true
    // except the tombstones who don't correspond to any local bookmark because
    // we don't store them yet, hence never "merged" (see bug 1343103).
    let rows = await this.db.execute(`
      SELECT
      EXISTS (
       SELECT 1
       FROM items v
       LEFT JOIN moz_bookmarks b ON v.guid = b.guid
       WHERE v.needsMerge AND
       (NOT v.isDeleted OR b.guid NOT NULL)
      ) OR EXISTS (
       WITH RECURSIVE
       ${LocalItemsSQLFragment}
       SELECT 1
       FROM localItems
       WHERE syncChangeCounter > 0
      ) OR EXISTS (
       SELECT 1
       FROM moz_bookmarks_deleted
      )
      AS hasChanges
    `);
    return !!rows[0].getResultByName("hasChanges");
  }

  /**
   * Ensures that all local roots are parented correctly. Misparented roots
   * (bug 1453994, bug 1472127) might produce an invalid tree, so we check
   * before merging, and rely on Places to reparent any invalid roots after
   * the next restart or maintenance run.
   *
   * @return {Boolean}
   *         `true` if the Places root, and four syncable roots, are parented
   *         correctly.
   */
  async validLocalRoots() {
    let rows = await this.db.execute(`
      SELECT EXISTS(SELECT 1 FROM moz_bookmarks
                    WHERE guid = '${PlacesUtils.bookmarks.rootGuid}' AND
                          parent = 0) AND
             (SELECT COUNT(*) FROM moz_bookmarks b
              JOIN moz_bookmarks p ON p.id = b.parent
              WHERE b.guid IN (${PlacesUtils.bookmarks.userContentRoots.map(
                v => `'${v}'`)}) AND
                    p.guid = '${PlacesUtils.bookmarks.rootGuid}') =
             ${PlacesUtils.bookmarks.userContentRoots.length} AS areValid`);
    return !!rows[0].getResultByName("areValid");
  }

  /**
   * Inflates Sync records for all staged outgoing items.
   *
   * @return {Object.<String, BookmarkChangeRecord>}
   *         A changeset containing Sync record cleartexts for outgoing items
   *         and tombstones, keyed by their Sync record IDs.
   */
  async fetchLocalChangeRecords() {
    let changeRecords = {};
    let childRecordIdsByLocalParentId = new Map();
    let tagsByLocalId = new Map();

    let childGuidRows = await this.db.execute(`
      SELECT parentId, guid FROM structureToUpload
      ORDER BY parentId, position`);

    await Async.yieldingForEach(childGuidRows, row => {
      let localParentId = row.getResultByName("parentId");
      let childRecordId = PlacesSyncUtils.bookmarks.guidToRecordId(
        row.getResultByName("guid"));
      let childRecordIds = childRecordIdsByLocalParentId.get(localParentId);
      if (childRecordIds) {
        childRecordIds.push(childRecordId);
      } else {
        childRecordIdsByLocalParentId.set(localParentId, [childRecordId]);
      }
    }, yieldState);

    let tagRows = await this.db.execute(`
      SELECT id, tag FROM tagsToUpload`);

    await Async.yieldingForEach(tagRows, row => {
      let localId = row.getResultByName("id");
      let tag = row.getResultByName("tag");
      let tags = tagsByLocalId.get(localId);
      if (tags) {
        tags.push(tag);
      } else {
        tagsByLocalId.set(localId, [tag]);
      }
    }, yieldState);

    let itemRows = await this.db.execute(`
      SELECT id, syncChangeCounter, guid, isDeleted, type, isQuery,
             tagFolderName, keyword, url, IFNULL(title, "") AS title,
             position, parentGuid,
             IFNULL(parentTitle, "") AS parentTitle, dateAdded
      FROM itemsToUpload`);

    await Async.yieldingForEach(itemRows, row => {
      let syncChangeCounter = row.getResultByName("syncChangeCounter");

      let guid = row.getResultByName("guid");
      let recordId = PlacesSyncUtils.bookmarks.guidToRecordId(guid);

      // Tombstones don't carry additional properties.
      let isDeleted = row.getResultByName("isDeleted");
      if (isDeleted) {
        changeRecords[recordId] = new BookmarkChangeRecord(syncChangeCounter, {
          id: recordId,
          deleted: true,
        });
        return;
      }

      let parentGuid = row.getResultByName("parentGuid");
      let parentRecordId = PlacesSyncUtils.bookmarks.guidToRecordId(parentGuid);

      let type = row.getResultByName("type");
      switch (type) {
        case PlacesUtils.bookmarks.TYPE_BOOKMARK: {
          let isQuery = row.getResultByName("isQuery");
          if (isQuery) {
            let queryCleartext = {
              id: recordId,
              type: "query",
              // We ignore `parentid` and use the parent's `children`, but older
              // Desktops and Android use `parentid` as the canonical parent.
              // iOS is stricter and requires both `children` and `parentid` to
              // match.
              parentid: parentRecordId,
              // Older Desktops use `hasDupe` (along with `parentName` for
              // deduping), if hasDupe is true, then they won't attempt deduping
              // (since they believe that a duplicate for this record should
              // exist). We set it to true to prevent them from applying their
              // deduping logic.
              hasDupe: true,
              parentName: row.getResultByName("parentTitle"),
              // Omit `dateAdded` from the record if it's not set locally.
              dateAdded: row.getResultByName("dateAdded") || undefined,
              bmkUri: row.getResultByName("url"),
              title: row.getResultByName("title"),
              // folderName should never be an empty string or null
              folderName: row.getResultByName("tagFolderName") || undefined,
            };
            changeRecords[recordId] = new BookmarkChangeRecord(
              syncChangeCounter, queryCleartext);
            return;
          }

          let bookmarkCleartext = {
            id: recordId,
            type: "bookmark",
            parentid: parentRecordId,
            hasDupe: true,
            parentName: row.getResultByName("parentTitle"),
            dateAdded: row.getResultByName("dateAdded") || undefined,
            bmkUri: row.getResultByName("url"),
            title: row.getResultByName("title"),
          };
          let keyword = row.getResultByName("keyword");
          if (keyword) {
            bookmarkCleartext.keyword = keyword;
          }
          let localId = row.getResultByName("id");
          let tags = tagsByLocalId.get(localId);
          if (tags) {
            bookmarkCleartext.tags = tags;
          }
          changeRecords[recordId] = new BookmarkChangeRecord(
            syncChangeCounter, bookmarkCleartext);
          return;
        }

        case PlacesUtils.bookmarks.TYPE_FOLDER: {
          let folderCleartext = {
            id: recordId,
            type: "folder",
            parentid: parentRecordId,
            hasDupe: true,
            parentName: row.getResultByName("parentTitle"),
            dateAdded: row.getResultByName("dateAdded") || undefined,
            title: row.getResultByName("title"),
          };
          let localId = row.getResultByName("id");
          let childRecordIds = childRecordIdsByLocalParentId.get(localId);
          folderCleartext.children = childRecordIds || [];
          changeRecords[recordId] = new BookmarkChangeRecord(
            syncChangeCounter, folderCleartext);
          return;
        }

        case PlacesUtils.bookmarks.TYPE_SEPARATOR: {
          let separatorCleartext = {
            id: recordId,
            type: "separator",
            parentid: parentRecordId,
            hasDupe: true,
            parentName: row.getResultByName("parentTitle"),
            dateAdded: row.getResultByName("dateAdded") || undefined,
            // Older Desktops use `pos` for deduping.
            pos: row.getResultByName("position"),
          };
          changeRecords[recordId] = new BookmarkChangeRecord(
            syncChangeCounter, separatorCleartext);
          return;
        }

        default:
          throw new TypeError("Can't create record for unknown Places item");
      }
    }, yieldState);

    return changeRecords;
  }

  /**
   * Closes the mirror database connection. This is called automatically on
   * shutdown, but may also be called explicitly when the mirror is no longer
   * needed.
   */
  finalize() {
    if (!this.finalizePromise) {
      this.finalizePromise = (async () => {
        this.merger.finalize();
        await this.db.close();
        this.finalizeAt.removeBlocker(this.finalizeBound);
      })();
    }
    return this.finalizePromise;
  }
}

this.SyncedBookmarksMirror = SyncedBookmarksMirror;

/** Key names for the key-value `meta` table. */
SyncedBookmarksMirror.META_KEY = {
  LAST_MODIFIED: "collection/lastModified",
  SYNC_ID: "collection/syncId",
};

/**
 * An error thrown when the merge failed for an unexpected reason.
 */
class MergeError extends Error {
  constructor(message) {
    super(message);
    this.name = "MergeError";
  }
}
SyncedBookmarksMirror.MergeError = MergeError;

/**
 * An error thrown when the merge can't proceed because the local tree
 * changed during the merge.
 */
class MergeConflictError extends Error {
  constructor(message) {
    super(message);
    this.name = "MergeConflictError";
  }
}
SyncedBookmarksMirror.MergeConflictError = MergeConflictError;

/**
 * An error thrown when the mirror database is corrupt, or can't be migrated to
 * the latest schema version, and must be replaced.
 */
class DatabaseCorruptError extends Error {
  constructor(message) {
    super(message);
    this.name = "DatabaseCorruptError";
  }
}

// Indicates if the mirror should be replaced because the database file is
// corrupt.
function isDatabaseCorrupt(error) {
  if (error instanceof DatabaseCorruptError) {
    return true;
  }
  if (error.errors) {
    return error.errors.some(error =>
      error instanceof Ci.mozIStorageError &&
      (error.result == Ci.mozIStorageError.CORRUPT ||
       error.result == Ci.mozIStorageError.NOTADB));
  }
  return false;
}

/**
 * Attaches a cloned Places database connection to the mirror database,
 * migrates the mirror schema to the latest version, and creates temporary
 * tables, views, and triggers.
 *
 * @param {Sqlite.OpenedConnection} db
 *        The Places database connection.
 * @param {String} path
 *        The full path to the mirror database file.
 */
async function attachAndInitMirrorDatabase(db, path) {
  await db.execute(`ATTACH :path AS mirror`, { path });
  try {
    await db.executeTransaction(async function() {
      let currentSchemaVersion = await db.getSchemaVersion("mirror");
      if (currentSchemaVersion > 0) {
        if (currentSchemaVersion < MIRROR_SCHEMA_VERSION) {
          await migrateMirrorSchema(db, currentSchemaVersion);
        }
      } else {
        await initializeMirrorDatabase(db);
      }
      // Downgrading from a newer profile to an older profile rolls back the
      // schema version, but leaves all new columns in place. We'll run the
      // migration logic again on the next upgrade.
      await db.setSchemaVersion(MIRROR_SCHEMA_VERSION, "mirror");
      await initializeTempMirrorEntities(db);
    });
  } catch (ex) {
    await db.execute(`DETACH mirror`);
    throw ex;
  }
}

/**
 * Migrates the mirror database schema to the latest version.
 *
 * @param {Sqlite.OpenedConnection} db
 *        The mirror database connection.
 * @param {Number} currentSchemaVersion
 *        The current mirror database schema version.
 */
async function migrateMirrorSchema(db, currentSchemaVersion) {
  if (currentSchemaVersion < 5) {
    // The mirror was pref'd off by default for schema versions 1-4.
    throw new DatabaseCorruptError(`Can't migrate from schema version ${
      currentSchemaVersion}; too old`);
  }
}

/**
 * Initializes a new mirror database, creating persistent tables, indexes, and
 * roots.
 *
 * @param {Sqlite.OpenedConnection} db
 *        The mirror database connection.
 */
async function initializeMirrorDatabase(db) {
  // Key-value metadata table. Stores the server collection last modified time
  // and sync ID.
  await db.execute(`CREATE TABLE mirror.meta(
    key TEXT PRIMARY KEY,
    value NOT NULL
  ) WITHOUT ROWID`);

  // Note: description and loadInSidebar are not used as of Firefox 63, but
  // remain to avoid rebuilding the database if the user happens to downgrade.
  await db.execute(`CREATE TABLE mirror.items(
    id INTEGER PRIMARY KEY,
    guid TEXT UNIQUE NOT NULL,
    /* The "parentid" from the record. */
    parentGuid TEXT,
    /* The server modified time, in milliseconds. */
    serverModified INTEGER NOT NULL DEFAULT 0,
    needsMerge BOOLEAN NOT NULL DEFAULT 0,
    validity INTEGER NOT NULL DEFAULT ${
      Ci.mozISyncedBookmarksMerger.VALIDITY_VALID},
    isDeleted BOOLEAN NOT NULL DEFAULT 0,
    kind INTEGER NOT NULL DEFAULT -1,
    /* The creation date, in milliseconds. */
    dateAdded INTEGER NOT NULL DEFAULT 0,
    title TEXT,
    urlId INTEGER REFERENCES urls(id)
                  ON DELETE SET NULL,
    keyword TEXT,
    description TEXT,
    loadInSidebar BOOLEAN,
    smartBookmarkName TEXT,
    feedURL TEXT,
    siteURL TEXT
  )`);

  await db.execute(`CREATE TABLE mirror.structure(
    guid TEXT,
    parentGuid TEXT REFERENCES items(guid)
                    ON DELETE CASCADE,
    position INTEGER NOT NULL,
    PRIMARY KEY(parentGuid, guid)
  ) WITHOUT ROWID`);

  await db.execute(`CREATE TABLE mirror.urls(
    id INTEGER PRIMARY KEY,
    guid TEXT NOT NULL,
    url TEXT NOT NULL,
    hash INTEGER NOT NULL,
    revHost TEXT NOT NULL
  )`);

  await db.execute(`CREATE TABLE mirror.tags(
    itemId INTEGER NOT NULL REFERENCES items(id)
                            ON DELETE CASCADE,
    tag TEXT NOT NULL
  )`);

  await db.execute(`CREATE INDEX mirror.urlHashes ON urls(hash)`);

  await createMirrorRoots(db);
}

/**
 * Sets up the syncable roots. All items in the mirror we apply will descend
 * from these roots - however, malformed records from the server which create
 * a different root *will* be created in the mirror - just not applied.
 *
 *
 * @param {Sqlite.OpenedConnection} db
 *        The mirror database connection.
 */
async function createMirrorRoots(db) {
  const syncableRoots = [{
    guid: PlacesUtils.bookmarks.rootGuid,
    // The Places root is its own parent, to satisfy the foreign key and
    // `NOT NULL` constraints on `structure`.
    parentGuid: PlacesUtils.bookmarks.rootGuid,
    position: -1,
    needsMerge: false,
  }, ...PlacesUtils.bookmarks.userContentRoots.map((guid, position) => {
    return {
      guid,
      parentGuid: PlacesUtils.bookmarks.rootGuid,
      position,
      needsMerge: true,
    };
  })];

  for (let { guid, parentGuid, position, needsMerge } of syncableRoots) {
    await db.executeCached(`
      INSERT INTO items(guid, parentGuid, kind, needsMerge)
      VALUES(:guid, :parentGuid, :kind, :needsMerge)`,
      { guid, parentGuid, kind: Ci.mozISyncedBookmarksMerger.KIND_FOLDER,
        needsMerge });

    await db.executeCached(`
      INSERT INTO structure(guid, parentGuid, position)
      VALUES(:guid, :parentGuid, :position)`,
      { guid, parentGuid, position });
  }
}

/**
 * Creates temporary tables, views, and triggers to apply the mirror to Places.
 *
 * The bulk of the logic to apply all remotely changed items is defined in
 * `INSTEAD OF DELETE` triggers on the `itemsToMerge` and `structureToMerge`
 * views. When we execute `DELETE FROM newRemote{Items, Structure}`, SQLite
 * fires the triggers for each row in the view. This is equivalent to, but more
 * efficient than, issuing `SELECT * FROM newRemote{Items, Structure}`,
 * followed by separate `INSERT` and `UPDATE` statements.
 *
 * Using triggers to execute all these statements avoids the overhead of passing
 * results between the storage and main threads, and wrapping each result row in
 * a `mozStorageRow` object.
 *
 * @param {Sqlite.OpenedConnection} db
 *        The mirror database connection.
 */
async function initializeTempMirrorEntities(db) {
  // Stores the value and structure states of all nodes in the merged tree.
  await db.execute(`CREATE TEMP TABLE mergeStates(
    mergedGuid TEXT PRIMARY KEY,
    localGuid TEXT,
    remoteGuid TEXT,
    mergedParentGuid TEXT NOT NULL,
    level INTEGER NOT NULL,
    position INTEGER NOT NULL,
    useRemote BOOLEAN NOT NULL, /* Take the remote state when merging? */
    shouldUpload BOOLEAN NOT NULL, /* Flag the item for upload? */
    /* The node should exist on at least one side. */
    CHECK(localGuid NOT NULL OR remoteGuid NOT NULL)
  ) WITHOUT ROWID`);

  // Stages all items to delete locally and remotely. Items to delete locally
  // don't need tombstones: since we took the remote deletion, the tombstone
  // already exists on the server. Items to delete remotely, or non-syncable
  // items to delete on both sides, need tombstones.
  await db.execute(`CREATE TEMP TABLE itemsToRemove(
    guid TEXT PRIMARY KEY,
    localLevel INTEGER NOT NULL,
    shouldUploadTombstone BOOLEAN NOT NULL
  ) WITHOUT ROWID`);

  await db.execute(`
    CREATE TEMP TRIGGER noteItemRemoved
    AFTER INSERT ON itemsToRemove
    BEGIN
      /* Note that we can't record item removed notifications in the
         "removeLocalItems" trigger, because SQLite can delete rows in any
         order, and might fire the trigger for a removed parent before its
         children. */
      INSERT INTO itemsRemoved(itemId, parentId, position, type, placeId,
                               guid, parentGuid, level)
      SELECT b.id, b.parent, b.position, b.type, b.fk, b.guid, p.guid,
             NEW.localLevel
      FROM moz_bookmarks b
      JOIN moz_bookmarks p ON p.id = b.parent
      WHERE b.guid = NEW.guid;
    END`);

  // Removes items that are deleted on one or both sides from Places, and
  // inserts new tombstones for non-syncable items to delete remotely.
  await db.execute(`
    CREATE TEMP TRIGGER removeLocalItems
    AFTER DELETE ON itemsToRemove
    BEGIN
      /* Flag URL frecency for recalculation. */
      UPDATE moz_places SET
        frecency = -frecency
      WHERE id = (SELECT fk FROM moz_bookmarks
                  WHERE guid = OLD.guid) AND
            frecency > 0;

      /* Trigger frecency updates for all affected origins. */
      DELETE FROM moz_updateoriginsupdate_temp;

      /* Remove annos for the deleted items. This can be removed in bug
         1460577. */
      DELETE FROM moz_items_annos
      WHERE item_id = (SELECT id FROM moz_bookmarks
                       WHERE guid = OLD.guid);

      /* Don't reupload tombstones for items that are already deleted on the
         server. */
      DELETE FROM moz_bookmarks_deleted
      WHERE NOT OLD.shouldUploadTombstone AND
            guid = OLD.guid;

      /* Upload tombstones for non-syncable items. We can remove the
         "shouldUploadTombstone" check and persist tombstones unconditionally
         in bug 1343103. */
      INSERT OR IGNORE INTO moz_bookmarks_deleted(guid, dateRemoved)
      SELECT OLD.guid, STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000
      WHERE OLD.shouldUploadTombstone;

      /* Remove the item from Places. */
      DELETE FROM moz_bookmarks
      WHERE guid = OLD.guid;

      /* Flag applied deletions as merged. */
      UPDATE items SET
        needsMerge = 0
      WHERE needsMerge AND
            guid = OLD.guid AND
            /* Don't flag tombstones for items that don't exist in the local
               tree. This can be removed once we persist tombstones in bug
               1343103. */
            (NOT isDeleted OR OLD.localLevel > -1);
    END`);

  // A view of the value states for all bookmarks in the mirror. We use triggers
  // on this view to update Places. Note that we can't just `REPLACE INTO
  // moz_bookmarks`, because `REPLACE` doesn't fire the `AFTER DELETE` triggers
  // that Places uses to maintain schema coherency.
  await db.execute(`
    CREATE TEMP VIEW itemsToMerge(localId, localGuid, remoteId, remoteGuid,
                                  mergedGuid, useRemote, shouldUpload, newLevel,
                                  newType,
                                  newDateAddedMicroseconds,
                                  newTitle, oldPlaceId, newPlaceId,
                                  newKeyword) AS
    SELECT b.id, b.guid, v.id, v.guid,
           r.mergedGuid, r.useRemote, r.shouldUpload, r.level,
           (CASE WHEN v.kind IN (${[
                        Ci.mozISyncedBookmarksMerger.KIND_BOOKMARK,
                        Ci.mozISyncedBookmarksMerger.KIND_QUERY,
                      ].join(",")}) THEN ${PlacesUtils.bookmarks.TYPE_BOOKMARK}
                 WHEN v.kind IN (${[
                        Ci.mozISyncedBookmarksMerger.KIND_FOLDER,
                        Ci.mozISyncedBookmarksMerger.KIND_LIVEMARK,
                      ].join(",")}) THEN ${PlacesUtils.bookmarks.TYPE_FOLDER}
                 ELSE ${PlacesUtils.bookmarks.TYPE_SEPARATOR} END),
           /* Take the older creation date. "b.dateAdded" is in microseconds;
              "v.dateAdded" is in milliseconds. */
           (CASE WHEN b.dateAdded / 1000 < v.dateAdded THEN b.dateAdded
                 ELSE v.dateAdded * 1000 END),
           v.title, h.id, (SELECT n.id FROM moz_places n
                           WHERE n.url_hash = u.hash AND
                                 n.url = u.url),
           v.keyword
    FROM mergeStates r
    LEFT JOIN items v ON v.guid = r.remoteGuid
    LEFT JOIN moz_bookmarks b ON b.guid = r.localGuid
    LEFT JOIN moz_places h ON h.id = b.fk
    LEFT JOIN urls u ON u.id = v.urlId
    WHERE r.mergedGuid <> '${PlacesUtils.bookmarks.rootGuid}'`);

  // Changes local GUIDs to remote GUIDs, drops local tombstones for revived
  // remote items, and flags remote items as merged. In the trigger body, `OLD`
  // refers to the row for the unmerged item in `itemsToMerge`.
  await db.execute(`
    CREATE TEMP TRIGGER updateGuidsAndSyncFlags
    INSTEAD OF DELETE ON itemsToMerge
    BEGIN
      UPDATE moz_bookmarks SET
        /* We update GUIDs here, instead of in the "updateExistingLocalItems"
           trigger, because deduped items where we're keeping the local value
           state won't have "useRemote" set. */
        guid = OLD.mergedGuid,
        syncStatus = CASE WHEN OLD.useRemote
                     THEN ${PlacesUtils.bookmarks.SYNC_STATUS.NORMAL}
                     ELSE syncStatus
                     END,
        /* Flag updated local items and new structure for upload. */
        syncChangeCounter = OLD.shouldUpload,
        lastModified = STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000
      WHERE id = OLD.localId;

      /* Record item changed notifications for the updated GUIDs. */
      INSERT INTO guidsChanged(itemId, oldGuid, level)
      SELECT OLD.localId, OLD.localGuid, OLD.newLevel
      WHERE OLD.localGuid <> OLD.mergedGuid;

      /* Drop local tombstones for revived remote items. */
      DELETE FROM moz_bookmarks_deleted
      WHERE guid IN (OLD.localGuid, OLD.remoteGuid);

      /* Flag the remote item as merged. */
      UPDATE items SET
        needsMerge = 0
      WHERE needsMerge AND
            guid IN (OLD.remoteGuid, OLD.localGuid);
    END`);

  await db.execute(`
    CREATE TEMP TRIGGER updateLocalItems
    INSTEAD OF DELETE ON itemsToMerge WHEN OLD.useRemote
    BEGIN
      /* Record an item added notification for the new item. */
      INSERT INTO itemsAdded(guid, keywordChanged, level)
      SELECT OLD.mergedGuid, OLD.newKeyword NOT NULL OR
                             EXISTS(SELECT 1 FROM moz_keywords
                                    WHERE place_id = OLD.newPlaceId OR
                                          keyword = OLD.newKeyword),
             OLD.newLevel
      WHERE OLD.localId IS NULL;

      /* Record an item changed notification for the existing item. */
      INSERT INTO itemsChanged(itemId, oldTitle, oldPlaceId, keywordChanged,
                               level)
      SELECT id, title, OLD.oldPlaceId, OLD.newKeyword NOT NULL OR
               EXISTS(SELECT 1 FROM moz_keywords
                      WHERE place_id IN (OLD.oldPlaceId, OLD.newPlaceId) OR
                            keyword = OLD.newKeyword),
             OLD.newLevel
      FROM moz_bookmarks
      WHERE OLD.localId NOT NULL AND
            id = OLD.localId;

      /* Sync associates keywords with bookmarks, and doesn't sync POST data;
         Places associates keywords with (URL, POST data) pairs, and multiple
         bookmarks may have the same URL. For consistency (bug 1328737), we
         reupload all items with the old URL, new URL, and new keyword. Note
         that we intentionally use "k.place_id IN (...)" instead of
         "b.fk = OLD.newPlaceId OR fk IN (...)" in the WHERE clause because we
         only want to reupload items with keywords. */
      INSERT OR IGNORE INTO relatedIdsToReupload(id)
      SELECT b.id FROM moz_bookmarks b
      JOIN moz_keywords k ON k.place_id = b.fk
      WHERE (b.id <> OLD.localId OR OLD.localId IS NULL) AND (
              k.place_id IN (OLD.oldPlaceId, OLD.newPlaceId) OR
              k.keyword = OLD.newKeyword
            );

      /* Remove all keywords from the old and new URLs, and remove the new
         keyword from all existing URLs. */
      DELETE FROM moz_keywords WHERE place_id IN (OLD.oldPlaceId,
                                                  OLD.newPlaceId) OR
                                     keyword = OLD.newKeyword;

      /* Remove existing tags. */
      DELETE FROM localTags WHERE placeId IN (OLD.oldPlaceId, OLD.newPlaceId);

      /* Insert the new item, using "-1" as the placeholder parent and
         position. We'll update these later, in the "updateLocalStructure"
         trigger. */
      INSERT INTO moz_bookmarks(id, guid, parent, position, type, fk, title,
                                dateAdded, lastModified, syncStatus,
                                syncChangeCounter)
      VALUES(OLD.localId, OLD.mergedGuid, -1, -1, OLD.newType, OLD.newPlaceId,
             OLD.newTitle, OLD.newDateAddedMicroseconds,
             STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000,
             ${PlacesUtils.bookmarks.SYNC_STATUS.NORMAL}, OLD.shouldUpload)
      ON CONFLICT(id) DO UPDATE SET
        title = excluded.title,
        dateAdded = excluded.dateAdded,
        lastModified = excluded.lastModified,
        /* It's important that we update the URL *after* removing old keywords
           and *before* inserting new ones, so that the above DELETEs select
           the correct affected items. */
        fk = excluded.fk;

      /* Recalculate frecency. */
      UPDATE moz_places SET
        frecency = -frecency
      WHERE OLD.oldPlaceId <> OLD.newPlaceId AND
            id IN (OLD.oldPlaceId, OLD.newPlaceId) AND
            frecency > 0;

      /* Trigger frecency updates for all affected origins. */
      DELETE FROM moz_updateoriginsupdate_temp;

      /* Insert a new keyword for the new URL, if one is set. */
      INSERT OR IGNORE INTO moz_keywords(keyword, place_id, post_data)
      SELECT OLD.newKeyword, OLD.newPlaceId, ''
      WHERE OLD.newKeyword NOT NULL;

      /* Insert new tags for the new URL. */
      INSERT INTO localTags(tag, placeId)
      SELECT t.tag, OLD.newPlaceId FROM tags t
      WHERE t.itemId = OLD.remoteId;
    END`);

  // A view of the structure states for all items in the merged tree. The
  // mirror stores structure info in a separate table, like iOS, while Places
  // stores structure info on children. Unlike iOS, we can't simply check the
  // parent's merge state to know if its children changed. This is because our
  // merged tree might diverge from the mirror if we're missing children, or if
  // we temporarily reparented children without parents to "unfiled". In that
  // case, we want to keep syncing, but *don't* want to reupload the new local
  // structure to the server.
  await db.execute(`
    CREATE TEMP VIEW structureToMerge(localId, oldParentId, newParentId,
                                      oldPosition, newPosition, newLevel) AS
    SELECT b.id, b.parent, p.id, b.position, r.position, r.level
    FROM moz_bookmarks b
    JOIN mergeStates r ON r.mergedGuid = b.guid
    JOIN moz_bookmarks p ON p.guid = r.mergedParentGuid
    /* Don't reposition roots, since we never upload the Places root, and our
       merged tree doesn't have a tags root. */
    WHERE '${PlacesUtils.bookmarks.rootGuid}' NOT IN (r.mergedGuid,
                                                      r.mergedParentGuid)`);

  // Updates all parents and positions to reflect the merged tree.
  await db.execute(`
    CREATE TEMP TRIGGER updateLocalStructure
    INSTEAD OF DELETE ON structureToMerge
    BEGIN
      UPDATE moz_bookmarks SET
        parent = OLD.newParentId
      WHERE id = OLD.localId AND
            parent <> OLD.newParentId;

      UPDATE moz_bookmarks SET
        position = OLD.newPosition
      WHERE id = OLD.localId AND
            position <> OLD.newPosition;

      /* Record observer notifications for moved items. We ignore items that
         didn't move, and items with placeholder parents and positions of "-1",
         since they're new. */
      INSERT INTO itemsMoved(itemId, oldParentId, oldParentGuid, oldPosition,
                             level)
      SELECT OLD.localId, OLD.oldParentId, p.guid, OLD.oldPosition,
             OLD.newLevel
      FROM moz_bookmarks p
      WHERE p.id = OLD.oldParentId AND
            -1 NOT IN (OLD.oldParentId, OLD.oldPosition) AND
            (OLD.oldParentId <> OLD.newParentId OR
             OLD.oldPosition <> OLD.newPosition);
    END`);

  // A view of local bookmark tags. Tags, like keywords, are associated with
  // URLs, so two bookmarks with the same URL should have the same tags. Unlike
  // keywords, one tag may be associated with many different URLs. Tags are also
  // different because they're implemented as bookmarks under the hood. Each tag
  // is stored as a folder under the tags root, and tagged URLs are stored as
  // untitled bookmarks under these folders. This complexity can be removed once
  // bug 424160 lands.
  await db.execute(`
    CREATE TEMP VIEW localTags(tagEntryId, tagEntryGuid, tagFolderId,
                               tagFolderGuid, tagEntryPosition, tagEntryType,
                               tag, placeId) AS
    SELECT b.id, b.guid, p.id, p.guid, b.position, b.type, p.title, b.fk
    FROM moz_bookmarks b
    JOIN moz_bookmarks p ON p.id = b.parent
    JOIN moz_bookmarks r ON r.id = p.parent
    WHERE b.type = ${PlacesUtils.bookmarks.TYPE_BOOKMARK} AND
          r.guid = '${PlacesUtils.bookmarks.tagsGuid}'`);

  // Untags a URL by removing its tag entry.
  await db.execute(`
    CREATE TEMP TRIGGER untagLocalPlace
    INSTEAD OF DELETE ON localTags
    BEGIN
      /* Record an item removed notification for the tag entry. */
      INSERT INTO itemsRemoved(itemId, parentId, position, type, placeId, guid,
                               parentGuid, isUntagging)
      VALUES(OLD.tagEntryId, OLD.tagFolderId, OLD.tagEntryPosition,
             OLD.tagEntryType, OLD.placeId, OLD.tagEntryGuid,
             OLD.tagFolderGuid, 1);

      DELETE FROM moz_bookmarks WHERE id = OLD.tagEntryId;

      /* Fix the positions of the sibling tag entries. */
      UPDATE moz_bookmarks SET
        position = position - 1
      WHERE parent = OLD.tagFolderId AND
            position > OLD.tagEntryPosition;
    END`);

  // Tags a URL by creating a tag folder if it doesn't exist, then inserting a
  // tag entry for the URL into the tag folder. `NEW.placeId` can be NULL, in
  // which case we'll just create the tag folder.
  await db.execute(`
    CREATE TEMP TRIGGER tagLocalPlace
    INSTEAD OF INSERT ON localTags
    BEGIN
      /* Ensure the tag folder exists. */
      INSERT OR IGNORE INTO moz_bookmarks(guid, parent, position, type, title,
                                          dateAdded, lastModified)
      VALUES(IFNULL((SELECT b.guid FROM moz_bookmarks b
                     JOIN moz_bookmarks p ON p.id = b.parent
                     WHERE b.title = NEW.tag AND
                           p.guid = '${PlacesUtils.bookmarks.tagsGuid}'),
                    GENERATE_GUID()),
             (SELECT id FROM moz_bookmarks
              WHERE guid = '${PlacesUtils.bookmarks.tagsGuid}'),
             (SELECT COUNT(*) FROM moz_bookmarks b
              JOIN moz_bookmarks p ON p.id = b.parent
              WHERE p.guid = '${PlacesUtils.bookmarks.tagsGuid}'),
             ${PlacesUtils.bookmarks.TYPE_FOLDER}, NEW.tag,
             STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000,
             STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000);

      /* Record an item added notification if we created a tag folder.
         "CHANGES()" returns the number of rows affected by the INSERT above:
         1 if we created the folder, or 0 if the folder already existed. */
      INSERT INTO itemsAdded(guid, isTagging)
      SELECT b.guid, 1 FROM moz_bookmarks b
      JOIN moz_bookmarks p ON p.id = b.parent
      WHERE CHANGES() > 0 AND
            b.title = NEW.tag AND
            p.guid = '${PlacesUtils.bookmarks.tagsGuid}';

      /* Add a tag entry for the URL under the tag folder. Omitting the place
         ID creates a tag folder without tagging the URL. */
      INSERT OR IGNORE INTO moz_bookmarks(guid, parent, position, type, fk,
                                          dateAdded, lastModified)
      SELECT GENERATE_GUID(),
             (SELECT b.id FROM moz_bookmarks b
              JOIN moz_bookmarks p ON p.id = b.parent
              WHERE p.guid = '${PlacesUtils.bookmarks.tagsGuid}' AND
                    b.title = NEW.tag),
             (SELECT COUNT(*) FROM moz_bookmarks b
              JOIN moz_bookmarks p ON p.id = b.parent
              JOIN moz_bookmarks r ON r.id = p.parent
              WHERE p.title = NEW.tag AND
                    r.guid = '${PlacesUtils.bookmarks.tagsGuid}'),
             ${PlacesUtils.bookmarks.TYPE_BOOKMARK}, NEW.placeId,
             STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000,
             STRFTIME('%s', 'now', 'localtime', 'utc') * 1000000
      WHERE NEW.placeId NOT NULL;

      /* Record an item added notification for the tag entry. */
      INSERT INTO itemsAdded(guid, isTagging)
      SELECT b.guid, 1 FROM moz_bookmarks b
      JOIN moz_bookmarks p ON p.id = b.parent
      JOIN moz_bookmarks r ON r.id = p.parent
      WHERE b.fk = NEW.placeId AND
            p.title = NEW.tag AND
            r.guid = '${PlacesUtils.bookmarks.tagsGuid}';
    END`);

  // Stores properties to pass to `onItem{Added, Changed, Moved, Removed}`
  // bookmark observers for new, updated, moved, and deleted items.
  await db.execute(`CREATE TEMP TABLE itemsAdded(
    guid TEXT PRIMARY KEY,
    isTagging BOOLEAN NOT NULL DEFAULT 0,
    keywordChanged BOOLEAN NOT NULL DEFAULT 0,
    level INTEGER NOT NULL DEFAULT -1
  ) WITHOUT ROWID`);

  await db.execute(`CREATE TEMP TABLE guidsChanged(
    itemId INTEGER NOT NULL,
    oldGuid TEXT NOT NULL,
    level INTEGER NOT NULL DEFAULT -1,
    PRIMARY KEY(itemId, oldGuid)
  ) WITHOUT ROWID`);

  await db.execute(`CREATE TEMP TABLE itemsChanged(
    itemId INTEGER PRIMARY KEY,
    oldTitle TEXT,
    oldPlaceId INTEGER,
    keywordChanged BOOLEAN NOT NULL DEFAULT 0,
    level INTEGER NOT NULL DEFAULT -1
  )`);

  await db.execute(`CREATE TEMP TABLE itemsMoved(
    itemId INTEGER PRIMARY KEY,
    oldParentId INTEGER NOT NULL,
    oldParentGuid TEXT NOT NULL,
    oldPosition INTEGER NOT NULL,
    level INTEGER NOT NULL DEFAULT -1
  )`);

  await db.execute(`CREATE TEMP TABLE itemsRemoved(
    guid TEXT PRIMARY KEY,
    itemId INTEGER NOT NULL,
    parentId INTEGER NOT NULL,
    position INTEGER NOT NULL,
    type INTEGER NOT NULL,
    placeId INTEGER,
    parentGuid TEXT NOT NULL,
    /* We record the original level of the removed item in the tree so that we
       can notify children before parents. */
    level INTEGER NOT NULL DEFAULT -1,
    isUntagging BOOLEAN NOT NULL DEFAULT 0
  ) WITHOUT ROWID`);

  // Stores local IDs for items to upload even if they're not flagged as changed
  // in Places. These are "weak" because we won't try to reupload the item on
  // the next sync if the upload is interrupted or fails.
  await db.execute(`CREATE TEMP TABLE idsToWeaklyUpload(
    id INTEGER PRIMARY KEY
  )`);

  // Stores local IDs for items to reupload. Removing an
  // ID from this table bumps its local change counter, so, unlike weak uploads,
  // we *will* reupload the item on the next sync if the current sync fails.
  // This is used to ensure that all bookmarks with the same URL have the same keyword (bug 1328737).
  await db.execute(`CREATE TEMP TABLE relatedIdsToReupload(
    id INTEGER PRIMARY KEY
  )`);

  await db.execute(`
    CREATE TEMP TRIGGER reuploadIds
    AFTER DELETE ON relatedIdsToReupload
    BEGIN
      UPDATE moz_bookmarks SET
        syncChangeCounter = syncChangeCounter + 1
      WHERE id = OLD.id;
    END`);

  // Stores locally changed items staged for upload.
  await db.execute(`CREATE TEMP TABLE itemsToUpload(
    id INTEGER PRIMARY KEY,
    guid TEXT UNIQUE NOT NULL,
    syncChangeCounter INTEGER NOT NULL,
    isDeleted BOOLEAN NOT NULL DEFAULT 0,
    parentGuid TEXT,
    parentTitle TEXT,
    dateAdded INTEGER, /* In milliseconds. */
    type INTEGER,
    title TEXT,
    placeId INTEGER,
    isQuery BOOLEAN NOT NULL DEFAULT 0,
    url TEXT,
    tagFolderName TEXT,
    keyword TEXT,
    position INTEGER
  )`);

  await db.execute(`CREATE TEMP TABLE structureToUpload(
    guid TEXT PRIMARY KEY,
    parentId INTEGER NOT NULL REFERENCES itemsToUpload(id)
                              ON DELETE CASCADE,
    position INTEGER NOT NULL
  ) WITHOUT ROWID`);

  await db.execute(`CREATE TEMP TABLE tagsToUpload(
    id INTEGER REFERENCES itemsToUpload(id)
               ON DELETE CASCADE,
    tag TEXT,
    PRIMARY KEY(id, tag)
  ) WITHOUT ROWID`);
}

async function resetMirror(db) {
  await db.execute(`DELETE FROM meta`);
  await db.execute(`DELETE FROM structure`);
  await db.execute(`DELETE FROM items`);
  await db.execute(`DELETE FROM urls`);

  // Since we need to reset the modified times and merge flags for the syncable
  // roots, we simply delete and recreate them.
  await createMirrorRoots(db);
}

// Converts a Sync record's last modified time to milliseconds.
function determineServerModified(record) {
  return Math.max(record.modified * 1000, 0) || 0;
}

// Determines a Sync record's creation date.
function determineDateAdded(record) {
  let serverModified = determineServerModified(record);
  return PlacesSyncUtils.bookmarks.ratchetTimestampBackwards(
    record.dateAdded, serverModified);
}

function validateTitle(rawTitle) {
  if (typeof rawTitle != "string" || !rawTitle) {
    return null;
  }
  return rawTitle.slice(0, DB_TITLE_LENGTH_MAX);
}

function validateURL(rawURL) {
  if (typeof rawURL != "string" || rawURL.length > DB_URL_LENGTH_MAX) {
    return null;
  }
  let url = null;
  try {
    url = new URL(rawURL);
  } catch (ex) {}
  return url;
}

function validateKeyword(rawKeyword) {
  if (typeof rawKeyword != "string") {
    return null;
  }
  let keyword = rawKeyword.trim();
  // Drop empty keywords.
  return keyword ? keyword.toLowerCase() : null;
}

function validateTag(rawTag) {
  if (typeof rawTag != "string") {
    return null;
  }
  let tag = rawTag.trim();
  if (!tag || tag.length > PlacesUtils.bookmarks.MAX_TAG_LENGTH) {
    // Drop empty and oversized tags.
    return null;
  }
  return tag;
}

/**
 * Measures and logs the time taken to execute a function, using a monotonic
 * clock.
 *
 * @param  {String} name
 *         The name of the operation, used for logging.
 * @param  {Function} func
 *         The function to time.
 * @param  {Function} [recordTiming]
 *         An optional function with the signature `(time: Number)`, where
 *         `time` is the measured time.
 * @return The return value of the timed function.
 */
async function withTiming(name, func, recordTiming) {
  MirrorLog.debug(name);

  let startTime = Cu.now();
  let result = await func();
  let elapsedTime = Cu.now() - startTime;

  MirrorLog.trace(`${name} took ${elapsedTime.toFixed(3)}ms`);
  if (typeof recordTiming == "function") {
    recordTiming(elapsedTime);
  }

  return result;
}

/**
 * Fires bookmark and keyword observer notifications for all changes made during
 * the merge.
 */
class BookmarkObserverRecorder {
  constructor(db, { maxFrecenciesToRecalculate }) {
    this.db = db;
    this.maxFrecenciesToRecalculate = maxFrecenciesToRecalculate;
    this.bookmarkObserverNotifications = [];
    this.shouldInvalidateKeywords = false;
  }

  /**
   * Fires observer notifications for all changed items, invalidates the
   * livemark cache if necessary, and recalculates frecencies for changed
   * URLs. This is called outside the merge transaction.
   */
  async notifyAll() {
    await this.noteAllChanges();
    if (this.shouldInvalidateKeywords) {
      await PlacesUtils.keywords.invalidateCachedKeywords();
    }
    await this.notifyBookmarkObservers();
    await updateFrecencies(this.db, this.maxFrecenciesToRecalculate);
  }

  /**
   * Records Places observer notifications for removed, added, moved, and
   * changed items.
   */
  async noteAllChanges() {
    MirrorLog.trace("Recording observer notifications for removed items");
    // `ORDER BY v.level DESC` sorts deleted children before parents, to ensure
    // that we update caches in the correct order (bug 1297941). We also order
    // by parent and position so that the notifications are well-ordered for
    // tests.
    let removedItemRows = await this.db.execute(`
      SELECT v.itemId AS id, v.parentId, v.parentGuid, v.position, v.type,
             h.url, v.guid, v.isUntagging
      FROM itemsRemoved v
      LEFT JOIN moz_places h ON h.id = v.placeId
      ORDER BY v.level DESC, v.parentId, v.position`);
    await Async.yieldingForEach(removedItemRows, row => {
      let info = {
        id: row.getResultByName("id"),
        parentId: row.getResultByName("parentId"),
        position: row.getResultByName("position"),
        type: row.getResultByName("type"),
        urlHref: row.getResultByName("url"),
        guid: row.getResultByName("guid"),
        parentGuid: row.getResultByName("parentGuid"),
        isUntagging: row.getResultByName("isUntagging"),
      };
      this.noteItemRemoved(info);
    }, yieldState);

    MirrorLog.trace("Recording observer notifications for changed GUIDs");
    let changedGuidRows = await this.db.execute(`
      SELECT b.id, b.lastModified, b.type, b.guid AS newGuid,
             c.oldGuid, p.id AS parentId, p.guid AS parentGuid
      FROM guidsChanged c
      JOIN moz_bookmarks b ON b.id = c.itemId
      JOIN moz_bookmarks p ON p.id = b.parent
      ORDER BY c.level, p.id, b.position`);
    await Async.yieldingForEach(changedGuidRows, row => {
      let info = {
        id: row.getResultByName("id"),
        lastModified: row.getResultByName("lastModified"),
        type: row.getResultByName("type"),
        newGuid: row.getResultByName("newGuid"),
        oldGuid: row.getResultByName("oldGuid"),
        parentId: row.getResultByName("parentId"),
        parentGuid: row.getResultByName("parentGuid"),
      };
      this.noteGuidChanged(info);
    }, yieldState);

    MirrorLog.trace("Recording observer notifications for new items");
    let newItemRows = await this.db.execute(`
      SELECT b.id, p.id AS parentId, b.position, b.type, h.url,
             IFNULL(b.title, "") AS title, b.dateAdded, b.guid,
             p.guid AS parentGuid, n.isTagging
      FROM itemsAdded n
      JOIN moz_bookmarks b ON b.guid = n.guid
      JOIN moz_bookmarks p ON p.id = b.parent
      LEFT JOIN moz_places h ON h.id = b.fk
      ORDER BY n.level, p.id, b.position`);
    await Async.yieldingForEach(newItemRows, row => {
      let info = {
        id: row.getResultByName("id"),
        parentId: row.getResultByName("parentId"),
        position: row.getResultByName("position"),
        type: row.getResultByName("type"),
        urlHref: row.getResultByName("url"),
        title: row.getResultByName("title"),
        dateAdded: row.getResultByName("dateAdded"),
        guid: row.getResultByName("guid"),
        parentGuid: row.getResultByName("parentGuid"),
        isTagging: row.getResultByName("isTagging"),
      };
      this.noteItemAdded(info);
    }, yieldState);

    MirrorLog.trace("Recording observer notifications for moved items");
    let movedItemRows = await this.db.execute(`
      SELECT b.id, b.guid, b.type, p.id AS newParentId, c.oldParentId,
             p.guid AS newParentGuid, c.oldParentGuid,
             b.position AS newPosition, c.oldPosition, h.url
      FROM itemsMoved c
      JOIN moz_bookmarks b ON b.id = c.itemId
      JOIN moz_bookmarks p ON p.id = b.parent
      LEFT JOIN moz_places h ON h.id = b.fk
      ORDER BY c.level, newParentId, newPosition`);
    await Async.yieldingForEach(movedItemRows, row => {
      let info = {
        id: row.getResultByName("id"),
        guid: row.getResultByName("guid"),
        type: row.getResultByName("type"),
        newParentId: row.getResultByName("newParentId"),
        oldParentId: row.getResultByName("oldParentId"),
        newParentGuid: row.getResultByName("newParentGuid"),
        oldParentGuid: row.getResultByName("oldParentGuid"),
        newPosition: row.getResultByName("newPosition"),
        oldPosition: row.getResultByName("oldPosition"),
        urlHref: row.getResultByName("url"),
      };
      this.noteItemMoved(info);
    }, yieldState);

    MirrorLog.trace("Recording observer notifications for changed items");
    let changedItemRows = await this.db.execute(`
      SELECT b.id, b.guid, b.lastModified, b.type,
             IFNULL(b.title, "") AS newTitle,
             IFNULL(c.oldTitle, "") AS oldTitle,
             h.url AS newURL, i.url AS oldURL,
             p.id AS parentId, p.guid AS parentGuid
      FROM itemsChanged c
      JOIN moz_bookmarks b ON b.id = c.itemId
      JOIN moz_bookmarks p ON p.id = b.parent
      LEFT JOIN moz_places h ON h.id = b.fk
      LEFT JOIN moz_places i ON i.id = c.oldPlaceId
      ORDER BY c.level, p.id, b.position`);
    await Async.yieldingForEach(changedItemRows, row => {
      let info = {
        id: row.getResultByName("id"),
        guid: row.getResultByName("guid"),
        lastModified: row.getResultByName("lastModified"),
        type: row.getResultByName("type"),
        newTitle: row.getResultByName("newTitle"),
        oldTitle: row.getResultByName("oldTitle"),
        newURLHref: row.getResultByName("newURL"),
        oldURLHref: row.getResultByName("oldURL"),
        parentId: row.getResultByName("parentId"),
        parentGuid: row.getResultByName("parentGuid"),
      };
      this.noteItemChanged(info);
    }, yieldState);

    MirrorLog.trace("Recording notifications for changed keywords");
    let keywordsChangedRows = await this.db.execute(`
      SELECT EXISTS(SELECT 1 FROM itemsAdded WHERE keywordChanged) OR
             EXISTS(SELECT 1 FROM itemsChanged WHERE keywordChanged)
             AS keywordsChanged`);
    this.shouldInvalidateKeywords =
      !!keywordsChangedRows[0].getResultByName("keywordsChanged");
  }

  noteItemAdded(info) {
    this.bookmarkObserverNotifications.push(new PlacesBookmarkAddition({
      id: info.id,
      parentId: info.parentId,
      index: info.position,
      url: info.urlHref || "",
      title: info.title,
      dateAdded: info.dateAdded,
      guid: info.guid,
      parentGuid: info.parentGuid,
      source: PlacesUtils.bookmarks.SOURCES.SYNC,
      itemType: info.type,
      isTagging: info.isTagging,
    }));
  }

  noteGuidChanged(info) {
    PlacesUtils.invalidateCachedGuidFor(info.id);
    this.bookmarkObserverNotifications.push({
      name: "onItemChanged",
      isTagging: false,
      args: [info.id, "guid", /* isAnnotationProperty */ false, info.newGuid,
        info.lastModified, info.type, info.parentId, info.newGuid,
        info.parentGuid, info.oldGuid, PlacesUtils.bookmarks.SOURCES.SYNC],
    });
  }

  noteItemMoved(info) {
    this.bookmarkObserverNotifications.push({
      name: "onItemMoved",
      isTagging: false,
      args: [info.id, info.oldParentId, info.oldPosition, info.newParentId,
        info.newPosition, info.type, info.guid, info.oldParentGuid,
        info.newParentGuid, PlacesUtils.bookmarks.SOURCES.SYNC, info.urlHref],
    });
  }

  noteItemChanged(info) {
    if (info.oldTitle != info.newTitle) {
      this.bookmarkObserverNotifications.push({
        name: "onItemChanged",
        isTagging: false,
        args: [info.id, "title", /* isAnnotationProperty */ false,
          info.newTitle, info.lastModified, info.type, info.parentId,
          info.guid, info.parentGuid, info.oldTitle,
          PlacesUtils.bookmarks.SOURCES.SYNC],
      });
    }
    if (info.oldURLHref != info.newURLHref) {
      this.bookmarkObserverNotifications.push({
        name: "onItemChanged",
        isTagging: false,
        args: [info.id, "uri", /* isAnnotationProperty */ false,
          info.newURLHref, info.lastModified, info.type, info.parentId,
          info.guid, info.parentGuid, info.oldURLHref,
          PlacesUtils.bookmarks.SOURCES.SYNC],
      });
    }
  }

  noteItemRemoved(info) {
    let uri = info.urlHref ? Services.io.newURI(info.urlHref) : null;
    this.bookmarkObserverNotifications.push({
      name: "onItemRemoved",
      isTagging: info.isUntagging,
      args: [info.id, info.parentId, info.position, info.type, uri, info.guid,
        info.parentGuid, PlacesUtils.bookmarks.SOURCES.SYNC],
    });
  }

  async notifyBookmarkObservers() {
    MirrorLog.trace("Notifying bookmark observers");
    let observers = PlacesUtils.bookmarks.getObservers();
    for (let observer of observers) {
      this.notifyObserver(observer, "onBeginUpdateBatch");
    }
    await Async.yieldingForEach(this.bookmarkObserverNotifications, info => {
      if (info instanceof PlacesEvent) {
        PlacesObservers.notifyListeners([info]);
      } else {
        for (let observer of observers) {
          if (info.isTagging && observer.skipTags) {
            return;
          }
          this.notifyObserver(observer, info.name, info.args);
        }
      }
    }, yieldState);
    for (let observer of observers) {
      this.notifyObserver(observer, "onEndUpdateBatch");
    }
  }

  notifyObserver(observer, notification, args = []) {
    try {
      observer[notification](...args);
    } catch (ex) {
      MirrorLog.warn("Error notifying observer", ex);
    }
  }
}

/**
 * Holds Sync metadata and the cleartext for a locally changed record. The
 * bookmarks engine inflates a Sync record from the cleartext, and updates the
 * `synced` property for successfully uploaded items.
 *
 * At the end of the sync, the engine writes the uploaded cleartext back to the
 * mirror, and passes the updated change record as part of the changeset to
 * `PlacesSyncUtils.bookmarks.pushChanges`.
 */
class BookmarkChangeRecord {
  constructor(syncChangeCounter, cleartext) {
    this.tombstone = cleartext.deleted === true;
    this.counter = syncChangeCounter;
    this.cleartext = cleartext;
    this.synced = false;
  }
}

async function updateFrecencies(db, limit) {
  MirrorLog.trace("Recalculating frecencies for new URLs");
  await db.execute(`
    UPDATE moz_places SET
      frecency = CALCULATE_FRECENCY(id)
    WHERE id IN (
      SELECT id FROM moz_places
      WHERE frecency < 0
      ORDER BY frecency ASC
      LIMIT :limit
    )`,
    { limit });

  // Trigger frecency updates for all affected origins.
  await db.execute(`DELETE FROM moz_updateoriginsupdate_temp`);
}

// In conclusion, this is why bookmark syncing is hard.
