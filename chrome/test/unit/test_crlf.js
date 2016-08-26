registerManifests([do_get_file("data/test_crlf.manifest")]);

function run_test()
{
  let cr = Cc["@mozilla.org/chrome/chrome-registry;1"].
    getService(Ci.nsIChromeRegistry);

  let ios = Cc["@mozilla.org/network/io-service;1"].
    getService(Ci.nsIIOService);
  let sourceURI = ios.newURI("chrome://test_crlf/content/", null, null);
  // this throws for packages that are not registered
  let file = cr.convertChromeURL(sourceURI).QueryInterface(Ci.nsIFileURL).file;
  
  do_check_true(file.equals(do_get_file("data/test_crlf.xul", true)));
}
