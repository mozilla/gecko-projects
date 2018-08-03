/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

ChromeUtils.import("resource://gre/modules/Services.jsm");

add_task(async function test_attribution() {
  let appPath = Services.dirsvc.get("GreD", Ci.nsIFile).parent.parent.path;
  let attributionSvc = Cc["@mozilla.org/mac-attribution;1"]
                         .getService(Ci.nsIMacAttributionService);

  attributionSvc.setReferrerUrl(appPath, "", true);
  let referrer = attributionSvc.getReferrerUrl(appPath);
  equal(referrer, "", "force an empty referrer url");

  // Set a url referrer
  let url = "http://example.com";
  attributionSvc.setReferrerUrl(appPath, url, true);
  referrer = attributionSvc.getReferrerUrl(appPath);
  equal(referrer, url, "overwrite referrer url");

  // Does not overwrite existing properties.
  attributionSvc.setReferrerUrl(appPath, "http://test.com", false);
  referrer = attributionSvc.getReferrerUrl(appPath);
  equal(referrer, url, "referrer url is not changed");
});
