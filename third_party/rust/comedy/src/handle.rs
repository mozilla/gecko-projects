// Licensed under the Apache License, Version 2.0
// <LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your option.
// All files in the project carrying such notice may not be copied, modified, or distributed
// except according to those terms.

//! Wrapping and automatically closing handles.

use std::ops::Deref;

use winapi::shared::minwindef::{DWORD, HLOCAL, LPVOID};
use winapi::shared::ntdef::NULL;
use winapi::um::combaseapi::CoTaskMemFree;
use winapi::um::errhandlingapi::GetLastError;
use winapi::um::handleapi::{CloseHandle, INVALID_HANDLE_VALUE};
use winapi::um::winbase::LocalFree;
use winapi::um::winnt::HANDLE;

/// Check and automatically close a Windows `HANDLE`.
#[repr(transparent)]
#[derive(Debug)]
pub struct Handle(HANDLE);

impl Handle {
    /// Take ownership of a `HANDLE`, which will be closed with `CloseHandle` upon drop.
    /// Checks for `INVALID_HANDLE_VALUE` but not `NULL`.
    ///
    /// # Safety
    ///
    /// `h` should be the only copy of the handle. `GetLastError()` is called to
    /// return an error, so the last Windows API called should have been what produced
    /// the invalid handle.
    pub unsafe fn wrap_valid(h: HANDLE) -> Result<Handle, DWORD> {
        if h == INVALID_HANDLE_VALUE {
            Err(GetLastError())
        } else {
            Ok(Handle(h))
        }
    }

    /// Take ownership of a `HANDLE`, which will be closed with `CloseHandle` upon drop.
    /// Checks for `NULL` but not `INVALID_HANDLE_VALUE`.
    ///
    /// # Safety
    ///
    /// `h` should be the only copy of the handle. `GetLastError()` is called to
    /// return an error, so the last Windows API called should have been what produced
    /// the invalid handle.
    pub unsafe fn wrap_nonnull(h: HANDLE) -> Result<Handle, DWORD> {
        if h == NULL {
            Err(GetLastError())
        } else {
            Ok(Handle(h))
        }
    }
}

impl Deref for Handle {
    type Target = HANDLE;
    fn deref(&self) -> &HANDLE {
        &self.0
    }
}

impl Drop for Handle {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

/// Call a function that returns a `HANDLE`, but `INVALID_HANDLE_VALUE` on failure, wrap result.
///
/// The handle is wrapped in a [`Handle`](handle/struct.Handle.html) which will automatically call
/// `CloseHandle()` on it. If the function fails, the error is retrieved via `GetLastError()` and
/// augmented with the name of the function and the file and line number of the macro usage.
///
/// # Example
///
/// ```ignore
/// unsafe {
///     let handle = call_valid_handle_getter!(
///         FindFirstFileA(std::ptr::null_mut(), std::ptr::null_mut())
///     )?;
/// }
/// ```
#[macro_export]
macro_rules! call_valid_handle_getter {
    ($f:ident ( $($arg:expr),* )) => {
        {
            use $crate::error::{Error, ErrorCode, FileLine};
            $crate::handle::Handle::wrap_valid($f($($arg),*))
                .map_err(|last_error| Win32Error {
                    code: last_error,
                    function: Some(stringify!($f)),
                    file_line: Some(FileLine(file!(), line!())),
                })
        }
    };

    // support for trailing comma in argument list
    ($f:ident ( $($arg:expr),+ , )) => {
        $crate::call_valid_handle_getter!($f($($arg),*))
    };
}

/// Call a function that returns a `HANDLE`, but `NULL` on failure, wrap result.
///
/// The handle is wrapped in a [`Handle`](handle/struct.Handle.html) which will automatically call
/// `CloseHandle()` on it. If the function fails, the error is retrieved via `GetLastError()` and
/// augmented with the name of the function and the file and line number of the macro usage.
///
/// # Example
///
/// ```ignore
/// unsafe {
///     let handle = call_nonnull_handle_getter!(
///         CreateEventA(
///             std::ptr::null_mut(),
///             0, 0,
///             std::ptr::null_mut(),
///         )
///     )?;
/// }
/// ```
#[macro_export]
macro_rules! call_nonnull_handle_getter {
    ($f:ident ( $($arg:expr),* )) => {
        {
            use $crate::error::{Error, ErrorCode, FileLine};
            $crate::handle::Handle::wrap_nonnull($f($($arg),*))
                .map_err(|last_error| Win32Error {
                    code: last_error,
                    function: Some(stringify!($f)),
                    file_line: Some(FileLine(file!(), line!())),
                })
        }
    };

    // support for trailing comma in argument list
    ($f:ident ( $($arg:expr),+ , )) => {
        $crate::call_nonnull_handle_getter!($f($($arg),*))
    };
}

/// Check and automatically free a Windows `HLOCAL`.
#[repr(transparent)]
#[derive(Debug)]
pub struct HLocal(HLOCAL);

impl HLocal {
    /// Take ownership of a `HLOCAL`, which will be closed with `LocalFree` upon drop.
    /// Checks for `NULL`.
    ///
    /// # Safety
    ///
    /// `h` should be the only copy of the handle. `GetLastError()` is called to
    /// return an error, so the last Windows API called should have been what produced
    /// the invalid handle.
    pub unsafe fn wrap(h: HLOCAL) -> Result<HLocal, DWORD> {
        if h == NULL {
            Err(GetLastError())
        } else {
            Ok(HLocal(h))
        }
    }
}

impl Deref for HLocal {
    type Target = HLOCAL;
    fn deref(&self) -> &HLOCAL {
        &self.0
    }
}

impl Drop for HLocal {
    fn drop(&mut self) {
        unsafe {
            LocalFree(self.0);
        }
    }
}

/// Check and automatically free a Windows COM task memory pointer.
#[repr(transparent)]
#[derive(Debug)]
pub struct CoTaskMem(LPVOID);

impl CoTaskMem {
    /// Take ownership of COM task memory, which will be closed with `CoTaskMemFree()` upon drop.
    /// Checks for `NULL`.
    ///
    /// # Safety
    ///
    /// `p` should be the only copy of the pointer.
    pub unsafe fn wrap(p: LPVOID) -> Result<CoTaskMem, ()> {
        if p == NULL {
            Err(())
        } else {
            Ok(CoTaskMem(p))
        }
    }
}

impl Deref for CoTaskMem {
    type Target = LPVOID;
    fn deref(&self) -> &LPVOID {
        &self.0
    }
}

impl Drop for CoTaskMem {
    fn drop(&mut self) {
        unsafe {
            CoTaskMemFree(self.0);
        }
    }
}
