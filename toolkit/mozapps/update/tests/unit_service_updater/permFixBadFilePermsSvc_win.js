/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a file with
 * bad permissions has its permissions corrected.
 */
const BAD_PERM_FILENAME = "bad_perm_file";

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  let file = getUpdateDirFile();
  file.append(BAD_PERM_FILENAME);
  // We can't actually set bad permissions on Windows using the second argument
  // to nsIFile::Create, because on Windows it is only used to set the readonly
  // flag.
  file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  let success = setBadPermissions(file.path);
  Assert.ok(success, "Bad permissions should have been successfully set");
  success = permissionsOk(file.path);
  Assert.ok(!success, "Bad permissions should be successfully read");

  success = await fixUpdateDirectoryPerms();
  Assert.ok(success, "Should have successfully fixed directory permissions");

  success = permissionsOk(file.path);
  Assert.ok(success, "Bad permissions should have been corrected");

  waitForFilesInUse();
}
