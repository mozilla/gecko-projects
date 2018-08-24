#![allow(non_snake_case)]

extern crate base64;
extern crate cookie;
#[macro_use]
extern crate log;
extern crate futures;
extern crate http;
extern crate hyper;
extern crate regex;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate time;
extern crate tokio;
extern crate unicode_segmentation;
extern crate url;

#[macro_use]
pub mod macros;
pub mod actions;
pub mod capabilities;
pub mod command;
pub mod common;
pub mod error;
pub mod httpapi;
pub mod response;
pub mod server;

#[cfg(test)]
#[macro_use]
extern crate lazy_static;
#[cfg(test)]
pub mod test;
