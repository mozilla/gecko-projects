// Licensed under the Apache License, Version 2.0
// <LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your option.
// All files in the project carrying such notice may not be copied, modified, or distributed
// except according to those terms.

//! Utilities for Windows COM, error handling, handles, and strings.

extern crate failure;
extern crate failure_derive;
extern crate winapi;

pub mod com;
pub mod error;
pub mod handle;
pub mod wide;

pub use error::Error;
