/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const EXPORTED_SYMBOLS = ["RemoteSecuritySettings"];

const {RemoteSettings} = ChromeUtils.import("resource://services-settings/remote-settings.js");

const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");
const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

const INTERMEDIATES_BUCKET_PREF          = "security.remote_settings.intermediates.bucket";
const INTERMEDIATES_CHECKED_SECONDS_PREF = "security.remote_settings.intermediates.checked";
const INTERMEDIATES_COLLECTION_PREF      = "security.remote_settings.intermediates.collection";
const INTERMEDIATES_DL_PER_POLL_PREF     = "security.remote_settings.intermediates.downloads_per_poll";
const INTERMEDIATES_ENABLED_PREF         = "security.remote_settings.intermediates.enabled";
const INTERMEDIATES_SIGNER_PREF          = "security.remote_settings.intermediates.signer";
const LOGLEVEL_PREF                      = "browser.policies.loglevel";

const INTERMEDIATES_ERRORS_TELEMETRY     = "INTERMEDIATE_PRELOADING_ERRORS";
const INTERMEDIATES_PENDING_TELEMETRY    = "security.intermediate_preloading_num_pending";
const INTERMEDIATES_PRELOADED_TELEMETRY  = "security.intermediate_preloading_num_preloaded";
const INTERMEDIATES_UPDATE_MS_TELEMETRY  = "INTERMEDIATE_PRELOADING_UPDATE_TIME_MS";

XPCOMUtils.defineLazyGlobalGetters(this, ["fetch"]);

XPCOMUtils.defineLazyGetter(this, "gTextDecoder", () => new TextDecoder());

XPCOMUtils.defineLazyGetter(this, "baseAttachmentsURL", async () => {
  const server = Services.prefs.getCharPref("services.settings.server");
  const serverInfo = await (await fetch(`${server}/`)).json();
  const {capabilities: {attachments: {base_url}}} = serverInfo;
  return base_url;
});

XPCOMUtils.defineLazyGetter(this, "log", () => {
  let { ConsoleAPI } = ChromeUtils.import("resource://gre/modules/Console.jsm");
  return new ConsoleAPI({
    prefix: "RemoteSecuritySettings.jsm",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.jsm for details.
    maxLogLevel: "error",
    maxLogLevelPref: LOGLEVEL_PREF,
  });
});

function hexify(data) {
  return Array.from(data, (c, i) => data.charCodeAt(i).toString(16).padStart(2, "0")).join("");
}

// Hash a UTF-8 string into a hex string with SHA256
function getHash(str) {
  // return the two-digit hexadecimal code for a byte
  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);
  hasher.init(Ci.nsICryptoHash.SHA256);
  let stringStream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(Ci.nsIStringInputStream);
  stringStream.data = str;
  hasher.updateFromStream(stringStream, -1);

  // convert the binary hash data to a hex string.
  return hexify(hasher.finish(false));
}

// Remove all colons from a string
function stripColons(hexString) {
  return hexString.replace(/:/g, "");
}

this.RemoteSecuritySettings = class RemoteSecuritySettings {
    constructor() {
        this.client = RemoteSettings(Services.prefs.getCharPref(INTERMEDIATES_COLLECTION_PREF), {
          bucketNamePref: INTERMEDIATES_BUCKET_PREF,
          lastCheckTimePref: INTERMEDIATES_CHECKED_SECONDS_PREF,
          signerName: Services.prefs.getCharPref(INTERMEDIATES_SIGNER_PREF),
          localFields: ["cert_import_complete"],
        });

        this.client.on("sync", this.onSync.bind(this));
        Services.obs.addObserver(this.onObservePollEnd.bind(this),
                                 "remote-settings:changes-poll-end");

        log.debug("Intermediate Preloading: constructor");
    }

    async updatePreloadedIntermediates() {
        // Bug 1429800: once the CertStateService has the correct interface, also
        // store the whitelist status and crlite enrollment status

        if (!Services.prefs.getBoolPref(INTERMEDIATES_ENABLED_PREF, true)) {
          log.debug("Intermediate Preloading is disabled");
          Services.obs.notifyObservers(null, "remote-security-settings:intermediates-updated", "disabled");
          return;
        }

        // Download attachments that are awaiting download, up to a max.
        const maxDownloadsPerRun = Services.prefs.getIntPref(INTERMEDIATES_DL_PER_POLL_PREF, 100);

        // Bug 1519256: Move this to a separate method that's on a separate timer
        // with a higher frequency (so we can attempt to download outstanding
        // certs more than once daily)
        const current = await this.client.get();
        const waiting = current.filter(record => !record.cert_import_complete);

        log.debug(`There are ${waiting.length} intermediates awaiting download.`);

        TelemetryStopwatch.start(INTERMEDIATES_UPDATE_MS_TELEMETRY);

        const certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(Ci.nsIX509CertDB);
        const col = await this.client.openCollection();

        Promise.all(waiting.slice(0, maxDownloadsPerRun)
          .map(record => this.maybeDownloadAttachment(record, col, certdb))
        ).then(async () => {
          const finalCurrent = await this.client.get();
          const finalWaiting = finalCurrent.filter(record => !record.cert_import_complete);
          const countPreloaded = finalCurrent.length - finalWaiting.length;

          TelemetryStopwatch.finish(INTERMEDIATES_UPDATE_MS_TELEMETRY);
          Services.telemetry.scalarSet(INTERMEDIATES_PRELOADED_TELEMETRY,
                                       countPreloaded);
          Services.telemetry.scalarSet(INTERMEDIATES_PENDING_TELEMETRY,
                                       finalWaiting.length);

          Services.obs.notifyObservers(null, "remote-security-settings:intermediates-updated",
                                       "success");
        });
    }

    async onObservePollEnd(subject, topic, data) {
        log.debug(`onObservePollEnd ${subject} ${topic}`);

        try {
          await this.updatePreloadedIntermediates();
        } catch (err) {
          log.warn(`Unable to update intermediate preloads: ${err}`);

          Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
            .add("failedToObserve");
        }
    }

    // This method returns a promise to RemoteSettingsClient.maybeSync method.
    onSync(event) {
        const {
          data: {deleted},
        } = event;

        if (!Services.prefs.getBoolPref(INTERMEDIATES_ENABLED_PREF, true)) {
          log.debug("Intermediate Preloading is disabled");
          return Promise.resolve();
        }

        log.debug(`Removing ${deleted.length} Intermediate certificates`);
        this.removeCerts(deleted);
        return Promise.resolve();
    }

    /**
     * Downloads the attachment data of the given record. Does not retry,
     * leaving that to the caller.
     * @param  {AttachmentRecord} record The data to obtain
     * @return {Promise}          resolves to a Uint8Array on success
     */
    async _downloadAttachmentBytes(record) {
      const {attachment: {location}} = record;
      const remoteFilePath = (await baseAttachmentsURL) + location;
      const headers = new Headers();
      headers.set("Accept-Encoding", "gzip");

      return fetch(remoteFilePath, {headers})
      .then(resp => {
        log.debug(`Download fetch completed: ${resp.ok} ${resp.status}`);
        if (!resp.ok) {
          Cu.reportError(`Failed to fetch ${remoteFilePath}: ${resp.status}`);

          Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
            .add("failedToFetch");

          return Promise.reject();
        }
        return resp.arrayBuffer();
      })
      .then(buffer => new Uint8Array(buffer));
    }

    /**
     * Attempts to download the attachment, assuming it's not been processed
     * already. Does not retry, and always resolves (e.g., does not reject upon
     * failure.) Errors are reported via Cu.reportError; If you need to know
     * success/failure, check record.cert_import_complete.
     * @param  {AttachmentRecord} record defines which data to obtain
     * @param  {KintoCollection}  col The kinto collection to update
     * @param  {nsIX509CertDB}    certdb The NSS DB to update
     * @return {Promise}          a Promise representing the transaction
     */
    async maybeDownloadAttachment(record, col, certdb) {
      const {attachment: {hash, size}} = record;

      return this._downloadAttachmentBytes(record)
      .then(function(attachmentData) {
        if (!attachmentData || attachmentData.length == 0) {
          // Bug 1519273 - Log telemetry for these rejections
          log.debug(`Empty attachment. Hash=${hash}`);

          Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
            .add("emptyAttachment");

          return Promise.reject();
        }

        // check the length
        if (attachmentData.length !== size) {
          log.debug(`Unexpected attachment length. Hash=${hash} Lengths ${attachmentData.length} != ${size}`);

          Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
            .add("unexpectedLength");

          return Promise.reject();
        }

        // check the hash
        let dataAsString = gTextDecoder.decode(attachmentData);
        let calculatedHash = getHash(dataAsString);
        if (calculatedHash !== hash) {
          log.warn(`Invalid hash. CalculatedHash=${calculatedHash}, Hash=${hash}, data=${dataAsString}`);

          Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
            .add("unexpectedHash");

          return Promise.reject();
        }

        // split off the header and footer, base64 decode, construct the cert
        // from the resulting DER data.
        let b64data = dataAsString.split("-----")[2].replace(/\s/g, "");
        let certDer = atob(b64data);

        try {
          log.debug(`Adding cert. Hash=${hash}. Size=${size}`);

          // We can assume that roots obtained from remote-settings are part of
          // the root program. If they aren't, they won't be used for path-
          // building or have trust anyway, so just add it to the DB.
          certdb.addCert(certDer, ",,");
        } catch (err) {
          Cu.reportError(`Failed to update CertDB: ${err}`);

          Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
            .add("failedToUpdateNSS");

          return Promise.reject();
        }

        record.cert_import_complete = true;
        return col.update(record);
      })
      .catch(() => {
        // Don't abort the outer Promise.all because of an error. Errors were
        // sent using Cu.reportError()
        Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
          .add("failedToDownloadMisc");
      });
    }

    async maybeSync(expectedTimestamp, options) {
      return this.client.maybeSync(expectedTimestamp, options);
    }

    // Note that removing certificates from the DB will likely not have an
    // effect until restart.
    removeCerts(records) {
      let recordsToRemove = records;

      let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(Ci.nsIX509CertDB);

      for (let cert of certdb.getCerts().getEnumerator()) {
        let certHash = stripColons(cert.sha256Fingerprint);
        for (let i = 0; i < recordsToRemove.length; i++) {
          let record = recordsToRemove[i];
          if (record.pubKeyHash == certHash) {
            try {
              certdb.deleteCertificate(cert);
              recordsToRemove.splice(i, 1);
            } catch (err) {
              Cu.reportError(`Failed to remove intermediate certificate Hash=${certHash}: ${err}`);
            }
            break;
          }
        }
      }

      if (recordsToRemove.length > 0) {
        Cu.reportError(`Failed to remove ${recordsToRemove.length} intermediate certificates`);
        Services.telemetry.getHistogramById(INTERMEDIATES_ERRORS_TELEMETRY)
          .add("failedToRemove");
      }
    }
};
