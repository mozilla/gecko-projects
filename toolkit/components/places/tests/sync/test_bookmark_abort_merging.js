/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

var { AsyncShutdown } = ChromeUtils.import(
  "resource://gre/modules/AsyncShutdown.jsm"
);

add_task(async function test_abort_merging() {
  let buf = await openMirror("abort_merging");

  let promise = new Promise((resolve, reject) => {
    buf.merger.finalize();
    let callback = {
      handleSuccess() {
        reject(new Error("Shouldn't have merged after aborting"));
      },
      handleError(code, message) {
        equal(code, Cr.NS_ERROR_ABORT, "Should abort merge with result code");
        resolve();
      },
    };
    buf.merger.merge(0, 0, [], callback);
  });

  await promise;

  // Even though the merger is already finalized on the Rust side, the DB
  // connection is still open on the JS side. Finalizing `buf` closes it.
  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});

add_task(async function test_blocker_state() {
  let barrier = new AsyncShutdown.Barrier("Test");
  let buf = await SyncedBookmarksMirror.open({
    path: "blocker_state_buf.sqlite",
    finalizeAt: barrier.client,
    recordTelemetryEvent(...args) {},
    recordStepTelemetry(...args) {},
    recordValidationTelemetry(...args) {},
  });
  await storeRecords(buf, [
    {
      id: "menu",
      parentid: "places",
      type: "folder",
      children: ["bookmarkAAAA"],
    },
    {
      id: "bookmarkAAAA",
      parentid: "menu",
      type: "bookmark",
      title: "A",
      bmkUri: "http://example.com/a",
    },
  ]);

  await buf.tryApply(0, 0, { notifyAll() {} }, []);
  // Fire the shutdown blocker, but don't `await` its promise yet, so that we
  // can check its progress synchronously.
  let waitPromise = barrier.wait();
  let blocker = barrier.state.find(
    b => b.name == "SyncedBookmarksMirror: finalize"
  );
  deepEqual(
    blocker.state.steps,
    buf.progress.steps,
    "Should report merge progress in shutdown blocker state"
  );
  await waitPromise;

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});
