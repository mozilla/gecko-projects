/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a hard link in
 * the update directory will be removed.
 */

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  const LINK_FILENAME = "hard_link";
  const LINK_TARGET_FILENAME = "hard_link_target";

  let link_target = gCommonAppDataDir.clone();
  link_target.append(LINK_TARGET_FILENAME);
  link_target.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  Assert.ok(link_target.exists(), "Link target should exist after creation");

  registerCleanupFunction(() => {
    link_target.remove(false);
  });

  let link = getUpdateDirFile();
  link.append(LINK_FILENAME);

  makeHardLink(link.path, link_target.path);
  Assert.ok(link.exists(), "Link should now exist");

  await fixUpdateDirectoryPerms();

  Assert.ok(link_target.exists(), "Link target should still exist");
  Assert.ok(!link.exists(), "Link should have been removed");

  waitForFilesInUse();
}
