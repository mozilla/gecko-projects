/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::{
    error::{BitsTaskError, ErrorCode, ErrorType},
    BitsRequest,
};
use log::{error, info, warn};
use nserror::{nsresult, NS_ERROR_FAILURE, NS_OK};
use xpcom::{interfaces::{nsIBitsCallback, nsIBitsNewRequestCallback}, RefPtr};

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum IsCallbackExpected {
    CallbackExpected,
    CallbackOptional,
}
pub use self::IsCallbackExpected::{CallbackExpected, CallbackOptional};

pub fn dispatch_pretask_interface_error(
    error: BitsTaskError,
    callback: &nsIBitsNewRequestCallback,
) {
    let _ = dispatch_request_via_callback(Err(error), callback);
}

pub fn dispatch_request_via_callback(
    result: Result<RefPtr<BitsRequest>, BitsTaskError>,
    callback: &nsIBitsNewRequestCallback,
) -> Result<(), nsresult> {
    maybe_dispatch_request_via_callback(result, Ok(callback), CallbackExpected)
}

pub fn maybe_dispatch_request_via_callback(
    result: Result<RefPtr<BitsRequest>, BitsTaskError>,
    maybe_callback: Result<&nsIBitsNewRequestCallback, BitsTaskError>,
    expected: IsCallbackExpected,
) -> Result<(), nsresult> {
    if let Err(error) = maybe_callback.as_ref() {
        if expected == CallbackExpected || error.error_type == ErrorType::CallbackOnWrongThread {
            error!(
                "Unexpected error when {} - No callback: {:?}",
                error.error_action.description(),
                error,
            );
        }
    }
    match result {
        Ok(request) => match maybe_callback {
            Ok(callback) => unsafe { callback.Success(request.coerce()) },
            Err(error) => match expected {
                CallbackExpected => {
                    error!(
                        "Success {} but there is no callback to return the result with",
                        error.error_action.description(),
                    );
                    NS_ERROR_FAILURE
                }
                CallbackOptional => {
                    info!("Success {}", error.error_action.description());
                    NS_OK
                }
            },
        },
        Err(error) => match maybe_callback {
            Ok(callback) => match error.error_code {
                ErrorCode::None => unsafe {
                    callback.Failure(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                    )
                },
                ErrorCode::Hresult(error_code) => unsafe {
                    callback.FailureHresult(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                        error_code,
                    )
                },
                ErrorCode::Nsresult(error_code) => unsafe {
                    callback.FailureNsresult(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                        error_code,
                    )
                },
                ErrorCode::Message(message) => unsafe {
                    callback.FailureString(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                        &*message,
                    )
                },
            },
            Err(_) => match expected {
                CallbackExpected => {
                    error!("Error {}: {:?}", error.error_action.description(), error);
                    NS_ERROR_FAILURE
                }
                CallbackOptional => {
                    warn!("Error {}: {:?}", error.error_action.description(), error);
                    NS_ERROR_FAILURE
                }
            },
        },
    }
    .to_result()
}

pub fn dispatch_pretask_request_error(
    error: BitsTaskError,
    callback: &nsIBitsCallback,
) {
    let _ = dispatch_via_callback(Err(error), callback);
}

pub fn dispatch_via_callback(
    result: Result<(), BitsTaskError>,
    callback: &nsIBitsCallback,
) -> Result<(), nsresult> {
    maybe_dispatch_via_callback(result, Ok(callback), CallbackExpected)
}

pub fn maybe_dispatch_via_callback(
    result: Result<(), BitsTaskError>,
    maybe_callback: Result<&nsIBitsCallback, BitsTaskError>,
    expected: IsCallbackExpected,
) -> Result<(), nsresult> {
    if let Err(error) = maybe_callback.as_ref() {
        if expected == CallbackExpected || error.error_type == ErrorType::CallbackOnWrongThread {
            error!(
                "Unexpected error when {} - No callback: {:?}",
                error.error_action.description(),
                error,
            );
        }
    }
    match result {
        Ok(()) => match maybe_callback {
            Ok(callback) => unsafe { callback.Success() },
            Err(error) => match expected {
                CallbackExpected => {
                    error!(
                        "Success {} but there is no callback to return the result with",
                        error.error_action.description(),
                    );
                    NS_ERROR_FAILURE
                }
                CallbackOptional => {
                    info!("Success {}", error.error_action.description());
                    NS_OK
                }
            },
        },
        Err(error) => match maybe_callback {
            Ok(callback) => match error.error_code {
                ErrorCode::None => unsafe {
                    callback.Failure(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                    )
                },
                ErrorCode::Hresult(error_code) => unsafe {
                    callback.FailureHresult(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                        error_code,
                    )
                },
                ErrorCode::Nsresult(error_code) => unsafe {
                    callback.FailureNsresult(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                        error_code,
                    )
                },
                ErrorCode::Message(message) => unsafe {
                    callback.FailureString(
                        error.error_type.bits_code(),
                        error.error_action.as_error_code(),
                        error.error_stage.bits_code(),
                        &*message,
                    )
                },
            },
            Err(_) => match expected {
                CallbackExpected => {
                    error!("Error {}: {:?}", error.error_action.description(), error);
                    NS_ERROR_FAILURE
                }
                CallbackOptional => {
                    warn!("Error {}: {:?}", error.error_action.description(), error);
                    NS_ERROR_FAILURE
                }
            },
        },
    }
    .to_result()
}
