// Licensed under the Apache License, Version 2.0
// <LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your option.
// All files in the project carrying such notice may not be copied, modified, or distributed
// except according to those terms.

//! Wrap several flavors of Windows error into a `Result`.

use std::fmt;
use std::ptr::NonNull;
use std::result;

use failure::Fail;

use winapi::shared::minwindef::DWORD;
use winapi::shared::winerror::{HRESULT, SUCCEEDED};
use winapi::um::errhandlingapi::GetLastError;

/// An error with optional error code, function name, source file name and line number.
#[derive(Clone, Debug, Default, Eq, Fail, PartialEq)]
pub struct Error {
    /// The error code returned by the function.
    pub code: Option<ErrorCode>,
    /// The name of the function that failed.
    pub function: Option<&'static str>,
    /// The file and line of the failing function call.
    pub file_line: Option<FileLine>,
}

/// The error code associated with the error.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ErrorCode {
    /// A null pointer where non-null was expected.
    NullPtr,
    /// A (usually non-zero) return code.
    Rc(DWORD),
    /// `GetLastError` after the error.
    LastError(DWORD),
    /// A (usually failing) `HRESULT`.
    HResult(HRESULT),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FileLine(pub &'static str, pub u32);

use self::ErrorCode::*;

impl Error {
    /// Add the name of the failing function to the error.
    pub fn function(self, function: &'static str) -> Error {
        Error {
            function: Some(function),
            ..self
        }
    }

    /// Add the source file name and line number of the call to the error.
    pub fn file_line(self, file: &'static str, line: u32) -> Error {
        Error {
            file_line: Some(FileLine(file, line)),
            ..self
        }
    }

    /// Was the error a null pointer?
    pub fn is_nullptr(&self) -> bool {
        if let Some(NullPtr) = self.code {
            true
        } else {
            false
        }
    }

    /// The return code of the error, if there is one.
    pub fn get_rc(&self) -> Option<DWORD> {
        if let Some(Rc(rc)) = self.code {
            Some(rc)
        } else {
            None
        }
    }

    /// The value of `GetLastError` from the error, if known.
    pub fn get_last_error(&self) -> Option<DWORD> {
        if let Some(LastError(last_err)) = self.code {
            Some(last_err)
        } else {
            None
        }
    }

    /// The `HRESULT` from the error, if there is one.
    pub fn get_hresult(&self) -> Option<HRESULT> {
        if let Some(HResult(hr)) = self.code {
            Some(hr)
        } else {
            None
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> result::Result<(), fmt::Error> {
        if self.function.is_none() && self.code.is_none() {
            if let Some(FileLine(file, line)) = self.file_line {
                write!(f, "Failure at {}:{}", file, line)?;
            } else {
                write!(f, "Error")?;
            }

            return Ok(());
        }

        if let Some(function) = self.function {
            if let Some(FileLine(file, line)) = self.file_line {
                write!(f, "{}:{} ", file, line)?;
            }

            write!(f, "{} ", function)?;

            write!(f, "failed.")?;
        }

        if self.function.is_some() && self.code.is_some() {
            write!(f, " ")?;
        }

        if let Some(ref ec) = self.code {
            match ec {
                NullPtr => write!(f, "null pointer")?,
                Rc(rc) => write!(f, "rc = {:#010x}", rc)?,
                LastError(rc) => write!(f, "GetLastError = {:#010x}", rc)?,
                HResult(hr) => write!(f, "hr = {:#010x}", hr)?,
            };
        }

        Ok(())
    }
}

pub type Result<T> = result::Result<T, Error>;

/// Trait for adding information to a `Result<T, Error>`.
pub trait ResultExt<T> {
    /// Add the name of the failing function to the error.
    fn function(self, function: &'static str) -> Result<T>;

    /// Add the source file name and line number of the call to the error.
    fn file_line(self, file: &'static str, line: u32) -> Result<T>;

    /// Replace `Err(code)` with `Ok(replacement)`.
    fn allow_err(self, code: ErrorCode, replacement: T) -> Result<T>;

    /// Replace `Err(NullPtr)` with `Ok(replacement)`.
    fn allow_nullptr(self, replacement: T) -> Result<T>;

    /// Replace `Err(Rc(rc))` with `Ok(replacement)`.
    fn allow_rc(self, rc: DWORD, replacement: T) -> Result<T>;

    /// Replace `Err(LastError(last_err))` with `Ok(replacement)`.
    fn allow_last_error(self, last_err: DWORD, replacement: T) -> Result<T>;

    /// Replace `Err(HResult(hr))` with `Ok(replacement)`.
    fn allow_hresult(self, hr: HRESULT, replacement: T) -> Result<T>;

    /// Replace `Err(code)` with the result of calling `replacement()`.
    fn allow_err_with<F>(self, code: ErrorCode, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T;

    /// Replace `Err(NullPtr)` with the result of calling `replacement()`.
    fn allow_nullptr_with<F>(self, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T;

    /// Replace `Err(Rc(rc))` with the result of calling `replacement()`.
    fn allow_rc_with<F>(self, rc: DWORD, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T;

    /// Replace `Err(LastError(last_err))` with the result of calling `replacement()`.
    fn allow_last_error_with<F>(self, last_err: DWORD, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T;

    /// Replace `Err(HResult(hr))` with the result of calling `replacement()`.
    fn allow_hresult_with<F>(self, hr: HRESULT, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T;
}

impl<T> ResultExt<T> for Result<T> {
    fn function(self, function: &'static str) -> Result<T> {
        self.map_err(|e| e.function(function))
    }

    fn file_line(self, file: &'static str, line: u32) -> Result<T> {
        self.map_err(|e| e.file_line(file, line))
    }

    fn allow_err(self, code: ErrorCode, replacement: T) -> Result<T> {
        match self {
            Ok(r) => Ok(r),
            Err(ref e) if e.code == Some(code) => Ok(replacement),
            Err(e) => Err(e),
        }
    }

    fn allow_nullptr(self, replacement: T) -> Result<T> {
        self.allow_err(NullPtr, replacement)
    }

    fn allow_rc(self, rc: DWORD, replacement: T) -> Result<T> {
        self.allow_err(Rc(rc), replacement)
    }

    fn allow_last_error(self, last_err: DWORD, replacement: T) -> Result<T> {
        self.allow_err(LastError(last_err), replacement)
    }

    fn allow_hresult(self, hr: HRESULT, replacement: T) -> Result<T> {
        self.allow_err(HResult(hr), replacement)
    }

    fn allow_err_with<F>(self, code: ErrorCode, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T,
    {
        match self {
            Ok(r) => Ok(r),
            Err(ref e) if e.code == Some(code) => Ok(replacement()),
            Err(e) => Err(e),
        }
    }

    fn allow_nullptr_with<F>(self, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T,
    {
        self.allow_err_with(NullPtr, replacement)
    }

    fn allow_rc_with<F>(self, rc: DWORD, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T,
    {
        self.allow_err_with(Rc(rc), replacement)
    }

    fn allow_last_error_with<F>(self, last_err: DWORD, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T,
    {
        self.allow_err_with(LastError(last_err), replacement)
    }

    fn allow_hresult_with<F>(self, hr: HRESULT, replacement: F) -> Result<T>
    where
        F: FnOnce() -> T,
    {
        self.allow_err_with(HResult(hr), replacement)
    }
}

/// Convert an `HRESULT` into a `Result`.
pub fn succeeded_or_err(hr: HRESULT) -> Result<HRESULT> {
    if !SUCCEEDED(hr) {
        Err(Error {
            code: Some(HResult(hr)),
            function: None,
            file_line: None,
        })
    } else {
        Ok(hr)
    }
}

/// Call a function that returns an `HRESULT`, convert to a `Result<HRESULT>`.
///
/// The error will be augmented with the name of the function and the file and line number of
/// the macro usage.
///
/// # Example
/// ```ignore
/// unsafe {
///     check_succeeded!(
///         CoInitialize(std::ptr::null_mut())
///     )?;
/// }
/// ```
#[macro_export]
macro_rules! check_succeeded {
    ($f:ident ( $($arg:expr),* )) => {
        {
            use $crate::error::ResultExt;
            $crate::error::succeeded_or_err($f($($arg),*))
                .function(stringify!($f))
                .file_line(file!(), line!())
        }
    };

    // support for trailing comma in argument list
    ($f:ident ( $($arg:expr),+ , )) => {
        $crate::check_succeeded!($f($($arg),+))
    };
}

/// Convert an integer return value into a `Result`, using `GetLastError()` if zero.
pub fn true_or_last_err<T>(rv: T) -> Result<T>
where
    T: Eq,
    T: From<bool>,
{
    if rv == T::from(false) {
        Err(Error {
            code: Some(LastError(unsafe { GetLastError() })),
            function: None,
            file_line: None,
        })
    } else {
        Ok(rv)
    }
}

/// Call a function that returns a integer, conver to a `Result`, using `GetLastError()` if zero.
///
/// The error will be augmented with the name of the function and the file and line number of
/// the macro usage.
///
/// # Example
/// ```ignore
/// unsafe {
///     check_true!(
///         FlushFileBuffers(file)
///     )?;
/// }
/// ```
#[macro_export]
macro_rules! check_true {
    ($f:ident ( $($arg:expr),* )) => {
        {
            use $crate::error::ResultExt;
            $crate::error::true_or_last_err($f($($arg),*))
                .function(stringify!($f))
                .file_line(file!(), line!())
        }
    };

    // support for trailing comma in argument list
    ($f:ident ( $($arg:expr),+ , )) => {
        $crate::check_true!($f($($arg),+))
    };
}

/// Convert a pointer `rv` into a `Result<NonNull>`, using `GetLastError()` if null.
pub fn nonnull_or_last_err<T>(p: *mut T) -> Result<NonNull<T>> {
    match NonNull::new(p) {
        None => Err(Error {
            code: Some(LastError(unsafe { GetLastError() })),
            function: None,
            file_line: None,
        }),
        Some(p) => Ok(p),
    }
}
