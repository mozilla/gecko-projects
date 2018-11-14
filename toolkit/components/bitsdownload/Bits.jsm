/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module is used to interact with the Windows BITS component (Background
 * Intelligent Transfer Service). This functionality cannot be used unless on
 * Windows.
 */

"use strict";

const gBits = Cc["@mozilla.org/bits;1"].getService(Ci.nsIBits);

/**
 * This class will wrap the errors returned by the nsIBits interface to make
 * them more uniform and more easily consumable by JS.
 *
 * The values of stored by this error type are entirely numeric. This is
 * intentional and is meant to allow these values to be more easily passed to
 * telemetry. The values correspond to the constants defined in nsIBits.
 *
 * The type of BitsError.code is dependant on the value of BitsError.codeType.
 * It may be null, a number (corresponding to an nsresult or hresult value),
 * a string, or an exception.
 */
class BitsError extends Error {
  // If codeType == "none", code may be unspecified.
  constructor(type, action, stage, codeType, code) {
    let message = "BitsError {" +
                  "type: " + type +
                  ", action: " + action +
                  ", stage: " + stage;
    switch (codeType) {
      case gBits.ERROR_CODE_TYPE_NONE:
        code = null;
        message += ", codeType: none";
        break;
      case gBits.ERROR_CODE_TYPE_NSRESULT:
        message += ", codeType: nsresult, code: " + code;
        break;
      case gBits.ERROR_CODE_TYPE_HRESULT:
        message += ", codeType: hresult, code: " + code;
        break;
      case gBits.ERROR_CODE_TYPE_STRING:
        message += ", codeType: string, code: " + JSON.stringify(code);
        break;
      case gBits.ERROR_CODE_TYPE_EXCEPTION:
        message += ", codeType: exception, code: " + code;
        break;
      default:
        message += ", codeType: invalid";
        break;
    }
    message += "}";
    super(message);

    this.type = type;
    this.action = action;
    this.stage = stage;
    this.codeType = codeType;
    this.code = code;
    this.name = this.constructor.name;
  }
}

function makeInterfaceCallback(resolve, reject) {
  return {
    QueryInterface: ChromeUtils.generateQI([Ci.nsIBitsNewRequestCallback]),
    success(request) {
      resolve(new BitsRequest(request));
    },
    failure(type, action, stage) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_NONE);
      reject(error);
    },
    failureNsresult(type, action, stage, code) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_NSRESULT, code);
      reject(error);
    },
    failureHresult(type, action, stage, code) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_HRESULT, code);
      reject(error);
    },
    failureString(type, action, stage, message) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_STRING, message);
      reject(error);
    },
  };
}

function makeRequestCallback(resolve, reject) {
  return {
    QueryInterface: ChromeUtils.generateQI([Ci.nsIBitsCallback]),
    success() {
      resolve();
    },
    failure(type, action, stage) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_NONE);
      reject(error);
    },
    failureNsresult(type, action, stage, code) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_NSRESULT, code);
      reject(error);
    },
    failureHresult(type, action, stage, code) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_HRESULT, code);
      reject(error);
    },
    failureString(type, action, stage, message) {
      let error = new BitsError(type, action, stage,
                                gBits.ERROR_CODE_TYPE_STRING, message);
      reject(error);
    },
  };
}

/**
 * This class is a wrapper around nsIBitsRequest that converts functions taking
 * callbacks to asynchronous functions. This class implements nsIRequest.
 */
class BitsRequest {
  constructor(request) {
    this._request = request;
    this._request.QueryInterface(Ci.nsIRequest);
    this._request.QueryInterface(Ci.nsIBitsRequest);
  }

  /**
   * This is the nsIRequest implementation. Since this._request is an
   * nsIRequest, these functions just call the corresponding method on it.
   *
   * Note that nsIBitsRequest does not yet properly implement load groups or
   * load flags. This class will still forward those calls, but they will have
   * not succeed.
   */
  get name() {
    return this._request.name;
  }
  isPending() {
    return this._request.isPending();
  }
  get status() {
    return this._request.status;
  }
  cancel(status) {
    return this._request.cancel(status);
  }
  suspend() {
    return this._request.suspend();
  }
  resume() {
    return this._request.resume();
  }
  get loadGroup() {
    return this._request.loadGroup;
  }
  set loadGroup(group) {
    this._request.loadGroup = group;
  }
  get loadFlags() {
    return this._request.loadFlags;
  }
  set loadFlags(flags) {
    this._request.loadFlags = flags;
  }

  /**
   * This function wraps nsIBitsRequest::bitsId.
   */
  get bitsId() {
    return this._request.bitsId;
  }

  /**
   * This function wraps nsIBitsRequest::changeMonitorInterval.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async changeMonitorInterval(monitorIntervalMs) {
    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.changeMonitorInterval(monitorIntervalMs, callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_CHANGE_MONITOR_INTERVAL,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }

  /**
   * This function wraps nsIBitsRequest::cancelAsync.
   *
   * Instead of taking a callback, the function is asychronous.
   * Adds a default status of NS_ERROR_ABORT if one is not provided.
   */
  async cancelAsync(status) {
    if (status === undefined) {
      status = Cr.NS_ERROR_ABORT;
    }

    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.cancelAsync(status, callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_CANCEL,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }

  /**
   * This function wraps nsIBitsRequest::setPriorityHigh.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async setPriorityHigh() {
    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.setPriorityHigh(callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_SET_PRIORITY,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }

  /**
   * This function wraps nsIBitsRequest::setPriorityLow.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async setPriorityLow() {
    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.setPriorityLow(callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_SET_PRIORITY,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }

  /**
   * This function wraps nsIBitsRequest::complete.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async complete() {
    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.complete(callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_COMPLETE,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }

  /**
   * This function wraps nsIBitsRequest::suspendAsync.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async suspendAsync() {
    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.suspendAsync(callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_SUSPEND,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }

  /**
   * This function wraps nsIBitsRequest::resumeAsync.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async resumeAsync() {
    return new Promise((resolve, reject) => {
      let callback = makeRequestCallback(resolve, reject);

      try {
        this._request.resumeAsync(callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_RESUME,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  }
}
BitsRequest.prototype.QueryInterface = ChromeUtils.generateQI([Ci.nsIRequest]);

var Bits = {
  /**
   * This function wraps nsIBits::initialized.
   */
  get initialized() {
    return gBits.initialized;
  },

  /**
   * This function wraps nsIBits::init.
   */
  init(jobName, savePathPrefix, monitorTimeoutMs) {
    return gBits.init(jobName, savePathPrefix, monitorTimeoutMs);
  },

  /**
   * This function wraps nsIBits::startDownload.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async startDownload(downloadURL, saveRelPath, proxy, monitorIntervalMs,
                      observer, context) {
    return new Promise((resolve, reject) => {
      let callback = makeInterfaceCallback(resolve, reject);

      try {
        gBits.startDownload(downloadURL, saveRelPath, proxy, monitorIntervalMs,
                            observer, context, callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_START_DOWNLOAD,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  },

  /**
   * This function wraps nsIBits::monitorDownload.
   *
   * Instead of taking a callback, the function is asychronous.
   */
  async monitorDownload(id, monitorIntervalMs, observer, context) {
    return new Promise((resolve, reject) => {
      let callback = makeInterfaceCallback(resolve, reject);

      try {
        gBits.monitorDownload(id, monitorIntervalMs, observer, context,
                              callback);
      } catch (e) {
        let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                  gBits.ERROR_ACTION_MONITOR_DOWNLOAD,
                                  gBits.ERROR_STAGE_PRETASK,
                                  gBits.ERROR_CODE_TYPE_EXCEPTION,
                                  e);
        reject(error);
      }
    });
  },
};

/**
 * The below line of code is unusual, but more maintainable than the
 * alternative.
 * It would not be ideal for consumers of Bits.jsm to need to import Bits.jsm
 * AND nsIBits in order to get access to the wrapped functions and the
 * constants used by them (and it would be a bit syntactically awkward).
 * It also would not be ideal to have a giant block of constants in this file
 * that have to be manually synchronized with the constants in nsIBits.
 *
 * The solution below may have the minor drawback of exposing more than just the
 * constants in BitsConstants, but it solves the above problems and provides
 * a nice looking syntax to callers, as demonstrated here:
 *   await Bits.startDownload(url, path, BitsConstants.PROXY_PRECONFIG,
 *                            interval, observer, context);
 * But please, do not do something ugly with this like BitsConstants.init(...).
 */
let BitsConstants = gBits;

const EXPORTED_SYMBOLS = [
  "Bits", "BitsConstants", "BitsError", "BitsRequest",
];
