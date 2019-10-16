// Copyright © 2017 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details.
#![warn(unused_extern_crates)]

#[macro_use]
extern crate cubeb_backend;
#[macro_use]
extern crate log;

#[macro_use]
mod send_recv;
mod context;
mod stream;

use crate::context::ClientContext;
use crate::stream::ClientStream;
use audioipc::{PlatformHandle, PlatformHandleType};
use cubeb_backend::{capi, ffi};
use std::os::raw::{c_char, c_int};

type InitParamsTls = std::cell::RefCell<Option<CpuPoolInitParams>>;

thread_local!(static IN_CALLBACK: std::cell::RefCell<bool> = std::cell::RefCell::new(false));
thread_local!(static CPUPOOL_INIT_PARAMS: InitParamsTls = std::cell::RefCell::new(None));

// This must match the definition of AudioIpcInitParams in
// dom/media/CubebUtils.cpp in Gecko.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AudioIpcInitParams {
    // Fields only need to be public for ipctest.
    pub server_connection: PlatformHandleType,
    pub pool_size: usize,
    pub stack_size: usize,
    pub thread_create_callback: Option<extern "C" fn(*const ::std::os::raw::c_char)>,
}

#[derive(Clone, Copy, Debug)]
struct CpuPoolInitParams {
    pool_size: usize,
    stack_size: usize,
    thread_create_callback: Option<extern "C" fn(*const ::std::os::raw::c_char)>,
}

impl CpuPoolInitParams {
    fn init_with(params: &AudioIpcInitParams) -> Self {
        CpuPoolInitParams {
            pool_size: params.pool_size,
            stack_size: params.stack_size,
            thread_create_callback: params.thread_create_callback,
        }
    }
}

fn set_in_callback(in_callback: bool) {
    IN_CALLBACK.with(|b| {
        assert_eq!(*b.borrow(), !in_callback);
        *b.borrow_mut() = in_callback;
    });
}

fn run_in_callback<F, R>(f: F) -> R
where
    F: FnOnce() -> R
{
    set_in_callback(true);

    let r = f();

    set_in_callback(false);

    r
}

fn assert_not_in_callback() {
    IN_CALLBACK.with(|b| {
        assert_eq!(*b.borrow(), false);
    });
}

fn set_cpupool_init_params<P>(params: P)
where
    P: Into<Option<CpuPoolInitParams>>,
{
    CPUPOOL_INIT_PARAMS.with(|p| {
        *p.borrow_mut() = params.into();
    });
}

static mut G_SERVER_FD: Option<PlatformHandle> = None;

#[no_mangle]
/// Entry point from C code.
pub unsafe extern "C" fn audioipc_client_init(
    c: *mut *mut ffi::cubeb,
    context_name: *const c_char,
    init_params: *const AudioIpcInitParams,
) -> c_int {
    if init_params.is_null() {
        return cubeb_backend::ffi::CUBEB_ERROR;
    }

    let init_params = &*init_params;

    // TODO: Better way to pass extra parameters to Context impl.
    if G_SERVER_FD.is_some() {
        return cubeb_backend::ffi::CUBEB_ERROR;
    }
    G_SERVER_FD = PlatformHandle::try_new(init_params.server_connection);
    if G_SERVER_FD.is_none() {
        return cubeb_backend::ffi::CUBEB_ERROR;
    }

    let cpupool_init_params = CpuPoolInitParams::init_with(&init_params);
    set_cpupool_init_params(cpupool_init_params);
    capi::capi_init::<ClientContext>(c, context_name)
}
