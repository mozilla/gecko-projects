
// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

"use strict";
do_get_profile(); // must be called before getting nsIX509CertDB

const { RemoteSecuritySettings } = ChromeUtils.import("resource://testing-common/psm/RemoteSecuritySettings.jsm");

let remoteSecSetting = new RemoteSecuritySettings();
let server;

let intermediate1Data;
let intermediate2Data;

let currentTime = 0;

function cyclingIteratorGenerator(items, count = null) {
  return () => cyclingIterator(items, count);
}

function* cyclingIterator(items, count = null) {
  if (count == null) {
    count = items.length;
  }
  for (let i = 0; i < count; i++) {
    yield items[i % items.length];
  }
}

function getTime() {
  currentTime = currentTime + 1000 * 60 * 60 * 12;
  return currentTime;
}

function getHash(aStr) {
  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);
  hasher.init(Ci.nsICryptoHash.SHA256);
  let stringStream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(Ci.nsIStringInputStream);
  stringStream.data = aStr;
  hasher.updateFromStream(stringStream, -1);

  // convert the binary hash data to a hex string.
  return hasher.finish(true);
}

function setupKintoPreloadServer(certGenerator, options = {
  attachmentCB: null,
  hashFunc: null,
  lengthFunc: null,
}) {
  const dummyServerURL = `http://localhost:${server.identity.primaryPort}/v1`;
  Services.prefs.setCharPref("services.settings.server", dummyServerURL);
  Services.prefs.setBoolPref("services.settings.verify_signature", false);

  const configPath = "/v1/";
  const recordsPath = "/v1/buckets/security-state/collections/intermediates/records";
  const attachmentsPath = "/attachments/";

  if (options.hashFunc == null) {
    options.hashFunc = getHash;
  }
  if (options.lengthFunc == null) {
    options.lengthFunc = arr => arr.length;
  }

  function setHeader(response, headers) {
    for (let headerLine of headers) {
      let headerElements = headerLine.split(":");
      response.setHeader(headerElements[0], headerElements[1].trimLeft());
    }
    response.setHeader("Date", (new Date()).toUTCString());
  }

  // Basic server information, all static
  server.registerPathHandler(configPath, (request, response) => {
    try {
      const respData = getResponseData(request, server.identity.primaryPort);
      if (!respData) {
        do_throw(`unexpected ${request.method} request for ${request.path}?${request.queryString}`);
        return;
      }

      response.setStatusLine(null, respData.status.status,
                             respData.status.statusText);
      setHeader(response, respData.responseHeaders);
      response.write(respData.responseBody);
    } catch (e) {
      info(e);
    }
  });

  // Lists of certs
  server.registerPathHandler(recordsPath, (request, response) => {
    response.setStatusLine(null, 200, "OK");
    setHeader(response, [
        "Access-Control-Allow-Origin: *",
        "Access-Control-Expose-Headers: Retry-After, Content-Length, Alert, Backoff",
        "Content-Type: application/json; charset=UTF-8",
        "Server: waitress",
        "Etag: \"1000\"",
    ]);

    let output = [];
    let count = 1;
    let certDB = Cc["@mozilla.org/security/x509certdb;1"]
                 .getService(Ci.nsIX509CertDB);

    let certIterator = certGenerator();
    let result = certIterator.next();
    while (!result.done) {
      let certBytes = result.value;
      let cert = certDB.constructX509FromBase64(pemToBase64(certBytes));

      output.push({
        "details": {
          "who": "",
          "why": "",
          "name": "",
          "created": "",
        },
        "subject": "",
        "attachment": {
          "hash": options.hashFunc(certBytes),
          "size": options.lengthFunc(certBytes),
          "filename": `intermediate certificate #${count}.pem`,
          "location": `int${count}`,
          "mimetype": "application/x-pem-file",
        },
        "whitelist": false,
        "pubKeyHash": cert.sha256Fingerprint,
        "crlite_enrolled": "true",
        "id": `78cf8900-fdea-4ce5-f8fb-${count}`,
        "last_modified": 1000,
      });

      count++;
      result = certIterator.next();
    }

    response.write(JSON.stringify({ data: output }));
  });

  // Certificate data
  server.registerPrefixHandler(attachmentsPath, (request, response) => {
    setHeader(response, [
        "Access-Control-Allow-Origin: *",
        "Access-Control-Expose-Headers: Retry-After, Content-Length, Alert, Backoff",
        "Content-Type: application/x-pem-file; charset=UTF-8",
        "Server: waitress",
        "Etag: \"1000\"",
    ]);

    let identifier = request.path.match(/\d+$/)[0];
    let count = 1;

    let certIterator = certGenerator();
    let result = certIterator.next();
    while (!result.done) {
      // Could do the modulus of the certIterator to get the right data,
      // but that requires plumbing through knowledge of those offsets, so
      // let's just loop. It's not that slow.

      if (count == identifier) {
        response.setStatusLine(null, 200, "OK");
        response.write(result.value);
        if (options.attachmentCB) {
          options.attachmentCB(identifier, true);
        }
        return;
      }

      count++;
      result = certIterator.next();
    }

    response.setStatusLine(null, 404, `Identifier ${identifier} Not Found`);
    if (options.attachmentCB) {
      options.attachmentCB(identifier, false);
    }
  });
}

add_task(async function test_preload_empty() {
  Services.prefs.setBoolPref("security.remote_settings.intermediates.enabled", true);

  let countDownloadAttempts = 0;
  setupKintoPreloadServer(
    cyclingIteratorGenerator([]),
    found => { countDownloadAttempts++; }
  );

  let certDB = Cc["@mozilla.org/security/x509certdb;1"]
               .getService(Ci.nsIX509CertDB);

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile("test_intermediate_preloads/ee.pem");
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // sync to the kinto server.
  await remoteSecSetting.maybeSync(getTime());

  Assert.equal(countDownloadAttempts, 0, "There should have been no downloads");

  // check that ee cert 1 is unknown
  await checkCertErrorGeneric(certDB, ee_cert, SEC_ERROR_UNKNOWN_ISSUER,
                              certificateUsageSSLServer);
});

add_task(async function test_preload_disabled() {
  Services.prefs.setBoolPref("security.remote_settings.intermediates.enabled", false);

  let countDownloadAttempts = 0;
  setupKintoPreloadServer(
    cyclingIteratorGenerator([intermediate1Data]),
    {attachmentCB: (identifier, attachmentFound) => { countDownloadAttempts++; }}
  );

  // sync to the kinto server.
  await remoteSecSetting.maybeSync(getTime());

  Assert.equal(countDownloadAttempts, 0, "There should have been no downloads");
});

add_task(async function test_preload_invalid_hash() {
  Services.prefs.setBoolPref("security.remote_settings.intermediates.enabled", true);

  let countDownloadAttempts = 0;
  setupKintoPreloadServer(
    cyclingIteratorGenerator([intermediate1Data]),
    {
      attachmentCB: (identifier, attachmentFound) => { countDownloadAttempts++; },
      hashFunc: data => "invalidHash",
    }
  );

  // sync to the kinto server.
  await remoteSecSetting.maybeSync(getTime());

  Assert.equal(countDownloadAttempts, 1, "There should have been one download attempt");

  let certDB = Cc["@mozilla.org/security/x509certdb;1"]
               .getService(Ci.nsIX509CertDB);

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile("test_intermediate_preloads/ee.pem");
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // We should still have a missing intermediate.
  await checkCertErrorGeneric(certDB, ee_cert, SEC_ERROR_UNKNOWN_ISSUER,
                              certificateUsageSSLServer);
});

add_task(async function test_preload_invalid_length() {
  Services.prefs.setBoolPref("security.remote_settings.intermediates.enabled", true);

  let countDownloadAttempts = 0;
  setupKintoPreloadServer(
    cyclingIteratorGenerator([intermediate1Data]),
    {
      attachmentCB: (identifier, attachmentFound) => { countDownloadAttempts++; },
      lengthFunc: data => 42,
    }
  );

  // sync to the kinto server.
  await remoteSecSetting.maybeSync(getTime());

  Assert.equal(countDownloadAttempts, 1, "There should have been one download attempt");

  let certDB = Cc["@mozilla.org/security/x509certdb;1"]
               .getService(Ci.nsIX509CertDB);

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile("test_intermediate_preloads/ee.pem");
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // We should still have a missing intermediate.
  await checkCertErrorGeneric(certDB, ee_cert, SEC_ERROR_UNKNOWN_ISSUER,
                              certificateUsageSSLServer);
});

add_task(async function test_preload_basic() {
  Services.prefs.setBoolPref("security.remote_settings.intermediates.enabled", true);

  let countDownloadAttempts = 0;
  setupKintoPreloadServer(
    cyclingIteratorGenerator([intermediate1Data, intermediate2Data]),
    {attachmentCB: (identifier, attachmentFound) => { countDownloadAttempts++; }}
  );

  let certDB = Cc["@mozilla.org/security/x509certdb;1"]
               .getService(Ci.nsIX509CertDB);

  // load the first root and end entity, ignore the initial intermediate
  addCertFromFile(certDB, "test_intermediate_preloads/ca.pem", "CTu,,");

  let ee_cert = constructCertFromFile("test_intermediate_preloads/ee.pem");
  notEqual(ee_cert, null, "EE cert should have successfully loaded");

  // load the second end entity, ignore both intermediate and root
  let ee_cert_2 = constructCertFromFile("test_intermediate_preloads/ee2.pem");
  notEqual(ee_cert_2, null, "EE cert 2 should have successfully loaded");

  // check that the missing intermediate causes an unknown issuer error, as
  // expected, in both cases
  await checkCertErrorGeneric(certDB, ee_cert, SEC_ERROR_UNKNOWN_ISSUER,
                              certificateUsageSSLServer);
  await checkCertErrorGeneric(certDB, ee_cert_2, SEC_ERROR_UNKNOWN_ISSUER,
                              certificateUsageSSLServer);

  // sync to the kinto server.
  await remoteSecSetting.maybeSync(getTime());

  Assert.equal(countDownloadAttempts, 2, "There should have been 2 downloads");

  // check that ee cert 1 verifies now the update has happened and there is
  // an intermediate
  await checkCertErrorGeneric(certDB, ee_cert, PRErrorCodeSuccess,
                              certificateUsageSSLServer);

  // check that ee cert 2 does not verify - since we don't know the issuer of
  // this certificate
  await checkCertErrorGeneric(certDB, ee_cert_2, SEC_ERROR_UNKNOWN_ISSUER,
                              certificateUsageSSLServer);
});


add_task(async function test_preload_200() {
  Services.prefs.setBoolPref("security.remote_settings.intermediates.enabled", true);

  let countDownloadedAttachments = 0;
  let countMissingAttachments = 0;
  setupKintoPreloadServer(
    cyclingIteratorGenerator([intermediate1Data, intermediate2Data], 200),
    {
      attachmentCB: (identifier, attachmentFound) => {
        if (!attachmentFound) {
          countMissingAttachments++;
        } else {
          countDownloadedAttachments++;
        }
      },
    }
  );

  // sync to the kinto server.
  await remoteSecSetting.maybeSync(getTime());

  Assert.equal(countMissingAttachments, 0, "There should have been no missing attachments");
  Assert.equal(countDownloadedAttachments, 100, "There should have been only 100 downloaded");

  // sync to the kinto server again
  await remoteSecSetting.maybeSync(getTime());

  await Promise.resolve();

  Assert.equal(countMissingAttachments, 0, "There should have been no missing attachments");
  Assert.equal(countDownloadedAttachments, 198, "There should have been now 198 downloaded, because 2 existed in an earlier test");
});


function run_test() {
  // Ensure that signature verification is disabled to prevent interference
  // with basic certificate sync tests
  Services.prefs.setBoolPref("services.blocklist.signing.enforced", false);

  let intermediate1File = do_get_file("test_intermediate_preloads/int.pem", false);
  intermediate1Data = readFile(intermediate1File);

  let intermediate2File = do_get_file("test_intermediate_preloads/int2.pem", false);
  intermediate2Data = readFile(intermediate2File);

  // Set up an HTTP Server
  server = new HttpServer();
  server.start(-1);

  run_next_test();

  registerCleanupFunction(function() {
    server.stop(() => { });
  });
}

// get a response for a given request from sample data
function getResponseData(req, port) {
  info(`Resource requested: ${req.method}:${req.path}?${req.queryString}\n\n`);
  const cannedResponses = {
    "OPTIONS": {
      "responseHeaders": [
        "Access-Control-Allow-Headers: Content-Length,Expires,Backoff,Retry-After,Last-Modified,Total-Records,ETag,Pragma,Cache-Control,authorization,content-type,if-none-match,Alert,Next-Page",
        "Access-Control-Allow-Methods: GET,HEAD,OPTIONS,POST,DELETE,OPTIONS",
        "Access-Control-Allow-Origin: *",
        "Content-Type: application/json; charset=UTF-8",
        "Server: waitress",
      ],
      "status": {status: 200, statusText: "OK"},
      "responseBody": "null",
    },
    "GET:/v1/": {
      "responseHeaders": [
        "Access-Control-Allow-Origin: *",
        "Access-Control-Expose-Headers: Retry-After, Content-Length, Alert, Backoff",
        "Content-Type: application/json; charset=UTF-8",
        "Server: waitress",
      ],
      "status": {status: 200, statusText: "OK"},
      "responseBody": JSON.stringify({
        "settings": {
          "batch_max_requests": 25,
        },
        "url": `http://localhost:${port}/v1/`,
        "documentation": "https://kinto.readthedocs.org/",
        "version": "1.5.1",
        "commit": "cbc6f58",
        "hello": "kinto",
        "capabilities": {
          "attachments": {
            "base_url": `http://localhost:${port}/attachments/`,
          },
        },
      }),
    },
  };
  let result = cannedResponses[`${req.method}:${req.path}?${req.queryString}`] ||
               cannedResponses[`${req.method}:${req.path}`] ||
               cannedResponses[req.method];
  return result;
}
