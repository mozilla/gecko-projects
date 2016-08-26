"use strict";

Components.utils.import("resource://gre/modules/Services.jsm");
Components.utils.import("resource://gre/modules/PlacesUtils.jsm");
Components.utils.import("resource://gre/modules/osfile.jsm");
Components.utils.import("resource:///modules/TabGroupsMigrator.jsm");

var gProfD = do_get_profile();

const TEST_STATES = {
  TWO_GROUPS: {
    selectedWindow: 1,
    windows: [
      {
        tabs: [
          {
            entries: [{
              url: "about:robots",
              title: "Robots 1",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{\"groupID\":2,\"active\":true}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 2",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{\"groupID\":2}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 3",
            }],
            index: 1,
            hidden: true,
            extData: {
              "tabview-tab": "{\"groupID\":13}",
            },
          }
        ],
        extData: {
          "tabview-group": "{\"2\":{},\"13\":{\"title\":\"Foopy\"}}",
          "tabview-groups": "{\"nextID\":20,\"activeGroupId\":2,\"totalNumber\":2}",
          "tabview-visibility": "false"
        },
      },
    ]
  },
  NAMED_ACTIVE_GROUP: {
    selectedWindow: 1,
    windows: [
      {
        tabs: [
          {
            entries: [{
              url: "about:mozilla",
              title: "Mozilla 1",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{\"groupID\":2,\"active\":true}",
            },
          },
        ],
        extData: {
          "tabview-group": "{\"2\":{\"title\":\"Foopy\"}}",
          "tabview-groups": "{\"nextID\":20,\"activeGroupId\":2,\"totalNumber\":1}",
          "tabview-visibility": "false"
        },
      }
    ],
  },
  TAB_WITHOUT_GROUP: {
    selectedWindow: 1,
    windows: [
      {
        tabs: [
          {
            entries: [{
              url: "about:robots",
              title: "Robots 1",
            }],
            index: 1,
            hidden: false,
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 2",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{\"groupID\":2}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 3",
            }],
            index: 1,
            hidden: true,
            extData: {
              "tabview-tab": "{\"groupID\":1}",
            },
          }
        ],
        extData: {
          "tabview-group": "{\"2\":{}, \"1\": {}}",
          "tabview-groups": "{\"nextID\":20,\"activeGroupId\":2,\"totalNumber\":2}",
          "tabview-visibility": "false"
        },
      }
    ],
  },
  ONLY_UNGROUPED_TABS: {
    selectedWindow: 1,
    windows: [
      {
        tabs: [
          {
            entries: [{
              url: "about:robots",
              title: "Robots 1",
            }],
            index: 1,
            hidden: false,
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 2",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 3",
            }],
            index: 1,
            hidden: true,
            extData: {
            },
          }
        ],
        extData: {
          "tabview-group": "{\"2\":{}}",
        },
      }
    ],
  },
  SORTING_NAMING_RESTORE_PAGE: {
    windows: [
      {
        tabs: [
          {
            entries: [{
              url: "about:robots",
              title: "Robots 1",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{\"groupID\":2,\"active\":true}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 2",
            }],
            index: 1,
            hidden: false,
            extData: {
              "tabview-tab": "{\"groupID\":2}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 3",
            }],
            index: 1,
            hidden: true,
            extData: {
              "tabview-tab": "{\"groupID\":13}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 4",
            }],
            index: 1,
            hidden: true,
            extData: {
              "tabview-tab": "{\"groupID\":15}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 5",
            }],
            index: 1,
            hidden: true,
            extData: {
              "tabview-tab": "{\"groupID\":16}",
            },
          },
          {
            entries: [{
              url: "about:robots",
              title: "Robots 6",
            }],
            index: 1,
            hidden: true,
            extData: {
              "tabview-tab": "{\"groupID\":17}",
            },
          }
        ],
        extData: {
          "tabview-group": "{\"2\":{},\"13\":{\"title\":\"Foopy\"}, \"15\":{\"title\":\"Barry\"}, \"16\":{}, \"17\":{}}",
          "tabview-groups": "{\"nextID\":20,\"activeGroupId\":2,\"totalNumber\":5}",
          "tabview-visibility": "false"
        },
      },
    ]
  },
};

add_task(function* gatherGroupDataTest() {
  let groupInfo = TabGroupsMigrator._gatherGroupData(TEST_STATES.TWO_GROUPS);
  Assert.equal(groupInfo.size, 1, "Information about 1 window");
  let singleWinGroups = [... groupInfo.values()][0];
  Assert.equal(singleWinGroups.size, 2, "2 groups");
  let group2 = singleWinGroups.get("2");
  Assert.ok(!!group2, "group 2 should exist");
  Assert.equal(group2.tabs.length, 2, "2 tabs in group 2");
  // Note that this has groupID 2 in the internal representation of tab groups,
  // but because it was the first group we encountered when migrating, it was
  // labeled "group 1" for the user
  Assert.equal(group2.tabGroupsMigrationTitle, "Group 1", "We assign a numeric title to untitled groups");
  Assert.equal(group2.anonGroupID, "1", "We mark an untitled group with an anonymous id");
  let group13 = singleWinGroups.get("13");
  Assert.ok(!!group13, "group 13 should exist");
  Assert.equal(group13.tabs.length, 1, "1 tabs in group 13");
  Assert.equal(group13.tabGroupsMigrationTitle, "Foopy", "Group with title has correct title");
  Assert.ok(!("anonGroupID" in group13), "We don't mark a titled group with an anonymous id");
});

add_task(function* bookmarkingTest() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.TWO_GROUPS));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  let removedGroups = TabGroupsMigrator._removeHiddenTabGroupsFromState(stateClone, groupInfo);
  yield TabGroupsMigrator._bookmarkAllGroupsFromState(groupInfo);
  let bmCounter = 0;
  let bmParents = {};
  let bookmarks = [];
  let onResult = bm => {
    bmCounter++;
    bmParents[bm.parentGuid] = (bmParents[bm.parentGuid] || 0) + 1;
    Assert.ok(bm.title.startsWith("Robots "), "Bookmark title(" + bm.title + ")  should start with 'Robots '");
  };
  yield PlacesUtils.bookmarks.fetch({url: "about:robots"}, onResult);
  Assert.equal(bmCounter, 3, "Should have seen 3 bookmarks");
  Assert.equal(Object.keys(bmParents).length, 2, "Should be in 2 folders");

  let ancestorGuid;
  let parents = Object.keys(bmParents).map(guid => {
    PlacesUtils.bookmarks.fetch({guid}, bm => {
      ancestorGuid = bm.parentGuid;
      if (bmParents[bm.guid] == 1) {
        Assert.equal(bm.title, "Foopy", "Group with 1 kid has right title");
      } else {
        Assert.ok(bm.title.includes("1"), "Group with more kids should have anon ID in title (" + bm.title + ")");
      }
    });
  });
  yield Promise.all(parents);

  yield PlacesUtils.bookmarks.fetch({guid: ancestorGuid}, bm => {
    Assert.equal(bm.title,
      gBrowserBundle.GetStringFromName("tabgroups.migration.tabGroupBookmarkFolderName"),
      "Should have the right title");
  });
});

add_task(function* bookmarkNamedActiveGroup() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.NAMED_ACTIVE_GROUP));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  let removedGroups = TabGroupsMigrator._removeHiddenTabGroupsFromState(stateClone, groupInfo);
  yield TabGroupsMigrator._bookmarkAllGroupsFromState(groupInfo);
  let bmParents = {};
  let bmCounter = 0;
  let onResult = bm => {
    bmCounter++;
    bmParents[bm.parentGuid] = (bmParents[bm.parentGuid] || 0) + 1;
    Assert.ok(bm.title.startsWith("Mozilla "), "Bookmark title (" + bm.title + ")  should start with 'Mozilla '");
  };
  yield PlacesUtils.bookmarks.fetch({url: "about:mozilla"}, onResult);
  Assert.equal(bmCounter, 1, "Should have seen 1 bookmarks");
  let parentPromise = PlacesUtils.bookmarks.fetch({guid: Object.keys(bmParents)[0]}, bm => {
    Assert.equal(bm.title, "Foopy", "Group with 1 kid has right title");
  });
  yield parentPromise;
});

add_task(function* removingTabGroupsFromJSONTest() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.TWO_GROUPS));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  let removedGroups = TabGroupsMigrator._removeHiddenTabGroupsFromState(stateClone, groupInfo);
  Assert.equal(removedGroups.windows.length, 1, "Removed 1 group which looks like a window in removed data");
  Assert.equal(removedGroups.windows[0].tabs.length, 1, "Removed group had 1 tab");
  Assert.ok(!stateClone.windows[0].extData, "extData removed from window");
  stateClone.windows[0].tabs.forEach(tab => {
    Assert.ok(!tab.extData, "extData removed from tab");
  });
  Assert.ok(stateClone.windows[0].tabs.length, 2, "Only 2 tabs remain in the window");
});

add_task(function* backupTest() {
  yield TabGroupsMigrator._createBackup(JSON.stringify(TEST_STATES.TWO_GROUPS));
  let f = Services.dirsvc.get("ProfD", Components.interfaces.nsIFile);
  f.append("tabgroups-session-backup.json");
  ok(f.exists(), "Should have created the file");

  let txt = (new TextDecoder()).decode(yield OS.File.read(f.path));
  Assert.deepEqual(JSON.parse(txt), TEST_STATES.TWO_GROUPS, "Should have written the expected state.");

  f.remove(false);
});

add_task(function* migrationPageDataTest() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.TWO_GROUPS));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  let removedGroups = TabGroupsMigrator._removeHiddenTabGroupsFromState(stateClone, groupInfo);
  TabGroupsMigrator._createBackgroundTabGroupRestorationPage(stateClone, removedGroups);
  Assert.equal(stateClone.windows.length, 1, "Should still only have 1 window");
  Assert.equal(stateClone.windows[0].tabs.length, 3, "Should now have 3 tabs");

  let url = "chrome://browser/content/aboutTabGroupsMigration.xhtml";
  let formdata = {id: {sessionData: JSON.stringify(removedGroups)}, url};
  Assert.deepEqual(stateClone.windows[0].tabs[2],
    {
      entries: [{url}],
      formdata,
      index: 1
    },
    "Should have added expected tab at the end of the tab list.");
});

add_task(function* correctMissingTabGroupInfo() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.TAB_WITHOUT_GROUP));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  Assert.equal(groupInfo.size, 1, "Should have 1 window");
  let windowGroups = [...groupInfo][0][1];
  Assert.equal(windowGroups.size, 2, "Window should have 2 groups");
  let group2 = windowGroups.get("2");
  Assert.ok(group2, "Group 2 should exist");
  Assert.equal(group2.tabs.length, 2, "There should be 2 tabs in group 2");
  Assert.equal(group2.tabs[0].entries[0].title, "Robots 1", "The first tab of group 2 should be the tab with no group info.");
});

add_task(function* dealWithNoGroupInfo() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.ONLY_UNGROUPED_TABS));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  Assert.equal(groupInfo.size, 1, "Should have 1 window");
  let windowGroups = [...groupInfo][0][1];
  Assert.equal(windowGroups.size, 1, "Window should have 1 group");
  let fallbackActiveGroup = windowGroups.get("active group");
  Assert.ok(fallbackActiveGroup, "Fallback group should exist");
  Assert.equal(fallbackActiveGroup.tabs.length, 3, "There should be 3 tabs in the fallback group");
});

add_task(function* groupSortingInRemovedDataUsedForRestorePage() {
  let stateClone = JSON.parse(JSON.stringify(TEST_STATES.SORTING_NAMING_RESTORE_PAGE));
  let groupInfo = TabGroupsMigrator._gatherGroupData(stateClone);
  let removedGroups = TabGroupsMigrator._removeHiddenTabGroupsFromState(stateClone, groupInfo);
  Assert.equal(stateClone.windows.length, 1, "Should still only have 1 window");
  Assert.equal(stateClone.windows[0].tabs.length, 2, "Should now have 2 tabs");

  let restoredWindowTitles = removedGroups.windows.map(win => win.tabGroupsMigrationTitle);
  // Note that group 1 is the active group and as such it won't appear in the list of
  // things the user can restore:
  Assert.deepEqual(restoredWindowTitles,
    ["Barry", "Foopy", "Group 2", "Group 3"]);
});

