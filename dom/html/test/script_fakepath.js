var { classes: Cc, interfaces: Ci, utils: Cu } = Components;
Cu.importGlobalProperties(["File"]);

addMessageListener("file.open", function (e) {
  var tmpFile = Cc["@mozilla.org/file/directory_service;1"]
                  .getService(Ci.nsIDirectoryService)
                  .QueryInterface(Ci.nsIProperties)
                  .get('ProfD', Ci.nsIFile);
  tmpFile.append('prefs.js');
  sendAsyncMessage("file.opened", {
    data: [ File.createFromNsIFile(tmpFile) ]
  });
});
