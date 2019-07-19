/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a directory
 * junction in the update directory will be removed.
 *
 * Note: It is currently extremely difficult to make a symbolic link in
 *       Windows without Admin privileges. But removing symbolic links and
 *       directory junctions use the exact same code paths, so this test should
 *       be sufficient.
 */
const LINK_DIR_NAME = "link";
const LINK_TARGET_DIR_NAME = "link_target";
const LINK_TARGET_SUB_FILE = "file";

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  let link_target = getUpdateDirFile();
  let link = link_target.clone();
  link_target.append(LINK_TARGET_DIR_NAME);
  link.append(LINK_DIR_NAME);

  link_target.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

  let target_sub_file = link_target.clone();
  target_sub_file.append(LINK_TARGET_SUB_FILE);
  target_sub_file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  let link_sub_file = link.clone();
  link_sub_file.append(LINK_TARGET_SUB_FILE);
  Assert.ok(target_sub_file.exists(), "Created file should exist");
  Assert.ok(!link_sub_file.exists(), "Link should not exist yet");

  makeDirJunction(link.path, link_target.path);
  Assert.ok(link.exists(), "Link should now exist");
  Assert.ok(link_sub_file.exists(), "Link should be navigable");

  let success = await fixUpdateDirectoryPerms();
  Assert.ok(success, "Should have successfully fixed directory permissions");

  Assert.ok(link_target.exists(), "Link target should still exist");
  Assert.ok(target_sub_file.exists(), "Target's contents should still exist");
  Assert.ok(!link_sub_file.exists(), "Link should no longer be navigable");
  Assert.ok(!link.exists(), "Link should have been removed");

  waitForFilesInUse();
}
