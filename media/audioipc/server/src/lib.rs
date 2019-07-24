// Copyright © 2017 Mozilla Foundation
//
// This program is made available under an ISC-style license.  See the
// accompanying file LICENSE for details

#[macro_use]
extern crate error_chain;

#[macro_use]
extern crate log;

extern crate audioipc;
extern crate bytes;
extern crate cubeb_core as cubeb;
extern crate futures;
extern crate lazycell;
extern crate libc;
extern crate slab;
extern crate tokio_core;
extern crate tokio_uds;
extern crate audio_thread_priority;
#[macro_use]
extern crate lazy_static;

use audioipc::core;
use audioipc::platformhandle_passing::framed_with_platformhandles;
use audioipc::rpc;
use audioipc::{MessageStream, PlatformHandle, PlatformHandleType};
use futures::sync::oneshot;
use futures::Future;
use std::error::Error;
use std::os::raw::c_void;
use std::ptr;
use audio_thread_priority::promote_current_thread_to_real_time;
use std::ffi::{CStr, CString};
use std::sync::Mutex;

mod server;

struct CubebContextParams {
    context_name: CString,
    backend_name: Option<CString>,
}

lazy_static! {
    static ref G_CUBEB_CONTEXT_PARAMS: Mutex<CubebContextParams> = Mutex::new(CubebContextParams {
        context_name: CString::new("AudioIPC Server").unwrap(),
        backend_name: None,
    });
}

#[allow(deprecated)]
pub mod errors {
    error_chain! {
        links {
            AudioIPC(::audioipc::errors::Error, ::audioipc::errors::ErrorKind);
        }
        foreign_links {
            Cubeb(::cubeb::Error);
            Io(::std::io::Error);
            Canceled(::futures::sync::oneshot::Canceled);
        }
    }
}

use errors::*;

struct ServerWrapper {
    core_thread: core::CoreThread,
    callback_thread: core::CoreThread,
}

fn run() -> Result<ServerWrapper> {
    trace!("Starting up cubeb audio server event loop thread...");

    let callback_thread = try!(
        core::spawn_thread("AudioIPC Callback RPC", || {
            match promote_current_thread_to_real_time(0, 48000) {
                Ok(_) => { }
                Err(_) => {
                    debug!("Failed to promote audio callback thread to real-time.");
                }
            }
            trace!("Starting up cubeb audio callback event loop thread...");
            Ok(())
        }).or_else(|e| {
            debug!(
                "Failed to start cubeb audio callback event loop thread: {:?}",
                e.description()
            );
            Err(e)
        })
    );

    let core_thread = try!(
        core::spawn_thread("AudioIPC Server RPC", move || Ok(())).or_else(|e| {
            debug!(
                "Failed to cubeb audio core event loop thread: {:?}",
                e.description()
            );
            Err(e)
        })
    );

    Ok(ServerWrapper {
        core_thread: core_thread,
        callback_thread: callback_thread,
    })
}

#[no_mangle]
pub extern "C" fn audioipc_server_start(context_name: *const std::os::raw::c_char,
                                        backend_name: *const std::os::raw::c_char) -> *mut c_void {
    let mut params = G_CUBEB_CONTEXT_PARAMS.lock().unwrap();
    if !context_name.is_null() {
        params.context_name = unsafe { CStr::from_ptr(context_name) }.to_owned();
    }
    if !backend_name.is_null() {
        let backend_string = unsafe { CStr::from_ptr(backend_name) }.to_owned();
        params.backend_name = Some(backend_string);
    }
    match run() {
        Ok(server) => Box::into_raw(Box::new(server)) as *mut _,
        Err(_) => ptr::null_mut() as *mut _,
    }
}

#[no_mangle]
pub extern "C" fn audioipc_server_new_client(p: *mut c_void) -> PlatformHandleType {
    let (wait_tx, wait_rx) = oneshot::channel();
    let wrapper: &ServerWrapper = unsafe { &*(p as *mut _) };

    let cb_remote = wrapper.callback_thread.remote();

    // We create a connected pair of anonymous IPC endpoints. One side
    // is registered with the reactor core, the other side is returned
    // to the caller.
    MessageStream::anonymous_ipc_pair()
        .and_then(|(sock1, sock2)| {
            // Spawn closure to run on same thread as reactor::Core
            // via remote handle.
            wrapper.core_thread.remote().spawn(|handle| {
                trace!("Incoming connection");
                sock2.into_tokio_ipc(handle)
                    .and_then(|sock| {
                        let transport = framed_with_platformhandles(sock, Default::default());
                        rpc::bind_server(transport, server::CubebServer::new(cb_remote), handle);
                        Ok(())
                    }).map_err(|_| ())
                    // Notify waiting thread that sock2 has been registered.
                    .and_then(|_| wait_tx.send(()))
            });
            // Wait for notification that sock2 has been registered
            // with reactor::Core.
            let _ = wait_rx.wait();
            Ok(PlatformHandle::from(sock1).as_raw())
        }).unwrap_or(-1isize as PlatformHandleType)
}

#[no_mangle]
pub extern "C" fn audioipc_server_stop(p: *mut c_void) {
    let wrapper = unsafe { Box::<ServerWrapper>::from_raw(p as *mut _) };
    drop(wrapper);
}
