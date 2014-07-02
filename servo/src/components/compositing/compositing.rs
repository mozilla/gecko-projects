/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![crate_id = "github.com/mozilla/servo#compositing:0.1"]
#![crate_type = "lib"]
#![crate_type = "dylib"]
#![crate_type = "rlib"]

#![comment = "The Servo Parallel Browser Project"]
#![license = "MPL"]

#![feature(globs, phase, macro_rules)]

#[phase(plugin, link)]
extern crate log;

extern crate debug;

extern crate alert;
extern crate azure;
extern crate geom;
extern crate gfx;
#[cfg(not(target_os="android"))]
extern crate glfw;
#[cfg(target_os="android")]
extern crate glut;
extern crate layers;
extern crate layout;
extern crate opengles;
extern crate png;
extern crate script;
extern crate servo_msg = "msg";
extern crate servo_net = "net";
#[phase(plugin, link)]
extern crate servo_util = "util";

extern crate libc;
extern crate time;
extern crate url;

#[cfg(target_os="macos")]
extern crate core_graphics;
#[cfg(target_os="macos")]
extern crate core_text;

pub use compositor_task::{CompositorChan, CompositorTask};
pub use constellation::Constellation;

mod compositor_task;

mod compositor_data;

mod compositor;
mod headless;

mod pipeline;
mod constellation;

mod windowing;

#[path="platform/mod.rs"]
pub mod platform;
