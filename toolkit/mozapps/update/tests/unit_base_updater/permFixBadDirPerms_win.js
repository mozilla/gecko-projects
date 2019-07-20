/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test the permission-setting code. Specifically it checks that a directory
 * with bad permissions has its permissions corrected.
 */

async function run_test() {
  if (!setupTestCommon()) {
    return;
  }
  const BAD_PERM_DIR_NAME = "bad_perm_dir";
  const CONTAINED_FILENAME = "file";
  const CONTAINED_DIR_NAME = "dir";

  let dir = getUpdateDirFile();
  dir.append(BAD_PERM_DIR_NAME);
  // We can't actually set bad permissions on Windows using the second argument
  // to nsIFile::Create, because on Windows it is only used to set the readonly
  // flag.
  dir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

  let contained_file = dir.clone();
  contained_file.append(CONTAINED_FILENAME);
  contained_file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
  Assert.ok(contained_file.exists(), "Contained file should have been created");
  let contained_dir = dir.clone();
  contained_dir.append(CONTAINED_DIR_NAME);
  contained_dir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
  Assert.ok(contained_dir.exists(), "Contained dir should have been created");
  let contained_subdir_file = contained_dir.clone();
  contained_subdir_file.append(CONTAINED_FILENAME);
  contained_subdir_file.create(
    Ci.nsIFile.NORMAL_FILE_TYPE,
    FileUtils.PERMS_FILE
  );
  Assert.ok(
    contained_subdir_file.exists(),
    "Contained sub-dir file should have been created"
  );

  setBadPermissions(dir.path);
  Assert.ok(
    !permissionsOk(dir.path),
    "Bad permissions on dir should be successfully read"
  );

  await fixUpdateDirectoryPerms();

  Assert.ok(
    permissionsOk(dir.path),
    "Bad permissions on dir should have been corrected"
  );
  Assert.ok(
    permissionsOk(contained_file.path),
    "Bad permissions on contained file should have been corrected"
  );
  Assert.ok(
    permissionsOk(contained_dir.path),
    "Bad permissions on contained directory should have been corrected"
  );
  Assert.ok(
    permissionsOk(contained_subdir_file.path),
    "Bad permissions on contained sub-directory file should have been corrected"
  );

  waitForFilesInUse();
}
