/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate log;
extern crate libc;

use std::thread;
use std::time::Duration;

mod device;
mod devicemap;
mod iokit;
mod iohid;
mod monitor;

use self::devicemap::DeviceMap;
use self::monitor::Monitor;

use consts::PARAMETER_SIZE;
use khmatcher::KeyHandleMatcher;
use runloop::RunLoop;
use util::{io_err, OnceCallback};
use u2fprotocol::{u2f_register, u2f_sign, u2f_is_keyhandle_valid};

#[derive(Default)]
pub struct PlatformManager {
    // Handle to the thread loop.
    thread: Option<RunLoop>,
}

impl PlatformManager {
    pub fn new() -> Self {
        Default::default()
    }

    pub fn register(
        &mut self,
        timeout: u64,
        challenge: Vec<u8>,
        application: Vec<u8>,
        key_handles: Vec<Vec<u8>>,
        callback: OnceCallback<Vec<u8>>,
    ) {
        // Abort any prior register/sign calls.
        self.cancel();

        let cbc = callback.clone();

        let thread = RunLoop::new_with_timeout(
            move |alive| {
                let mut devices = DeviceMap::new();
                let monitor = try_or!(Monitor::new(), |e| callback.call(Err(e)));
                let mut matches = KeyHandleMatcher::new(&key_handles);

                'top: while alive() && monitor.alive() {
                    for event in monitor.events() {
                        devices.process_event(event);
                    }

                    // Query newly added devices.
                    matches.update(devices.iter_mut(), |device, key_handle| {
                        u2f_is_keyhandle_valid(device, &challenge, &application, key_handle)
                            .unwrap_or(false /* no match on failure */)
                    });

                    // Iterate all devices that don't match any of the handles
                    // in the exclusion list and try to register.
                    for (path, device) in devices.iter_mut() {
                        if matches.get(path).is_empty() {
                            if let Ok(bytes) = u2f_register(device, &challenge, &application) {
                                callback.call(Ok(bytes));
                                return;
                            }
                        }

                        // Check to see if monitor.events has any hotplug events that we'll need
                        // to handle
                        if monitor.events().size_hint().0 > 0 {
                            debug!("Hotplug event; restarting loop");
                            continue 'top;
                        }
                    }

                    thread::sleep(Duration::from_millis(100));
                }

                callback.call(Err(io_err("aborted or timed out")));
            },
            timeout,
        );

        self.thread = Some(try_or!(
            thread,
            |_| cbc.call(Err(io_err("couldn't create runloop")))
        ));
    }


    pub fn sign(
        &mut self,
        timeout: u64,
        challenge: Vec<u8>,
        application: Vec<u8>,
        key_handles: Vec<Vec<u8>>,
        callback: OnceCallback<(Vec<u8>, Vec<u8>)>,
    ) {
        // Abort any prior register/sign calls.
        self.cancel();

        let cbc = callback.clone();

        let thread = RunLoop::new_with_timeout(
            move |alive| {
                let mut devices = DeviceMap::new();
                let monitor = try_or!(Monitor::new(), |e| callback.call(Err(e)));
                let mut matches = KeyHandleMatcher::new(&key_handles);

                'top: while alive() && monitor.alive() {
                    for event in monitor.events() {
                        devices.process_event(event);
                    }

                    // Query newly added devices.
                    matches.update(devices.iter_mut(), |device, key_handle| {
                        u2f_is_keyhandle_valid(device, &challenge, &application, key_handle)
                            .unwrap_or(false /* no match on failure */)
                    });

                    // Iterate all devices.
                    for (path, device) in devices.iter_mut() {
                        let key_handles = matches.get(path);

                        // If the device matches none of the given key handles
                        // then just make it blink with bogus data.
                        if key_handles.is_empty() {
                            let blank = vec![0u8; PARAMETER_SIZE];
                            if let Ok(_) = u2f_register(device, &blank, &blank) {
                                callback.call(Err(io_err("invalid key")));
                                return;
                            }

                            continue;
                        }

                        // Otherwise, try to sign.
                        for key_handle in key_handles {
                            if let Ok(bytes) = u2f_sign(
                                device,
                                &challenge,
                                &application,
                                key_handle,
                            )
                            {
                                callback.call(Ok((key_handle.to_vec(), bytes)));
                                return;
                            }
                        }

                        // Check to see if monitor.events has any hotplug events that we'll
                        // need to handle
                        if monitor.events().size_hint().0 > 0 {
                            debug!("Hotplug event; restarting loop");
                            continue 'top;
                        }
                    }

                    thread::sleep(Duration::from_millis(100));
                }

                callback.call(Err(io_err("aborted or timed out")));
            },
            timeout,
        );

        self.thread = Some(try_or!(
            thread,
            |_| cbc.call(Err(io_err("couldn't create runloop")))
        ));
    }

    pub fn cancel(&mut self) {
        if let Some(thread) = self.thread.take() {
            thread.cancel();
        }
    }
}

impl Drop for PlatformManager {
    fn drop(&mut self) {
        debug!("OSX PlatformManager dropped");
        self.cancel();
    }
}
