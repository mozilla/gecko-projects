/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

let nsLoginInfo = new Components.Constructor("@mozilla.org/login-manager/loginInfo;1",
                                             Ci.nsILoginInfo, "init");
let TEST_LOGIN1 = new nsLoginInfo("https://example.com/", "https://example.com/", null, "user1", "pass1", "username", "password");
let TEST_LOGIN2 = new nsLoginInfo("https://example2.com/", "https://example2.com/", null, "user2", "pass2", "username", "password");

add_task(async function setup() {
  await SpecialPowers.pushPrefEnv({"set": [["signon.management.overrideURI", "about:logins?filter=%DOMAIN%"]] });

  for (let login of [TEST_LOGIN1, TEST_LOGIN2]) {
    let storageChangedPromised = TestUtils.topicObserved("passwordmgr-storage-changed",
                                                         (_, data) => data == "addLogin");
    login = Services.logins.addLogin(login);
    await storageChangedPromised;
  }
  let tabOpenedPromise =
    BrowserTestUtils.waitForNewTab(gBrowser, "about:logins?filter=" + encodeURIComponent(TEST_LOGIN1.hostname), true);
  LoginHelper.openPasswordManager(window, { filterString: TEST_LOGIN1.hostname, entryPoint: "preferences" });
  await tabOpenedPromise;
  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(gBrowser.selectedTab);
    Services.logins.removeAllLogins();
  });
});

add_task(async function test_query_parameter_filter() {
  let browser = gBrowser.selectedBrowser;
  let vanillaLogins = [
    LoginHelper.loginToVanillaObject(TEST_LOGIN1),
    LoginHelper.loginToVanillaObject(TEST_LOGIN2),
  ];
  await ContentTask.spawn(browser, vanillaLogins, async (logins) => {
    let loginList = Cu.waiveXrays(content.document.querySelector("login-list"));
    await ContentTaskUtils.waitForCondition(() => {
      return loginList._logins.length == 2;
    }, "Waiting for logins to be cached");

    let loginFilter = Cu.waiveXrays(content.document.querySelector("login-filter"));
    is(loginFilter.value, logins[0].hostname, "The filter should be prepopulated");

    let hiddenLoginListItems = loginList.shadowRoot.querySelectorAll("login-list-item[hidden]");
    let visibleLoginListItems = loginList.shadowRoot.querySelectorAll("login-list-item:not([hidden])");
    is(visibleLoginListItems.length, 1, "One login should be visible");
    is(visibleLoginListItems[0].guid, logins[0].guid, "TEST_LOGIN1 should be visible");
    is(hiddenLoginListItems.length, 1, "One login should be hidden");
    is(hiddenLoginListItems[0].guid, logins[1].guid, "TEST_LOGIN2 should be hidden");
  });
});
