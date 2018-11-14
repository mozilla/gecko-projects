/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use bits_client::PipeError;
use comedy;
use failure::Fail;
use nserror::nsresult;
use std::convert::From;
use std::str::Utf8Error;
use std::string::FromUtf8Error;

// This wrapper is needed in order to implement Fail for Utf8Error
#[derive(Clone, Debug, Fail)]
#[fail(display = "{}", err)]
pub struct Utf8ErrorWrapper {
    err: Utf8Error,
}

#[derive(Clone, Debug, Fail)]
pub enum BITSTaskError {
    #[fail(display = "BITS Client Error")]
    ClientError(#[fail(cause)] PipeError),
    #[fail(display = "Invalid UTF8")]
    StringError(#[fail(cause)] Utf8ErrorWrapper),
    #[fail(display = "{}: {}", _0, _1)]
    NsError(String, nsresult),
    #[fail(display = "{}", _0)]
    Error(String),
}

impl From<PipeError> for BITSTaskError {
    fn from(err: PipeError) -> BITSTaskError {
        BITSTaskError::ClientError(err)
    }
}

impl From<FromUtf8Error> for BITSTaskError {
    fn from(err: FromUtf8Error) -> BITSTaskError {
        BITSTaskError::StringError(Utf8ErrorWrapper {
            err: err.utf8_error(),
        })
    }
}

impl From<comedy::Error> for BITSTaskError {
    fn from(err: comedy::Error) -> BITSTaskError {
        BITSTaskError::from(PipeError::from(err))
    }
}

impl BITSTaskError {
    pub fn with_message(message: &str) -> BITSTaskError {
        BITSTaskError::Error(String::from(message))
    }

    pub fn with_nsresult(message: &str, rv: nsresult) -> BITSTaskError {
        BITSTaskError::NsError(String::from(message), rv)
    }
}
