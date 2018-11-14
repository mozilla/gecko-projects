/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module is used to interact with the Windows BITS component (Background
 * Intelligent Transfer Service). This functionality cannot be used unless on
 * Windows.
 *
 * The reason for this file's existence is that the interfaces in nsIBits.idl
 * are asynchronous, but are unable to use Promises because they are implemented
 * in Rust, which does not yet support Promises. This file functions as a layer
 * between the Rust and the JS that provides access to the functionality
 * provided by nsIBits via Promises rather than callbacks.
 */

"use strict";

const {AppConstants} = ChromeUtils.import("resource://gre/modules/AppConstants.jsm");
const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

// This conditional prevents errors if this file is imported from operating
// systems other than Windows. This is purely for convenience, because
// attempting to use anything in this file on platforms other than Windows will
// result in an error.
if (AppConstants.platform == "win") {
  XPCOMUtils.defineLazyServiceGetter(this, "gBits", "@mozilla.org/bits;1",
                                     "nsIBits");
}

const kBitsMethodTimeoutMs = 10 * 60 * 1000; // 10 minutes

/**
 * This class will wrap the errors returned by the nsIBits interface to make
 * them more uniform and more regular and more easily consumable.
 *
 * The values of stored by this error type are entirely numeric. This is
 * intentional and is meant to allow these values to be more easily passed to
 * telemetry. The values correspond to the constants defined in nsIBits.
 *
 * The type of BitsError.code is dependent on the value of BitsError.codeType.
 * It may be null, a number (corresponding to an nsresult or hresult value),
 * a string, or an exception.
 */
class BitsError extends Error {
  // If codeType == "none", code may be unspecified.
  constructor(type, action, stage, codeType, code) {
    let message = `${BitsError.name} {type: ${type}, action: ${action}, ` +
                  `stage: ${stage}`;
    switch (codeType) {
      case gBits.ERROR_CODE_TYPE_NONE:
        code = null;
        message += ", codeType: none}";
        break;
      case gBits.ERROR_CODE_TYPE_NSRESULT:
        message += `, codeType: nsresult, code: ${code}}`;
        break;
      case gBits.ERROR_CODE_TYPE_HRESULT:
        message += `, codeType: hresult, code: ${code}}`;
        break;
      case gBits.ERROR_CODE_TYPE_STRING:
        message += `, codeType: string, code: ${JSON.stringify(code)}}`;
        break;
      case gBits.ERROR_CODE_TYPE_EXCEPTION:
        message += `, codeType: exception, code: ${code}}`;
        break;
      default:
        message += ", codeType: invalid}";
        break;
    }
    super(message);

    this.type = type;
    this.action = action;
    this.stage = stage;
    this.codeType = codeType;
    this.code = code;
    this.name = this.constructor.name;
  }
}

/**
 * Returns a timer object. If the timer expires, reject will be called with
 * a BitsError error. The timer's cancel method should be called if the promise
 * resolves or rejects without the timeout expiring.
 */
function makeTimeout(reject, error_action) {
  let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(() => {
    let error = new BitsError(gBits.ERROR_TYPE_METHOD_TIMEOUT,
                              error_action,
                              gBits.ERROR_STAGE_UNKNOWN,
                              gBits.ERROR_CODE_TYPE_NONE);
    reject(error);
  }, kBitsMethodTimeoutMs, Ci.nsITimer.TYPE_ONE_SHOT);
  return timer;
}

/**
 * This function does all of the wrapping and error handling for a BitsRequest
 * function. This allows the implementations for those functions to simply call
 * this with a closure that executes appropriate nsIBitsRequest method.
 */
async function requestPromise(error_action, action_fn) {
  return new Promise((resolve, reject) => {
    let timer = makeTimeout(reject, error_action);

    let callback = {
      QueryInterface: ChromeUtils.generateQI([Ci.nsIBitsCallback]),
      success() {
        timer.cancel();
        resolve();
      },
      failure(type, action, stage) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_NONE);
        reject(error);
      },
      failureNsresult(type, action, stage, code) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_NSRESULT, code);
        reject(error);
      },
      failureHresult(type, action, stage, code) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_HRESULT, code);
        reject(error);
      },
      failureString(type, action, stage, message) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_STRING, message);
        reject(error);
      },
    };

    try {
      action_fn(callback);
    } catch (e) {
      let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                error_action,
                                gBits.ERROR_STAGE_PRETASK,
                                gBits.ERROR_CODE_TYPE_EXCEPTION,
                                e);
      reject(error);
    }
  });
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
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   */
  async changeMonitorInterval(monitorIntervalMs) {
    let action = gBits.ERROR_ACTION_CHANGE_MONITOR_INTERVAL;
    return requestPromise(action, callback => {
      this._request.changeMonitorInterval(monitorIntervalMs, callback);
    });
  }

  /**
   * This function wraps nsIBitsRequest::cancelAsync.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   *
   * Adds a default status of NS_ERROR_ABORT if one is not provided.
   */
  async cancelAsync(status) {
    if (status === undefined) {
      status = Cr.NS_ERROR_ABORT;
    }
    let action = gBits.ERROR_ACTION_CANCEL;
    return requestPromise(action, callback => {
      this._request.cancelAsync(status, callback);
    });
  }

  /**
   * This function wraps nsIBitsRequest::setPriorityHigh.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   */
  async setPriorityHigh() {
    let action = gBits.ERROR_ACTION_SET_PRIORITY;
    return requestPromise(action, callback => {
      this._request.setPriorityHigh(callback);
    });
  }

  /**
   * This function wraps nsIBitsRequest::setPriorityLow.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   */
  async setPriorityLow() {
    let action = gBits.ERROR_ACTION_SET_PRIORITY;
    return requestPromise(action, callback => {
      this._request.setPriorityLow(callback);
    });
  }

  /**
   * This function wraps nsIBitsRequest::complete.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   */
  async complete() {
    let action = gBits.ERROR_ACTION_COMPLETE;
    return requestPromise(action, callback => {
      this._request.complete(callback);
    });
  }

  /**
   * This function wraps nsIBitsRequest::suspendAsync.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   */
  async suspendAsync() {
    let action = gBits.ERROR_ACTION_SUSPEND;
    return requestPromise(action, callback => {
      this._request.suspendAsync(callback);
    });
  }

  /**
   * This function wraps nsIBitsRequest::resumeAsync.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with no data, or rejects with a BitsError.
   */
  async resumeAsync() {
    let action = gBits.ERROR_ACTION_RESUME;
    return requestPromise(action, callback => {
      this._request.resumeAsync(callback);
    });
  }
}
BitsRequest.prototype.QueryInterface = ChromeUtils.generateQI([Ci.nsIRequest]);

/**
 * This function does all of the wrapping and error handling for a Bits
 * Interface function. This allows the implementations for those functions to
 * simply call this with a closure that executes appropriate nsIBits method.
 */
async function interfacePromise(error_action, observer, action_fn) {
  return new Promise((resolve, reject) => {
    if (!observer) {
      let error = new BitsError(gBits.ERROR_TYPE_NULL_ARGUMENT,
                                error_action,
                                gBits.ERROR_STAGE_PRETASK,
                                gBits.ERROR_CODE_TYPE_NONE);
      reject(error);
      return;
    }
    try {
      observer.QueryInterface(Ci.nsIRequestObserver);
    } catch (e) {
      let error = new BitsError(gBits.ERROR_TYPE_INVALID_ARGUMENT,
                                error_action,
                                gBits.ERROR_STAGE_PRETASK,
                                gBits.ERROR_CODE_TYPE_EXCEPTION,
                                e);
      reject(error);
      return;
    }
    let isProgressEventSink = false;
    try {
      observer.QueryInterface(Ci.nsIProgressEventSink);
      isProgressEventSink = true;
    } catch (e) {}

    let wrappedObserver = {
      onStartRequest: function wrappedObserver_onStartRequest(request) {
        observer.onStartRequest(new BitsRequest(request));
      },
      onStopRequest: function wrappedObserver_onStopRequest(request, status) {
        observer.onStopRequest(new BitsRequest(request), status);
      },
      onProgress: function wrappedObserver_onProgress(request, context,
                                                      progress, progressMax) {
        if (isProgressEventSink) {
          observer.onProgress(new BitsRequest(request), context, progress,
                              progressMax);
        }
      },
      onStatus: function wrappedObserver_onStatus(request, context, status,
                                                  statusArg) {
        if (isProgressEventSink) {
          observer.onStatus(new BitsRequest(request), context, status,
                            statusArg);
        }
      },
      QueryInterface: ChromeUtils.generateQI([Ci.nsIRequestObserver,
                                              Ci.nsIProgressEventSink]),
    };

    let timer = makeTimeout(reject, error_action);
    let callback = {
      QueryInterface: ChromeUtils.generateQI([Ci.nsIBitsNewRequestCallback]),
      success(request) {
        timer.cancel();
        resolve(new BitsRequest(request));
      },
      failure(type, action, stage) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_NONE);
        reject(error);
      },
      failureNsresult(type, action, stage, code) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_NSRESULT, code);
        reject(error);
      },
      failureHresult(type, action, stage, code) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_HRESULT, code);
        reject(error);
      },
      failureString(type, action, stage, message) {
        timer.cancel();
        let error = new BitsError(type, action, stage,
                                  gBits.ERROR_CODE_TYPE_STRING, message);
        reject(error);
      },
    };

    try {
      action_fn(wrappedObserver, callback);
    } catch (e) {
      let error = new BitsError(gBits.ERROR_TYPE_METHOD_THREW,
                                error_action,
                                gBits.ERROR_STAGE_PRETASK,
                                gBits.ERROR_CODE_TYPE_EXCEPTION,
                                e);
      reject(error);
    }
  });
}

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
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with a BitsRequest (which is also an
   * nsIRequest), or rejects with a BitsError.
   */
  async startDownload(downloadURL, saveRelPath, proxy, monitorIntervalMs,
                      observer, context) {
    let action = gBits.ERROR_ACTION_START_DOWNLOAD;
    return interfacePromise(action, observer, (wrappedObserver, callback) => {
      gBits.startDownload(downloadURL, saveRelPath, proxy, monitorIntervalMs,
                          wrappedObserver, context, callback);
    });
  },

  /**
   * This function wraps nsIBits::monitorDownload.
   *
   * Instead of taking a callback, the function is asynchronous.
   * This method either resolves with a BitsRequest (which is also an
   * nsIRequest), or rejects with a BitsError.
   */
  async monitorDownload(id, monitorIntervalMs, observer, context) {
    let action = gBits.ERROR_ACTION_MONITOR_DOWNLOAD;
    return interfacePromise(action, observer, (wrappedObserver, callback) => {
      gBits.monitorDownload(id, monitorIntervalMs, wrappedObserver, context,
                            callback);
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
var BitsConstants = null;
if (AppConstants.platform == "win") {
  BitsConstants = gBits;
}

const EXPORTED_SYMBOLS = [
  "Bits", "BitsConstants", "BitsError", "BitsRequest",
];
