/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a file with
 * conflicting with the needed directory structure is properly removed.
 *
 * Note: Ideally there would be some other similar tests to check for
 *       creation/fixing/re-creation of the base update directory
 *       (typically C:\ProgramData\Mozilla). However, testing these would
 *       require moving or deleting the existing update directory. When running
 *       these tests locally, that could disrupt a running Firefox's update
 *       process. Because of this, no such tests currently exist.
 */

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  let updateDir = getUpdateDirFile();
  updateDir.remove(true);
  // Create the conflicting file
  updateDir.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);

  registerCleanupFunction(() => {
    // We attempt to move a conflicting file out of the way rather than deleting
    // it, so delete the backup before exiting.
    let backup = updateDir.parent;
    backup.append(updateDir.leafName + ".bak0");
    try {
      backup.remove(false);
    } catch (e) {}
  });

  Assert.ok(updateDir.exists(), "Conflicting file should have been created");
  Assert.ok(updateDir.isFile(), "Conflicting file should not be a directory");

  let success = await fixUpdateDirectoryPerms();
  Assert.ok(success, "Should have successfully fixed directory permissions");

  Assert.ok(
    !updateDir.exists() || updateDir.isDirectory(),
    "Conflicting file should no longer conflict with update directory"
  );

  waitForFilesInUse();
}
