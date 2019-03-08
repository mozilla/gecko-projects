// Licensed under the Apache License, Version 2.0
// <LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your option.
// All files in the project carrying such notice may not be copied, modified, or distributed
// except according to those terms.

//! Utilities and wrappers for Microsoft COM interfaces in Windows.
//!
//! This works with the `Class` and `Interface` traits from the `winapi` crate.

use std::marker::PhantomData;
use std::mem;
use std::ops::Deref;
use std::ptr::{self, null_mut, NonNull};
use std::rc::Rc;

use winapi::shared::minwindef::LPVOID;
use winapi::shared::{
    winerror::HRESULT,
    wtypesbase::{CLSCTX, CLSCTX_INPROC_SERVER, CLSCTX_LOCAL_SERVER},
};
use winapi::um::{
    combaseapi::{CoCreateInstance, CoInitializeEx, CoUninitialize},
    objbase::{COINIT_APARTMENTTHREADED, COINIT_MULTITHREADED},
    unknwnbase::IUnknown,
};
use winapi::{Class, Interface};

use check_succeeded;
use error::{succeeded_or_err, HResult, ResultExt};
use handle::CoTaskMem;

/// Wrap COM interfaces sanely
///
/// Originally from [wio-rs](https://github.com/retep998/wio-rs) `ComPtr`
#[repr(transparent)]
pub struct ComRef<T>(NonNull<T>)
where
    T: Interface;
impl<T> ComRef<T>
where
    T: Interface,
{
    /// Creates a `ComRef` to wrap a raw pointer.
    /// It takes ownership over the pointer which means it does __not__ call `AddRef`.
    /// `T` __must__ be a COM interface that inherits from `IUnknown`.
    pub unsafe fn from_raw(ptr: *mut T) -> ComRef<T> {
        ComRef(NonNull::new(ptr).expect("ptr should not be null"))
    }
    /// Casts up the inheritance chain
    pub fn up<U>(self) -> ComRef<U>
    where
        T: Deref<Target = U>,
        U: Interface,
    {
        unsafe { ComRef::from_raw(self.into_raw() as *mut U) }
    }
    /// Extracts the raw pointer.
    /// You are now responsible for releasing it yourself.
    pub fn into_raw(self) -> *mut T {
        let p = self.0.as_ptr();
        mem::forget(self);
        p
    }
    fn as_unknown(&self) -> &IUnknown {
        unsafe { &*(self.as_raw() as *mut IUnknown) }
    }
    /// Get another interface via QueryInterface.
    pub fn cast<U>(&self) -> Result<ComRef<U>, HRESULT>
    where
        U: Interface,
    {
        let mut obj = null_mut();
        let err = unsafe { self.as_unknown().QueryInterface(&U::uuidof(), &mut obj) };
        if err < 0 {
            return Err(err);
        }
        Ok(unsafe { ComRef::from_raw(obj as *mut U) })
    }
    /// Obtains the raw pointer without transferring ownership.
    /// Do __not__ release this pointer because it is still owned by the `ComRef`.
    pub fn as_raw(&self) -> *mut T {
        self.0.as_ptr()
    }
}
impl<T> Deref for ComRef<T>
where
    T: Interface,
{
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.as_raw() }
    }
}
impl<T> Clone for ComRef<T>
where
    T: Interface,
{
    fn clone(&self) -> Self {
        unsafe {
            self.as_unknown().AddRef();
            ComRef::from_raw(self.as_raw())
        }
    }
}
impl<T> Drop for ComRef<T>
where
    T: Interface,
{
    fn drop(&mut self) {
        unsafe {
            self.as_unknown().Release();
        }
    }
}
impl<T> PartialEq<ComRef<T>> for ComRef<T>
where
    T: Interface,
{
    fn eq(&self, other: &ComRef<T>) -> bool {
        self.0 == other.0
    }
}

/// A scope for automatic COM initialization and deinitialization.
///
/// Functions that need COM initialized can take a `&ComApartmentScope` argument and be sure that
/// it is so. It's recommended to use a thread local for this through the
/// [`INIT_MTA`](constant.INIT_MTA.html) or [`INIT_STA`](constant.INIT_STA.html) statics.
#[derive(Debug, Default)]
pub struct ComApartmentScope {
    /// PhantomData used in lieu of unstable impl !Send + !Sync.
    /// It must be dropped on the same thread it was created on so it can't be Send,
    /// and references are meant to indicate that COM has been inited on the current thread so it
    /// can't be Sync.
    _do_not_send: PhantomData<Rc<()>>,
}

impl ComApartmentScope {
    /// This thread should be the sole occupant of a single thread apartment
    pub fn init_sta() -> Result<Self, HResult> {
        unsafe { check_succeeded!(CoInitializeEx(ptr::null_mut(), COINIT_APARTMENTTHREADED)) }?;

        Ok(Default::default())
    }

    /// This thread should join the process's multithreaded apartment
    pub fn init_mta() -> Result<Self, HResult> {
        unsafe { check_succeeded!(CoInitializeEx(ptr::null_mut(), COINIT_MULTITHREADED)) }?;

        Ok(Default::default())
    }
}

impl Drop for ComApartmentScope {
    fn drop(&mut self) {
        unsafe {
            CoUninitialize();
        }
    }
}

thread_local! {
    // TODO these examples should probably be in convenience functions.
    /// A single thread apartment scope for the duration of the current thread.
    ///
    /// # Example
    /// ```
    /// use comedy::com::{ComApartmentScope, INIT_STA};
    ///
    /// fn do_com_stuff(_com: &ComApartmentScope) {
    /// }
    ///
    /// INIT_STA.with(|com| {
    ///     let com = match com {
    ///         Err(e) => return Err(e.clone()),
    ///         Ok(ref com) => com,
    ///     };
    ///     do_com_stuff(com);
    ///     Ok(())
    /// }).unwrap()
    /// ```
    pub static INIT_STA: Result<ComApartmentScope, HResult> = ComApartmentScope::init_sta();

    /// A multithreaded apartment scope for the duration of the current thread.
    ///
    /// # Example
    /// ```
    /// use comedy::com::{ComApartmentScope, INIT_MTA};
    ///
    /// fn do_com_stuff(_com: &ComApartmentScope) {
    /// }
    ///
    /// INIT_MTA.with(|com| {
    ///     let com = match com {
    ///         Err(e) => return Err(e.clone()),
    ///         Ok(ref com) => com,
    ///     };
    ///     do_com_stuff(com);
    ///     Ok(())
    /// }).unwrap()
    /// ```
    pub static INIT_MTA: Result<ComApartmentScope, HResult> = ComApartmentScope::init_mta();
}

/// Create an instance of a COM class.
///
/// This is mostly just a call to `CoCreateInstance` with some error handling.
/// The CLSID of the class and the IID of the interface come from the winapi `RIDL` macro, which
/// defines `Class` and `Interface` implementations.
pub fn create_instance<C, I>(ctx: CLSCTX) -> Result<ComRef<I>, HResult>
where
    C: Class,
    I: Interface,
{
    get(|interface| unsafe {
        CoCreateInstance(
            &C::uuidof(),
            ptr::null_mut(), // pUnkOuter
            ctx,
            &I::uuidof(),
            interface as *mut *mut _,
        )
    })
    .function("CoCreateInstance")
}

/// Create an instance of a COM class in the current process (`CLSCTX_LOCAL_SERVER`).
pub fn create_instance_local_server<C, I>() -> Result<ComRef<I>, HResult>
where
    C: Class,
    I: Interface,
{
    create_instance::<C, I>(CLSCTX_LOCAL_SERVER)
}

/// Create an instance of a COM class in a separate process space on the same machine
/// (`CLSCTX_INPROC_SERVER`).
pub fn create_instance_inproc_server<C, I>() -> Result<ComRef<I>, HResult>
where
    C: Class,
    I: Interface,
{
    create_instance::<C, I>(CLSCTX_INPROC_SERVER)
}

/// Get an interface of type `U` via `IUnknown::QueryInterface()`.
pub fn cast<T, U>(interface: &ComRef<T>) -> Result<ComRef<U>, HResult>
where
    T: Interface,
    U: Interface,
{
    interface
        .cast()
        .map_err(|hr| HResult::new(hr).function("IUnknown::QueryInterface"))
}

/// Call a COM method, returning a `Result`.
///
/// An error is returned if the call fails. The error is augmented with the name of the interface
/// and method, and the file name and line number of the macro usage.
///
/// `QueryInterface` is not used, the receiving interface must already be the given type.
///
/// # Example
///
/// ```ignore
/// fn cancel_job(job: &ComRef<IBackgroundCopyJob>) -> Result<(), HResult> {
///     unsafe {
///         com_call!(job, IBackgroundCopyJob::Cancel())
///     }
/// }
/// ```
#[macro_export]
macro_rules! com_call {
    ($obj:expr, $interface:ident :: $method:ident ( $($arg:expr),* )) => {
        $crate::error::succeeded_or_err({
            let obj: &$interface = &*$obj;
            obj.$method($($arg),*)
        }).function(concat!(stringify!($interface), "::", stringify!($method)))
          .file_line(file!(), line!())
    };

    // support for trailing comma in method argument list
    ($obj:expr, $interface:ident :: $method:ident ( $($arg:expr),+ , )) => {
        $crate::com_call!($obj, $interface::$method($($arg),+))
    };
}

/// Get an interface pointer that is returned through an output parameter.
///
/// If the call returns a failure `HRESULT`, return an error.
///
/// # Nulls and Enumerators
/// If the method succeeds but the resulting interface pointer is null, this will return an
/// `HResult` error with the successful return code. In particular this can happen with
/// enumeration interfaces, which return `S_FALSE` when they write less than the requested number
/// of results.
pub fn get<I, F>(getter: F) -> Result<ComRef<I>, HResult>
where
    I: Interface,
    F: FnOnce(*mut *mut I) -> HRESULT,
{
    let mut interface: *mut I = ptr::null_mut();

    let hr = succeeded_or_err(getter(&mut interface as *mut *mut I))?;

    if interface.is_null() {
        Err(HResult::new(hr))
    } else {
        Ok(unsafe { ComRef::from_raw(interface) })
    }
}

/// Call a COM method, create a [`ComRef`](com/struct.ComRef.html) from an output parameter.
///
/// An error is returned if the call fails or if the interface pointer is null. The error is
/// augmented with the name of the interface and method, and the file name and line number of the
/// macro usage.
///
/// # Nulls and Enumerators
/// If the method succeeds but the resulting interface pointer is null, this will return an
/// `HResult` error with the successful return code. In particular this can happen with
/// enumeration interfaces, which return `S_FALSE` when they write less than the requested number
/// of results.
///
/// # Example
///
/// ```ignore
/// fn create_job(bcm: &ComRef<IBackgroundCopyManager>)
///     -> Result<ComRef<IBackgroundCopyJob>, HResult>
/// {
///     unsafe {
///         com_call_getter!(
///             |job| bcm,
///             IBackgroundCopyManager::CreateJob(x, y, z, job)
///         )
///     }
/// }
/// ```
#[macro_export]
macro_rules! com_call_getter {
    (| $outparam:ident | $obj:expr, $interface:ident :: $method:ident ( $($arg:expr),* )) => {{
        let obj: &$interface = &*$obj;
        $crate::com::get(|$outparam| {
            obj.$method($($arg),*)
        }).function(concat!(stringify!($interface), "::", stringify!($method)))
          .file_line(file!(), line!())
    }};

    // support for trailing comma in method argument list
    (| $outparam:ident | $obj:expr, $interface:ident :: $method:ident ( $($arg:expr),+ , )) => {
        $crate::com_call_getter!(|$outparam| $obj, $interface::$method($($arg),+))
    };
}

/// Get a task memory pointer that is returned through an output parameter.
///
/// If the call returns a failure `HRESULT`, return an error.
///
/// # Nulls
/// If the method succeeds but the resulting pointer is null, this will return an `HResult`
/// error with the successful return code.
pub fn get_cotaskmem<F, T>(getter: F) -> Result<CoTaskMem, HResult>
where
    F: FnOnce(*mut *mut T) -> HRESULT,
{
    let mut ptr = ptr::null_mut() as *mut T;

    let hr = succeeded_or_err(getter(&mut ptr))?;

    if ptr.is_null() {
        Err(HResult::new(hr))
    } else {
        Ok(unsafe { CoTaskMem::wrap(ptr as LPVOID).unwrap() })
    }
}

/// Call a COM method, create a [`CoTaskMem`](handle/struct.CoTaskMem.html) from an output
/// parameter.
///
/// An error is returned if the call fails or if the pointer is null. The error is augmented with
/// the name of the interface and method, and the file name and line number of the macro usage.
///
/// # Nulls
/// If the method succeeds but the resulting pointer is null, this will return an `HResult`
/// error with the successful return code.
///
/// # Example
///
/// ```ignore
/// fn get_error_description(bcm: &ComRef<IBackgroundCopyManager>, hr: HRESULT)
///     -> Result<CoTaskMem, HResult>
/// {
///     unsafe {
///         com_call_taskmem_getter!(
///             |desc| bcm,
///             IBackgroundCopyManager::GetErrorDescription(hr, language_id, desc)
///         )
///     }
/// }
/// ```
#[macro_export]
macro_rules! com_call_taskmem_getter {
    (| $outparam:ident | $obj:expr, $interface:ident :: $method:ident ( $($arg:expr),* )) => {{
        $crate::com::get_cotaskmem(|$outparam| {
            $obj.$method($($arg),*)
        }).function(concat!(stringify!($interface), "::", stringify!($method)))
          .file_line(file!(), line!())
    }};

    // support for trailing comma in method argument list
    (| $outparam:ident | $obj:expr, $interface:ident :: $method:ident ( $($arg:expr),+ , )) => {
        $crate::com_call_taskmem_getter!(|$outparam| $obj, $interface::$method($($arg),+))
    };
}
