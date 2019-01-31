/*
 * Tests that from an empty database a default profile is created.
 */

add_task(async () => {
  let service = getProfileService();
  let { profile, didCreate } = selectStartupProfile();

  checkStartupReason("firstrun-created-default");

  let profileData = readProfilesIni();
  let installData = readInstallsIni();
  checkProfileService(profileData, installData);

  Assert.ok(didCreate, "Should have created a new profile.");
  Assert.equal(profile, service.defaultProfile, "Should now be the default profile.");
  Assert.equal(profile.name, DEDICATED_NAME, "Should have created a new profile with the right name.");
  Assert.ok(!service.createdAlternateProfile, "Should not have created an alternate profile.");

  Assert.ok(profileData.options.startWithLastProfile, "Should be set to start with the last profile.");
  Assert.equal(profileData.profiles.length, 2, "Should have the right number of profiles.");

  profile = profileData.profiles[0];
  Assert.equal(profile.name, "default", "Should have the right name.");
  Assert.ok(profile.default, "Should be marked as the old-style default.");

  profile = profileData.profiles[1];
  Assert.equal(profile.name, DEDICATED_NAME, "Should have the right name.");
  Assert.ok(!profile.default, "Should not be marked as the old-style default.");

  let hash = xreDirProvider.getInstallHash();
  Assert.ok(installData.installs[hash].locked, "Should have locked the profile");
});
