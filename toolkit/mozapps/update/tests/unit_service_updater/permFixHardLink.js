/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a hard link in
 * the update directory will be removed.
 */
const LINK_FILENAME = "hard_link";
const LINK_TARGET_FILENAME = "hard_link_target";

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  let link_target = getUpdateDirFile();
  let link = link_target.clone();
  link_target.append(LINK_TARGET_FILENAME);
  link.append(LINK_FILENAME);

  link_target.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  Assert.ok(link_target.exists(), "Link target should exist after creation");

  makeHardLink(link.path, link_target.path);
  Assert.ok(link.exists(), "Link should now exist");

  let success = await fixUpdateDirectoryPerms();
  Assert.ok(success, "Should have successfully fixed directory permissions");

  // Strictly speaking, both link and link_target were hard links, so it would
  // be correct to remove either or both of them.
  Assert.ok(
    !link.exists() || !link_target.exists(),
    "Link should have been removed"
  );

  waitForFilesInUse();
}
