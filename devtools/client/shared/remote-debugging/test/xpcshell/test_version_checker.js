/* global equal */

"use strict";

const {
  _compareVersionCompatibility,
  checkVersionCompatibility,
  COMPATIBILITY_STATUS,
} = require("devtools/client/shared/remote-debugging/version-checker");

const TEST_DATA = [
  {
    description: "same build date and same version number",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version in range (-1)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "59.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version in range (-2)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "58.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version in range (-2 Nightly)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "58.0a1",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same build date and older version out of range (-3)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "57.0",
    expected: COMPATIBILITY_STATUS.TOO_OLD,
  },
  {
    description: "same build date and newer version out of range (+1)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "61.0",
    expected: COMPATIBILITY_STATUS.TOO_RECENT,
  },
  {
    description: "same major version and build date in range (-10 days)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190121000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same major version and build date in range (+2 days)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
  {
    description: "same major version and build date out of range (+8 days)",
    localBuildId: "20190131000000",
    localVersion: "60.0",
    runtimeBuildId: "20190208000000",
    runtimeVersion: "60.0",
    expected: COMPATIBILITY_STATUS.TOO_RECENT,
  },
  {
    description: "debugger backward compatibility issue for 67 -> 66",
    localBuildId: "20190131000000",
    localVersion: "67.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "66.0",
    expected: COMPATIBILITY_STATUS.TOO_OLD_67_DEBUGGER,
  },
  {
    description: "debugger backward compatibility issue for 67 -> 65",
    localBuildId: "20190131000000",
    localVersion: "67.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "65.0",
    expected: COMPATIBILITY_STATUS.TOO_OLD_67_DEBUGGER,
  },
  {
    description: "debugger backward compatibility issue for 68 -> 66",
    localBuildId: "20190131000000",
    localVersion: "68.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "66.0",
    expected: COMPATIBILITY_STATUS.TOO_OLD_67_DEBUGGER,
  },
  {
    description:
      "regular compatibility issue for 67 -> 64 (not debugger-related)",
    localBuildId: "20190131000000",
    localVersion: "67.0",
    runtimeBuildId: "20190131000000",
    runtimeVersion: "64.0",
    expected: COMPATIBILITY_STATUS.TOO_OLD,
  },
  {
    description:
      "debugger backward compatibility error not raised for 68 -> 67",
    localBuildId: "20190131000000",
    localVersion: "68.0",
    runtimeBuildId: "20190202000000",
    runtimeVersion: "67.0",
    expected: COMPATIBILITY_STATUS.COMPATIBLE,
  },
];

add_task(async function testVersionChecker() {
  for (const testData of TEST_DATA) {
    const localDescription = {
      appbuildid: testData.localBuildId,
      platformversion: testData.localVersion,
    };

    const runtimeDescription = {
      appbuildid: testData.runtimeBuildId,
      platformversion: testData.runtimeVersion,
    };

    const report = _compareVersionCompatibility(
      localDescription,
      runtimeDescription
    );
    equal(
      report.status,
      testData.expected,
      "Expected status for test: " + testData.description
    );
  }
});

add_task(async function testVersionCheckWithVeryOldClient() {
  // Use an empty object as debugger client, calling any method on it will fail.
  const emptyClient = {};
  const report = await checkVersionCompatibility(emptyClient);
  equal(
    report.status,
    COMPATIBILITY_STATUS.TOO_OLD,
    "Report status too old if debugger client is not implementing expected interface"
  );
});
