/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a file with
 * bad permissions has its permissions corrected.
 */

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  const BAD_PERM_FILENAME = "bad_perm_file";

  let file = getUpdateDirFile();
  file.append(BAD_PERM_FILENAME);
  // We can't actually set bad permissions on Windows using the second argument
  // to nsIFile::Create, because on Windows it is only used to set the readonly
  // flag.
  file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  setBadPermissions(file.path);
  Assert.ok(
    !permissionsOk(file.path),
    "Bad permissions should be successfully read"
  );

  await fixUpdateDirectoryPerms();

  Assert.ok(
    permissionsOk(file.path),
    "Bad permissions should have been corrected"
  );

  waitForFilesInUse();
}
