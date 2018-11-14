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
// systems other than Windows. This is purely for convenient importing, because
// attempting to use anything in this file on platforms other than Windows will
// result in an error.
if (AppConstants.platform == "win") {
  XPCOMUtils.defineLazyServiceGetter(this, "gBits", "@mozilla.org/bits;1",
                                     "nsIBits");
}

// This value exists to mitigate a very unlikely problem: If a BITS method
// catastrophically fails, it may never call its callback. This would result in
// methods in this file returning promises that never resolve. This could, in
// turn, lead to download code hanging altogether rather than being able to
// report errors and utilize fallback mechanisms.
// This problem is mitigated by giving these promises a timeout, the length of
// which will be determined by this value.
const kBitsMethodTimeoutMs = 10 * 60 * 1000; // 10 minutes

/**
 * This class will wrap the errors returned by the nsIBits interface to make
 * them more uniform and more easily consumable.
 *
 * The values of stored by this error type are entirely numeric. This should
 * make them easier to consume with JS and telemetry, but does make them fairly
 * unreadable. nsIBits.idl will need to be referenced to look up what errors
 * the values correspond to.
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
    this.succeeded = false;
  }
}

// These specializations exist to make them easier to construct since they may
// need to be constructed outside of this file.
class BitsVerificationError extends BitsError {
  constructor() {
    super(Ci.nsIBits.ERROR_TYPE_VERIFICATION_FAILURE,
          Ci.nsIBits.ERROR_ACTION_NONE,
          Ci.nsIBits.ERROR_STAGE_VERIFICATION,
          Ci.nsIBits.ERROR_CODE_TYPE_NONE);
  }
}
class BitsUnknownError extends BitsError {
  constructor() {
    super(Ci.nsIBits.ERROR_TYPE_UNKNOWN,
          Ci.nsIBits.ERROR_ACTION_UNKNOWN,
          Ci.nsIBits.ERROR_STAGE_UNKNOWN,
          Ci.nsIBits.ERROR_CODE_TYPE_NONE);
  }
}

/**
 * This is something of a counterpart to BitsError that allows a result to be
 * returned as success or failure. Consumers can check for success or failure
 * using instanceof or via result.succeeded (which will be true for BitsSuccess
 * and false for BitsError).
 * Consumers can also use result.type without worrying about whether they got a
 * BitsSuccess or a BitsError.
 */
class BitsSuccess {
  constructor() {
    this.type = Ci.nsIBits.ERROR_TYPE_SUCCESS;
    this.succeeded = true;
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
 * This function does all of the wrapping and error handling for an async
 * BitsRequest method. This allows the implementations for those methods to
 * simply call this function with a closure that executes appropriate
 * nsIBitsRequest method.
 *
 * Specifically, this function takes an nsBitsErrorAction and a function.
 * The nsBitsErrorAction will be used when constructing a BitsError, if the
 * wrapper encounters an error.
 * The function will be passed the callback function that should be passed to
 * the nsIBitsRequest method.
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
   * This function wraps nsIBitsRequest::bitsTransferResult.
   *
   * Instead of simply returning the nsBitsErrorType value, however, it returns
   * a BitsError or BitsSuccess object.
   */
  get bitsTransferResult() {
    let result = this._request.bitsTransferResult;
    if (result == Ci.nsIBits.ERROR_TYPE_SUCCESS) {
      return new BitsSuccess();
    }
    return new BitsError(result,
                         Ci.nsIBits.ERROR_ACTION_NONE,
                         Ci.nsIBits.ERROR_STAGE_MONITOR,
                         Ci.nsIBits.ERROR_CODE_TYPE_NONE);
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

/**
 * This function does all of the wrapping and error handling for an async
 * Bits Interface method. This allows the implementations for those methods to
 * simply call this function with a closure that executes appropriate
 * nsIBits method.
 *
 * Specifically, this function takes an nsBitsErrorAction, an observer and a
 * function.
 * The nsBitsErrorAction will be used when constructing a BitsError, if the
 * wrapper encounters an error.
 * The observer should be the one that the caller passed to the Bits Interface
 * method. It will be wrapped so that its methods are passed a BitsRequest
 * rather than an nsIBitsRequest.
 * The function will be passed the callback function and the wrapped observer,
 * both of which should be passed to the nsIBitsRequest method.
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

var BitsConstants = null;
if (AppConstants.platform == "win") {
  // This should make all of the error codes, proxy values and any other
  // constants accessible from this variable without requiring that the entire
  // list of constants be duplicated here.
  // The original list of constants can be found in nsIBits.idl
  BitsConstants = Ci.nsIBits;
}

const EXPORTED_SYMBOLS = [
  "Bits", "BitsConstants", "BitsError", "BitsRequest", "BitsSuccess",
  "BitsUnknownError", "BitsVerificationError",
];
