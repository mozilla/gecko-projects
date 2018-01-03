/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function run_test() {
  // If we can't get the profiler component then assume gecko was
  // built without it and pass all the tests
  var profilerCc = Cc["@mozilla.org/tools/profiler;1"];
  if (!profilerCc)
    return;

  var profiler = profilerCc.getService(Ci.nsIProfiler);
  if (!profiler)
    return;

  Assert.ok(!profiler.IsActive());
  Assert.ok(!profiler.IsPaused());

  profiler.StartProfiler(1000, 10, [], 0);

  Assert.ok(profiler.IsActive());

  profiler.PauseSampling();

  Assert.ok(profiler.IsPaused());

  profiler.ResumeSampling();

  Assert.ok(!profiler.IsPaused());

  profiler.StopProfiler();
  Assert.ok(!profiler.IsActive());
  Assert.ok(!profiler.IsPaused());
  do_test_finished();
}
