// Licensed under the Apache License, Version 2.0
// <LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your option.
// All files in the project carrying such notice may not be copied, modified, or distributed
// except according to those terms.

use std::panic::{catch_unwind, RefUnwindSafe};
use std::sync::Mutex;

use comedy::Error;
use guid_win::Guid;
use winapi::ctypes::c_void;
use winapi::shared::guiddef::REFIID;
use winapi::shared::minwindef::DWORD;
use winapi::shared::ntdef::ULONG;
use winapi::shared::winerror::{E_FAIL, E_NOINTERFACE, HRESULT, NOERROR, S_OK};
use winapi::um::bits::{
    IBackgroundCopyCallback, IBackgroundCopyCallbackVtbl, IBackgroundCopyError, IBackgroundCopyJob,
};
use winapi::um::unknwnbase::{IUnknown, IUnknownVtbl};
use winapi::Interface;

use BitsJob;

/// The type of a notification callback.
///
/// The callbacks must be `Fn()` to be called arbitrarily many times, `RefUnwindSafe` to have a
/// panic unwind safely caught, `Send`, `Sync` and `'static` to run on any thread COM invokes us on
/// any time.
///
/// If the callback returns a non-success `HRESULT`, the notification may pass to other BITS
/// mechanisms such as `IBackgroundCopyJob2::SetNotifyCmdLine`.
pub type TransferredCallback =
    (Fn() -> Result<(), HRESULT>) + RefUnwindSafe + Send + Sync + 'static;
pub type ErrorCallback = (Fn() -> Result<(), HRESULT>) + RefUnwindSafe + Send + Sync + 'static;
pub type ModificationCallback =
    (Fn() -> Result<(), HRESULT>) + RefUnwindSafe + Send + Sync + 'static;

#[repr(C)]
pub struct BackgroundCopyCallback {
    interface: IBackgroundCopyCallback,
    rc: Mutex<ULONG>,
    transferred_cb: Option<Box<TransferredCallback>>,
    error_cb: Option<Box<ErrorCallback>>,
    modification_cb: Option<Box<ModificationCallback>>,
}

impl BackgroundCopyCallback {
    /// Construct the callback object and register it with a job.
    ///
    /// Only one notify interface can be present on a job at once, so this will release BITS'
    /// ref to any previously registered interface.
    pub fn register(
        job: &mut BitsJob,
        transferred_cb: Option<Box<TransferredCallback>>,
        error_cb: Option<Box<ErrorCallback>>,
        modification_cb: Option<Box<ModificationCallback>>,
    ) -> Result<(), Error> {
        let cb = Box::new(BackgroundCopyCallback {
            interface: IBackgroundCopyCallback { lpVtbl: &VTBL },
            rc: Mutex::new(1),
            transferred_cb,
            error_cb,
            modification_cb,
        });

        // Leak the callback, it has no owner until we need to drop it later.
        let cb = Box::leak(cb) as *mut BackgroundCopyCallback as *mut IUnknown;

        unsafe {
            job.set_notify_interface(cb)?;
            (*cb).Release();
        };

        Ok(())
    }
}

extern "system" fn query_interface(
    this: *mut IUnknown,
    riid: REFIID,
    obj: *mut *mut c_void,
) -> HRESULT {
    unsafe {
        if Guid(*riid) == Guid(IUnknown::uuidof())
            || Guid(*riid) == Guid(IBackgroundCopyCallback::uuidof())
        {
            addref(this);
            *obj = this as *mut c_void;
            NOERROR
        } else {
            E_NOINTERFACE
        }
    }
}

extern "system" fn addref(raw_this: *mut IUnknown) -> ULONG {
    unsafe {
        // Forge a ref based on `raw_this`.
        let this = raw_this as *const BackgroundCopyCallback;

        // Justification for unwrap(): `rc` is only locked here and in `release()` below,
        // these can't panic while the `MutexGuard` is in scope, so the `Mutex` will never be
        // poisoned, so `lock()` will never return an error.
        let mut rc = (*this).rc.lock().unwrap();
        *rc += 1;
        *rc
    }
}

extern "system" fn release(raw_this: *mut IUnknown) -> ULONG {
    unsafe {
        {
            let this = raw_this as *const BackgroundCopyCallback;

            let mut rc = (*this).rc.lock().unwrap();
            *rc -= 1;

            if *rc > 0 {
                return *rc;
            }
        };

        // rc will have been 0 for us to get here, and we're out of scope of the uses above, so
        // there should be no references besides `raw_this`. re-Box and to drop immediately.
        let _ = Box::from_raw(raw_this as *mut BackgroundCopyCallback);

        0
    }
}

extern "system" fn transferred_stub(
    raw_this: *mut IBackgroundCopyCallback,
    _job: *mut IBackgroundCopyJob,
) -> HRESULT {
    unsafe {
        let this = raw_this as *const BackgroundCopyCallback;
        if let Some(ref cb) = (*this).transferred_cb {
            match catch_unwind(|| cb()) {
                Ok(Ok(())) => S_OK,
                Ok(Err(hr)) => hr,
                Err(_) => E_FAIL,
            }
        } else {
            S_OK
        }
    }
}

extern "system" fn error_stub(
    raw_this: *mut IBackgroundCopyCallback,
    _job: *mut IBackgroundCopyJob,
    _error: *mut IBackgroundCopyError,
) -> HRESULT {
    unsafe {
        let this = raw_this as *const BackgroundCopyCallback;
        if let Some(ref cb) = (*this).error_cb {
            match catch_unwind(|| cb()) {
                Ok(Ok(())) => S_OK,
                Ok(Err(hr)) => hr,
                Err(_) => E_FAIL,
            }
        } else {
            S_OK
        }
    }
}

extern "system" fn modification_stub(
    raw_this: *mut IBackgroundCopyCallback,
    _job: *mut IBackgroundCopyJob,
    _reserved: DWORD,
) -> HRESULT {
    unsafe {
        let this = raw_this as *const BackgroundCopyCallback;
        if let Some(ref cb) = (*this).modification_cb {
            match catch_unwind(|| cb()) {
                Ok(Ok(())) => S_OK,
                Ok(Err(hr)) => hr,
                Err(_) => E_FAIL,
            }
        } else {
            S_OK
        }
    }
}

pub static VTBL: IBackgroundCopyCallbackVtbl = IBackgroundCopyCallbackVtbl {
    parent: IUnknownVtbl {
        QueryInterface: query_interface,
        AddRef: addref,
        Release: release,
    },
    JobTransferred: transferred_stub,
    JobError: error_stub,
    JobModification: modification_stub,
};
