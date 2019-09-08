/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This test is mainly to verify the 2_1To2_2 upgrade does remove obsolete
// origins and the temporary storage initialization won't be blocked after
// removing them. Also, an invalid origin shouldn't block upgrades, so this
// test verifies that as well.

async function testSteps() {
  const obsoleteOriginPaths = [
    "storage/default/chrome+++content+browser.xul/",
    "storage/default/moz-safe-about+++home/",
    // Deprecated client
    "storage/default/https+++example.com/asmjs/",
    "storage/default/about+home+1",
    "storage/default/about+home+1+q",
    // about:reader?url=xxx before bug 1422456
    "storage/default/about+reader+url=https%3A%2F%2Fexample.com",
  ];
  const obsoleteFilePath =
    "storage/default/https+++example.com/idb/UUID123.tmp";
  const invalidOriginPath = "storage/default/invalid+++example.com/";

  info(
    "Verifying the obsolete origins are removed after the 2_1To2_2 upgrade" +
      " and invalid origins wouldn't block upgrades"
  );

  // This profile only contains the storage.sqlite with schema 2_1, so that we
  // can test the 2_1To2_2 upgrade.
  installPackage("version2_2upgrade_profile");

  // Creating artifical directories for setting up a profile with testing
  // directories.
  for (let originPath of obsoleteOriginPaths.concat(invalidOriginPath)) {
    let originDir = getRelativeFile(originPath);
    originDir.create(Ci.nsIFile.DIRECTORY_TYPE, parseInt(0o755));
  }

  let strangeFile = getRelativeFile(obsoleteFilePath);
  strangeFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, parseInt(0o644));

  info("Upgrading");

  let request = init();
  await requestFinished(request);

  info("Verifying the obsolete origins are removed after the 2_1To2_2 upgrade");

  for (let originPath of obsoleteOriginPaths.concat(obsoleteFilePath)) {
    let folder = getRelativeFile(originPath);
    let exists = folder.exists();
    ok(!exists, "Obsolete origin was removed during origin initialization");
  }

  // Removing the invalid origin directory becasue it will block the temporary
  // storage initialization
  let folder = getRelativeFile(invalidOriginPath);
  folder.remove(true);

  info(
    "Verifying the temporary storage is able to be initialized after " +
      "removing an invalid origin"
  );

  request = initTemporaryStorage();
  await requestFinished(request);
}
