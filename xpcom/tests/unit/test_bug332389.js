var Cc = Components.classes;
var Ci = Components.interfaces;

function run_test() {
  var f =
      Cc["@mozilla.org/file/directory_service;1"].
      getService(Ci.nsIProperties).get("CurProcD", Ci.nsIFile);

  var terminated = false;
  for (var i = 0; i < 100; i++) {
    if (f == null) {
      terminated = true;
      break;
    }
    f = f.parent;
  }

  do_check_true(terminated);
}
