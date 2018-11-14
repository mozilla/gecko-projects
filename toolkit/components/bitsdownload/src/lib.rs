/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![cfg(target_os = "windows")]

extern crate bits_client;
extern crate comedy;
extern crate crossbeam_utils;
extern crate failure;
extern crate libc;
extern crate log;
extern crate moz_task;
extern crate nserror;
extern crate nsstring;
extern crate xpcom;

pub mod bits_interface;
