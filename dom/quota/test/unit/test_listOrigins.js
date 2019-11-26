/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

async function CreateTestEnvironment(origins) {
  let request;
  for (let origin of origins) {
    request = initStorageAndOrigin(getPrincipal(origin.origin), "default");
    await requestFinished(request);
  }

  request = reset();
  await requestFinished(request);
}

async function testSteps() {
  const origins = [
    {
      origin: "https://example.com",
      path: "https+++example.com",
    },

    {
      origin: "https://localhost",
      path: "https+++localhost",
    },

    {
      origin: "https://www.mozilla.org",
      path: "https+++www.mozilla.org",
    },
  ];

  function verifyResult(result, expectedOrigins) {
    ok(result instanceof Array, "Got an array object");
    ok(result.length == expectedOrigins.length, "Correct number of elements");

    info("Sorting elements");

    result.sort(function(a, b) {
      let originA = a.origin;
      let originB = b.origin;

      if (originA < originB) {
        return -1;
      }
      if (originA > originB) {
        return 1;
      }
      return 0;
    });

    info("Verifying elements");

    for (let i = 0; i < result.length; i++) {
      let a = result[i];
      let b = expectedOrigins[i];
      ok(a.origin == b.origin, "Origin equals");
    }
  }

  info("Creating test environment");

  await CreateTestEnvironment(origins);

  info("Getting origins after initializing the storage");

  await new Promise(resolve => {
    listOrigins(req => {
      verifyResult(req.result, origins);
      resolve();
    });
  });
}
