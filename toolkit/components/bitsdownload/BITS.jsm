/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module is used to interact with the Windows BITS component (Background
 * Intelligent Transfer Service). This functionality cannot be used unless on
 * Windows.
 */

"use strict";

const gBITS = Cc["@mozilla.org/bits;1"].getService(Ci.nsIBITS);

var BITS = {
  /**
   * Initializes the BITS download system. This function must be called before
   * any other BITS functions are called.
   * init() should be called again if cancelDownload() or completeDownload() is
   * called.
   *
   * @param jobName The name of the BITS job. This is used both to set the name
   *                during job creation and to verify that a job is ours.
   * @param savePathPrefix The directory that downloads will be saved to.
   *                       Providing a safe directory here ensures that the
   *                       download path cannot be manipulated to save
   *                       files to a malicious location. Downloads are
   *                       guaranteed to be saved to this directory or a
   *                       subdirectory.
   *                       This should end with a trailing slash, as the
   *                       saveRelativePath will be directly appended.
   * @param monitorTimeoutMs The amount of time to wait between download monitor
   *                         notifications. This should be larger than the
   *                         largest monitorIntervalMs that will be passed to
   *                         startDownload(), monitorDownload(), or
   *                         changeMonitorInterval(). This value may not be 0.
   */
  async init(jobName, savePathPrefix, monitorTimeoutMs) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSInitCallback]),
        done() {
          resolve();
        },
      };

      gBITS.init(jobName, savePathPrefix, monitorTimeoutMs, callback);
    });
  },

  /**
   * Downloads the specified URL to the specified location within the
   * savePathPrefix passed to init().
   *
   * @param downloadURL The URL to be downloaded. Must be from a mozilla.org
   *                    domain or this function will fail.
   * @param saveRelativePath The location to download to. The path given should
   *                         be a path relative to the savePathPrefix passed to
   *                         init().
   * @param proxy Specifies what proxy to use when downloading. Valid values are
   *              listed below.
   * @param monitorIntervalMs The number of milliseconds between download status
   *                          notifications.
   * @param observer An observer to be notified of various events.
   *                 OnStartRequest is called once the BITS job has been
   *                 created. OnStopRequest is called when the file transfer
   *                 has completed or when an error occurs. If this object
   *                 implements nsIProgressEventSink, then its OnProgress method
   *                 will be called as data is transferred.
   *                 IMPORTANT NOTE: When OnStopRequest is called, the download
   *                 has completed, but completeDownload() still needs to be
   *                 called to save the file to the filesystem.
   * @param context User defined object forwarded to observer's methods
   * @return A Promise that resolves with the nsIRequest object representing the
   *         BITS request. The BITS job id will be the name attribute of the
   *         request.
   *         The Promise may reject if the BITS job could not be started.
   */
  async startDownload(downloadURL, saveRelPath, proxy, monitorIntervalMs,
                      observer, context) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSStartDownloadCallback]),
        success(id) {
          resolve(gBITS.QueryInterface(Ci.nsIRequest));
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.startDownload(downloadURL, saveRelPath, proxy, monitorIntervalMs,
                          observer, context, callback);
    });
  },

  PROXY_NONE:       gBITS.PROXY_NONE,
  PROXY_PRECONFIG:  gBITS.PROXY_PRECONFIG,
  PROXY_AUTODETECT: gBITS.PROXY_AUTODETECT,

  /**
   * Requests notifications on how much download progress BITS has made.
   *
   * @param id The GUID of the download to monitor.
   * @param monitorIntervalMs The number of milliseconds between download status
   *                          notifications.
   * @param observer An observer to be notified of various events.
   *                 OnStartRequest is called once the BITS job has been
   *                 created. OnStopRequest is called when the file transfer
   *                 has completed or when an error occurs. If this object
   *                 implements nsIProgressEventSink, then its OnProgress method
   *                 will be called as data is transferred.
   *                 IMPORTANT NOTE: When OnStopRequest is called, the download
   *                 has completed, but completeDownload() still needs to be
   *                 called to save the file to the filesystem.
   * @param context User defined object forwarded to observer's methods
   * @return A Promise that resolves with the nsIRequest object requesting the
   *         BITS request.
   *         The Promise may reject if monitoring could not be started.
   */
  async monitorDownload(id, monitorIntervalMs, observer, context) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSMonitorDownloadCallback]),
        success() {
          resolve(gBITS.QueryInterface(Ci.nsIRequest));
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.monitorDownload(id, monitorIntervalMs, observer, context, callback);
    });
  },

  /**
   * Requests a change to the frequency that Firefox is receiving download
   * status notifications.
   *
   * @param id The GUID of the download to monitor.
   * @param monitorIntervalMs The number of milliseconds between download status
   *                          notifications.
   * @return A Promise that resolves when the notification frequency has been
   *         changed.
   *         The Promise may reject if the frequency could not be changed.
   */
  async changeMonitorInterval(id, monitorIntervalMs) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSChangeMonitorIntervalCallback]),
        success() {
          resolve();
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.changeMonitorInterval(id, monitorIntervalMs, callback);
    });
  },

  /**
   * Cancels the download specified by the GUID given. This must be the id for
   * the download started by Firefox.
   *
   * @param id The GUID of the download to monitor.
   * @param status The reason for cancelling the request.
   * @return A Promise that resolves when the download has been cancelled.
   *         The Promise may reject if the download could not be cancelled.
   */
  async cancelDownload(id, status) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSCancelDownloadCallback]),
        success() {
          resolve();
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.cancelDownload(id, status, callback);
    });
  },

  /**
   * Sets the priority of the BITS job to high (i.e. foreground download).
   *
   * @param id The GUID of the download.
   * @return A Promise that resolves when operation has completed.
   *         The Promise may reject if the operation failed
   */
  async setPriorityHigh(id) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSSetPriorityHighCallback]),
        success() {
          resolve();
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.setPriorityHigh(id, callback);
    });
  },

  /**
   * Sets the priority of the BITS job to low (i.e. background download).
   *
   * @param id The GUID of the download.
   * @return A Promise that resolves when operation has completed.
   *         The Promise may reject if the operation failed
   */
  async setPriorityLow(id) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSSetPriorityLowCallback]),
        success() {
          resolve();
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.setPriorityLow(id, callback);
    });
  },

  /**
   * Completes the download, moving it out of the BITS system and onto the
   * disk location specified when startDownload was called.
   *
   * @param id The GUID of the download to complete.
   * @return A Promise that resolves when operation has completed.
   *         The Promise may reject if the operation failed
   */
  async completeDownload(id) {
    return new Promise(async (resolve, reject) => {
      let callback = {
        QueryInterface: ChromeUtils.generateQI([Ci.nsIBITSCompleteDownloadCallback]),
        success() {
          resolve();
        },
        BITSFailure(failure) {
          reject(failure);
        },
        failure(failure) {
          reject(failure);
        },
      };

      gBITS.completeDownload(id, callback);
    });
  },
};

const EXPORTED_SYMBOLS = [
  "BITS",
];
