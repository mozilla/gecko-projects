/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function test_missing_children() {
  let buf = await openMirror("missing_childen");

  info("Set up empty mirror");
  await PlacesTestUtils.markBookmarksAsSynced();

  info("Make remote changes: A > ([B] C [D E])");
  {
    await buf.store(shuffle([{
      id: "menu",
      type: "folder",
      children: ["bookmarkBBBB", "bookmarkCCCC", "bookmarkDDDD",
                 "bookmarkEEEE"],
    }, {
      id: "bookmarkCCCC",
      type: "bookmark",
      bmkUri: "http://example.com/c",
      title: "C",
    }]));
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");

    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload menu with missing children (B D E)");
    await assertLocalTree(PlacesUtils.bookmarks.menuGuid, {
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
      children: [{
        guid: "bookmarkCCCC",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 0,
        title: "C",
        url: "http://example.com/c",
      }],
    }, "Menu children should be (C)");
    let { missingChildren } = await buf.fetchRemoteOrphans();
    deepEqual(missingChildren.sort(), ["bookmarkBBBB", "bookmarkDDDD",
      "bookmarkEEEE"], "Should report (B D E) as missing");
  }

  info("Add (B E) to remote");
  {
    await buf.store(shuffle([{
      id: "bookmarkBBBB",
      type: "bookmark",
      title: "B",
      bmkUri: "http://example.com/b",
    }, {
      id: "bookmarkEEEE",
      type: "bookmark",
      title: "E",
      bmkUri: "http://example.com/e",
    }]));
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");

    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload menu with missing child D");
    await assertLocalTree(PlacesUtils.bookmarks.menuGuid, {
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
      children: [{
        guid: "bookmarkBBBB",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 0,
        title: "B",
        url: "http://example.com/b",
      }, {
        guid: "bookmarkCCCC",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 1,
        title: "C",
        url: "http://example.com/c",
      }, {
        guid: "bookmarkEEEE",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 2,
        title: "E",
        url: "http://example.com/e",
      }],
    }, "Menu children should be (B C E)");
    let { missingChildren } = await buf.fetchRemoteOrphans();
    deepEqual(missingChildren, ["bookmarkDDDD"],
      "Should report (D) as missing");
  }

  info("Add D to remote");
  {
    await buf.store([{
      id: "bookmarkDDDD",
      type: "bookmark",
      title: "D",
      bmkUri: "http://example.com/d",
    }]);
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");

    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload complete menu");
    await assertLocalTree(PlacesUtils.bookmarks.menuGuid, {
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
      children: [{
        guid: "bookmarkBBBB",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 0,
        title: "B",
        url: "http://example.com/b",
      }, {
        guid: "bookmarkCCCC",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 1,
        title: "C",
        url: "http://example.com/c",
      }, {
        guid: "bookmarkDDDD",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 2,
        title: "D",
        url: "http://example.com/d",
      }, {
        guid: "bookmarkEEEE",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 3,
        title: "E",
        url: "http://example.com/e",
      }],
    }, "Menu children should be (B C D E)");
    let { missingChildren } = await buf.fetchRemoteOrphans();
    deepEqual(missingChildren, [], "Should not report any missing children");
  }

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});

add_task(async function test_new_orphan_without_local_parent() {
  let buf = await openMirror("new_orphan_without_local_parent");

  info("Set up empty mirror");
  await PlacesTestUtils.markBookmarksAsSynced();

  // A doesn't exist locally, so we move the bookmarks into "unfiled" without
  // reuploading. When the partial uploader returns and uploads A, we'll
  // move the bookmarks to the correct folder.
  info("Make remote changes: [A] > (B C D)");
  await buf.store(shuffle([{
    id: "bookmarkBBBB",
    type: "bookmark",
    title: "B (remote)",
    bmkUri: "http://example.com/b-remote",
  }, {
    id: "bookmarkCCCC",
    type: "bookmark",
    title: "C (remote)",
    bmkUri: "http://example.com/c-remote",
  }, {
    id: "bookmarkDDDD",
    type: "bookmark",
    title: "D (remote)",
    bmkUri: "http://example.com/d-remote",
  }]));

  info("Apply remote with (B C D)");
  {
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");
    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload orphans (B C D)");
  }

  await assertLocalTree(PlacesUtils.bookmarks.unfiledGuid, {
    guid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 3,
    title: UnfiledBookmarksTitle,
    children: [{
      guid: "bookmarkBBBB",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 0,
      title: "B (remote)",
      url: "http://example.com/b-remote",
    }, {
      guid: "bookmarkCCCC",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 1,
      title: "C (remote)",
      url: "http://example.com/c-remote",
    }, {
      guid: "bookmarkDDDD",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 2,
      title: "D (remote)",
      url: "http://example.com/d-remote",
    }],
  }, "Should move (B C D) to unfiled");

  // A is an orphan because we don't have E locally, but we should move
  // (B C D) into A.
  info("Add [E] > A to remote");
  await buf.store([{
    id: "folderAAAAAA",
    type: "folder",
    title: "A",
    children: ["bookmarkDDDD", "bookmarkCCCC", "bookmarkBBBB"],
  }]);

  info("Apply remote with A");
  {
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");
    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload orphan A");
  }

  await assertLocalTree(PlacesUtils.bookmarks.unfiledGuid, {
    guid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 3,
    title: UnfiledBookmarksTitle,
    children: [{
      guid: "folderAAAAAA",
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: "A",
      children: [{
        guid: "bookmarkDDDD",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 0,
        title: "D (remote)",
        url: "http://example.com/d-remote",
      }, {
        guid: "bookmarkCCCC",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 1,
        title: "C (remote)",
        url: "http://example.com/c-remote",
      }, {
        guid: "bookmarkBBBB",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 2,
        title: "B (remote)",
        url: "http://example.com/b-remote",
      }],
    }],
  }, "Should move (D C B) into A");

  info("Add E to remote");
  await buf.store([{
    id: "folderEEEEEE",
    type: "folder",
    title: "E",
    children: ["folderAAAAAA"],
  }]);

  info("Apply remote with E");
  {
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");
    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload orphan E");
  }

  // E is still in unfiled because we don't have a record for the menu.
  await assertLocalTree(PlacesUtils.bookmarks.unfiledGuid, {
    guid: PlacesUtils.bookmarks.unfiledGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 3,
    title: UnfiledBookmarksTitle,
    children: [{
      guid: "folderEEEEEE",
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: "E",
      children: [{
        guid: "folderAAAAAA",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        index: 0,
        title: "A",
        children: [{
          guid: "bookmarkDDDD",
          type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
          index: 0,
          title: "D (remote)",
          url: "http://example.com/d-remote",
        }, {
          guid: "bookmarkCCCC",
          type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
          index: 1,
          title: "C (remote)",
          url: "http://example.com/c-remote",
        }, {
          guid: "bookmarkBBBB",
          type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
          index: 2,
          title: "B (remote)",
          url: "http://example.com/b-remote",
        }],
      }],
    }],
  }, "Should move A into E");

  info("Add Menu > E to remote");
  await buf.store([{
    id: "menu",
    type: "folder",
    children: ["folderEEEEEE"],
  }]);

  info("Apply remote with menu");
  {
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");
    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload after forming complete tree");
  }

  await assertLocalTree(PlacesUtils.bookmarks.rootGuid, {
    guid: PlacesUtils.bookmarks.rootGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 0,
    title: "",
    children: [{
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
      children: [{
        guid: "folderEEEEEE",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        index: 0,
        title: "E",
        children: [{
          guid: "folderAAAAAA",
          type: PlacesUtils.bookmarks.TYPE_FOLDER,
          index: 0,
          title: "A",
          children: [{
            guid: "bookmarkDDDD",
            type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
            index: 0,
            title: "D (remote)",
            url: "http://example.com/d-remote",
          }, {
            guid: "bookmarkCCCC",
            type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
            index: 1,
            title: "C (remote)",
            url: "http://example.com/c-remote",
          }, {
            guid: "bookmarkBBBB",
            type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
            index: 2,
            title: "B (remote)",
            url: "http://example.com/b-remote",
          }],
        }],
      }],
    }, {
      guid: PlacesUtils.bookmarks.toolbarGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 1,
      title: BookmarksToolbarTitle,
    }, {
      guid: PlacesUtils.bookmarks.unfiledGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 3,
      title: UnfiledBookmarksTitle,
    }, {
      guid: PlacesUtils.bookmarks.mobileGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 4,
      title: MobileBookmarksTitle,
    }],
  }, "Should form complete tree after applying E");

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});

add_task(async function test_move_into_orphaned() {
  let buf = await openMirror("move_into_orphaned");

  info("Set up mirror: Menu > (A B (C > (D (E > F))))");
  await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [{
      guid: "bookmarkAAAA",
      url: "http://example.com/a",
      title: "A",
    }, {
      guid: "bookmarkBBBB",
      url: "http://example.com/b",
      title: "B",
    }, {
      guid: "folderCCCCCC",
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      title: "C",
      children: [{
        guid: "bookmarkDDDD",
        title: "D",
        url: "http://example.com/d",
      }, {
        guid: "folderEEEEEE",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "E",
        children: [{
          guid: "bookmarkFFFF",
          title: "F",
          url: "http://example.com/f",
        }],
      }],
    }],
  });
  await buf.store([{
    id: "menu",
    type: "folder",
    children: ["bookmarkAAAA", "bookmarkBBBB", "folderCCCCCC"],
  }, {
    id: "bookmarkAAAA",
    type: "bookmark",
    title: "A",
    bmkUri: "http://example.com/a",
  }, {
    id: "bookmarkBBBB",
    type: "bookmark",
    title: "B",
    bmkUri: "http://example.com/b",
  }, {
    id: "folderCCCCCC",
    type: "folder",
    title: "C",
    children: ["bookmarkDDDD", "folderEEEEEE"],
  }, {
    id: "bookmarkDDDD",
    type: "bookmark",
    title: "D",
    bmkUri: "http://example.com/d",
  }, {
    id: "folderEEEEEE",
    type: "folder",
    title: "E",
    children: ["bookmarkFFFF"],
  }, {
    id: "bookmarkFFFF",
    type: "bookmark",
    title: "F",
    bmkUri: "http://example.com/f",
  }], { needsMerge: false });
  await PlacesTestUtils.markBookmarksAsSynced();

  info("Make local changes: delete D, add E > I");
  await PlacesUtils.bookmarks.remove("bookmarkDDDD");
  await PlacesUtils.bookmarks.insert({
    guid: "bookmarkIIII",
    parentGuid: "folderEEEEEE",
    title: "I (local)",
    url: "http://example.com/i",
  });

  // G doesn't exist on the server.
  info("Make remote changes: ([G] > A (C > (D H E))), (C > H)");
  await buf.store(shuffle([{
    id: "bookmarkAAAA",
    type: "bookmark",
    title: "A",
    bmkUri: "http://example.com/a",
  }, {
    id: "folderCCCCCC",
    type: "folder",
    title: "C",
    children: ["bookmarkDDDD", "bookmarkHHHH", "folderEEEEEE"],
  }, {
    id: "bookmarkHHHH",
    type: "bookmark",
    title: "H (remote)",
    bmkUri: "http://example.com/h-remote",
  }]));

  info("Apply remote");
  let changesToUpload = await buf.apply();
  deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");

  let idsToUpload = inspectChangeRecords(changesToUpload);
  deepEqual(idsToUpload, {
    updated: ["bookmarkIIII", "folderCCCCCC", "folderEEEEEE"],
    deleted: ["bookmarkDDDD"],
  }, "Should upload records for (I C E); tombstone for D");

  await assertLocalTree(PlacesUtils.bookmarks.rootGuid, {
    guid: PlacesUtils.bookmarks.rootGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 0,
    title: "",
    children: [{
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
      children: [{
        // A remains in its original place, since we don't use the `parentid`,
        // and we don't have a record for G.
        guid: "bookmarkAAAA",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 0,
        title: "A",
        url: "http://example.com/a",
      }, {
        guid: "bookmarkBBBB",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 1,
        title: "B",
        url: "http://example.com/b",
      }, {
        // C exists on the server, so we take its children and order. D was
        // deleted locally, and doesn't exist remotely. C is also a child of
        // G, but we don't have a record for it on the server.
        guid: "folderCCCCCC",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        index: 2,
        title: "C",
        children: [{
          guid: "bookmarkHHHH",
          type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
          index: 0,
          title: "H (remote)",
          url: "http://example.com/h-remote",
        }, {
          guid: "folderEEEEEE",
          type: PlacesUtils.bookmarks.TYPE_FOLDER,
          index: 1,
          title: "E",
          children: [{
            guid: "bookmarkFFFF",
            type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
            index: 0,
            title: "F",
            url: "http://example.com/f",
          }, {
            guid: "bookmarkIIII",
            type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
            index: 1,
            title: "I (local)",
            url: "http://example.com/i",
          }],
        }],
      }],
    }, {
      guid: PlacesUtils.bookmarks.toolbarGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 1,
      title: BookmarksToolbarTitle,
    }, {
      guid: PlacesUtils.bookmarks.unfiledGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 3,
      title: UnfiledBookmarksTitle,
    }, {
      guid: PlacesUtils.bookmarks.mobileGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 4,
      title: MobileBookmarksTitle,
    }],
  }, "Should treat local tree as canonical if server is missing new parent");

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});

add_task(async function test_new_orphan_with_local_parent() {
  let buf = await openMirror("new_orphan_with_local_parent");

  info("Set up mirror: A > (B D)");
  await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [{
      guid: "folderAAAAAA",
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      title: "A",
      children: [{
        guid: "bookmarkBBBB",
        url: "http://example.com/b",
        title: "B",
      }, {
        guid: "bookmarkEEEE",
        url: "http://example.com/e",
        title: "E",
      }],
    }],
  });
  await buf.store(shuffle([{
    id: "menu",
    type: "folder",
    children: ["folderAAAAAA"],
  }, {
    id: "folderAAAAAA",
    type: "folder",
    title: "A",
    children: ["bookmarkBBBB", "bookmarkEEEE"],
  }, {
    id: "bookmarkBBBB",
    type: "bookmark",
    title: "B",
    bmkUri: "http://example.com/b",
  }, {
    id: "bookmarkEEEE",
    type: "bookmark",
    title: "E",
    bmkUri: "http://example.com/e",
  }]), { needsMerge: false });
  await PlacesTestUtils.markBookmarksAsSynced();

  // Simulate a partial write by another device that uploaded only B and C. A
  // exists locally, so we can move B and C into the correct folder, but not
  // the correct positions.
  info("Set up remote with orphans: [A] > (C D)");
  await buf.store([{
    id: "bookmarkDDDD",
    type: "bookmark",
    title: "D (remote)",
    bmkUri: "http://example.com/d-remote",
  }, {
    id: "bookmarkCCCC",
    type: "bookmark",
    title: "C (remote)",
    bmkUri: "http://example.com/c-remote",
  }]);

  info("Apply remote with (C D)");
  {
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");
    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload orphans (C D)");
  }

  await assertLocalTree(PlacesUtils.bookmarks.rootGuid, {
    guid: PlacesUtils.bookmarks.rootGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 0,
    title: "",
    children: [{
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
      children: [{
        guid: "folderAAAAAA",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        index: 0,
        title: "A",
        children: [{
          guid: "bookmarkBBBB",
          type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
          index: 0,
          title: "B",
          url: "http://example.com/b",
        }, {
          guid: "bookmarkEEEE",
          type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
          index: 1,
          title: "E",
          url: "http://example.com/e",
        }],
      }],
    }, {
      guid: PlacesUtils.bookmarks.toolbarGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 1,
      title: BookmarksToolbarTitle,
    }, {
      guid: PlacesUtils.bookmarks.unfiledGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 3,
      title: UnfiledBookmarksTitle,
      children: [{
        guid: "bookmarkCCCC",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 0,
        title: "C (remote)",
        url: "http://example.com/c-remote",
      }, {
        guid: "bookmarkDDDD",
        type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
        index: 1,
        title: "D (remote)",
        url: "http://example.com/d-remote",
      }],
    }, {
      guid: PlacesUtils.bookmarks.mobileGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 4,
      title: MobileBookmarksTitle,
    }],
  }, "Should move (C D) to unfiled");

  // The partial uploader returns and uploads A.
  info("Add A to remote");
  await buf.store([{
    id: "folderAAAAAA",
    type: "folder",
    title: "A",
    children: ["bookmarkCCCC", "bookmarkDDDD", "bookmarkEEEE", "bookmarkBBBB"],
  }]);

  info("Apply remote with A");
  {
    let changesToUpload = await buf.apply();
    deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");
    let idsToUpload = inspectChangeRecords(changesToUpload);
    deepEqual(idsToUpload, {
      updated: [],
      deleted: [],
    }, "Should not reupload orphan A");
  }

  await assertLocalTree("folderAAAAAA", {
    guid: "folderAAAAAA",
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 0,
    title: "A",
    children: [{
      guid: "bookmarkCCCC",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 0,
      title: "C (remote)",
      url: "http://example.com/c-remote",
    }, {
      guid: "bookmarkDDDD",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 1,
      title: "D (remote)",
      url: "http://example.com/d-remote",
    }, {
      guid: "bookmarkEEEE",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 2,
      title: "E",
      url: "http://example.com/e",
    }, {
      guid: "bookmarkBBBB",
      type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
      index: 3,
      title: "B",
      url: "http://example.com/b",
    }],
  }, "Should update child positions once A exists in mirror");

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});

add_task(async function test_tombstone_as_child() {
  // TODO (Bug 1433180): Add a folder that mentions a tombstone in its
  // `children`.
});

add_task(async function test_left_pane_root() {
  // TODO (Bug 1433182): Add a left pane root to the mirror.
});

add_task(async function test_partial_cycle() {
  let buf = await openMirror("partial_cycle");

  info("Set up mirror: Menu > A > B > C");
  await PlacesUtils.bookmarks.insertTree({
    guid: PlacesUtils.bookmarks.menuGuid,
    children: [{
      guid: "folderAAAAAA",
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      title: "A",
      children: [{
        guid: "folderBBBBBB",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        title: "B",
        children: [{
          guid: "bookmarkCCCC",
          url: "http://example.com/c",
          title: "C",
        }],
      }],
    }],
  });
  await buf.store(shuffle([{
    id: "menu",
    type: "folder",
    children: ["folderAAAAAA"],
  }, {
    id: "folderAAAAAA",
    type: "folder",
    title: "A",
    children: ["folderBBBBBB"],
  }, {
    id: "folderBBBBBB",
    type: "folder",
    title: "B",
    children: ["bookmarkCCCC"],
  }, {
    id: "bookmarkCCCC",
    type: "bookmark",
    title: "C",
    bmkUri: "http://example.com/c",
  }]), { needsMerge: false });
  await PlacesTestUtils.markBookmarksAsSynced();

  // Try to create a cycle: move A into B, and B into the menu, but don't upload
  // a record for the menu. B is still a child of A locally. Since we ignore the
  // `parentid`, we'll move (B A) into unfiled.
  info("Make remote changes: A > C");
  await buf.store([{
    id: "folderAAAAAA",
    type: "folder",
    title: "A (remote)",
    children: ["bookmarkCCCC"],
  }, {
    id: "folderBBBBBB",
    type: "folder",
    title: "B (remote)",
    children: ["folderAAAAAA"],
  }]);

  info("Apply remote");
  let changesToUpload = await buf.apply();
  deepEqual(await buf.fetchUnmergedGuids(), [], "Should merge all items");

  let idsToUpload = inspectChangeRecords(changesToUpload);
  deepEqual(idsToUpload, { updated: [], deleted: [] },
    "Should not mark any local items for upload");

  await assertLocalTree(PlacesUtils.bookmarks.rootGuid, {
    guid: PlacesUtils.bookmarks.rootGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 0,
    title: "",
    children: [{
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
    }, {
      guid: PlacesUtils.bookmarks.toolbarGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 1,
      title: BookmarksToolbarTitle,
    }, {
      guid: PlacesUtils.bookmarks.unfiledGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 3,
      title: UnfiledBookmarksTitle,
      children: [{
        guid: "folderBBBBBB",
        type: PlacesUtils.bookmarks.TYPE_FOLDER,
        index: 0,
        title: "B (remote)",
        children: [{
          guid: "folderAAAAAA",
          type: PlacesUtils.bookmarks.TYPE_FOLDER,
          index: 0,
          title: "A (remote)",
          children: [{
            guid: "bookmarkCCCC",
            type: PlacesUtils.bookmarks.TYPE_BOOKMARK,
            index: 0,
            title: "C",
            url: "http://example.com/c",
          }],
        }],
      }],
    }, {
      guid: PlacesUtils.bookmarks.mobileGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 4,
      title: MobileBookmarksTitle,
    }],
  }, "Should move A and B to unfiled");

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});

add_task(async function test_complete_cycle() {
  let buf = await openMirror("complete_cycle");

  info("Set up empty mirror");
  await PlacesTestUtils.markBookmarksAsSynced();

  // This test is order-dependent. We shouldn't recurse infinitely, but,
  // depending on the order of the records, we might ignore the circular
  // subtree because there's nothing linking it back to the rest of the
  // tree.
  info("Make remote changes: Menu > A > B > C > A");
  await buf.store([{
    id: "menu",
    type: "folder",
    children: ["folderAAAAAA"],
  }, {
    id: "folderAAAAAA",
    type: "folder",
    title: "A",
    children: ["folderBBBBBB"],
  }, {
    id: "folderBBBBBB",
    type: "folder",
    title: "B",
    children: ["folderCCCCCC"],
  }, {
    id: "folderCCCCCC",
    type: "folder",
    title: "C",
    children: ["folderDDDDDD"],
  }, {
    id: "folderDDDDDD",
    type: "folder",
    title: "D",
    children: ["folderAAAAAA"],
  }]);

  info("Apply remote");
  let changesToUpload = await buf.apply();
  deepEqual((await buf.fetchUnmergedGuids()).sort(), ["folderAAAAAA",
    "folderBBBBBB", "folderCCCCCC", "folderDDDDDD"],
    "Should leave items in circular subtree unmerged");

  let idsToUpload = inspectChangeRecords(changesToUpload);
  deepEqual(idsToUpload, { updated: [], deleted: [] },
    "Should not mark any local items for upload");

  await assertLocalTree(PlacesUtils.bookmarks.rootGuid, {
    guid: PlacesUtils.bookmarks.rootGuid,
    type: PlacesUtils.bookmarks.TYPE_FOLDER,
    index: 0,
    title: "",
    children: [{
      guid: PlacesUtils.bookmarks.menuGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 0,
      title: BookmarksMenuTitle,
    }, {
      guid: PlacesUtils.bookmarks.toolbarGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 1,
      title: BookmarksToolbarTitle,
    }, {
      guid: PlacesUtils.bookmarks.unfiledGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 3,
      title: UnfiledBookmarksTitle,
    }, {
      guid: PlacesUtils.bookmarks.mobileGuid,
      type: PlacesUtils.bookmarks.TYPE_FOLDER,
      index: 4,
      title: MobileBookmarksTitle,
    }],
  }, "Should not be confused into creating a cycle");

  await buf.finalize();
  await PlacesUtils.bookmarks.eraseEverything();
  await PlacesSyncUtils.bookmarks.reset();
});
