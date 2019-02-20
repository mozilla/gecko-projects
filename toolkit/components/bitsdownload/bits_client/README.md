bits\_client
============

Interfaces for BITS.

bits\_client lib
---------------

`bits_client` is the primary target and provides `BitsClient`, an API for creating and monitoring BITS jobs.

`bits_client::new()` creates a `BitsClient` that does all operations within the current process, as the current user.

bits crate
----------

`bits` is a safe interface to BITS, providing connections to the
Background Copy Manager, some basic operations on Background Copy Jobs, and
methods for implementing `IBackgroundCopyCallback`s in Rust.

utility crates
--------------

- comedy
  Deals with Windows error handling, handles, COM, and wide strings.

  - `comedy::error` provides macros for calling Windows functions and producing
    `Result`s with detailed errors (the `comedy::Error` type)
    and implements `Fail` from `failure`.
  - `comedy::com` has tools for setting up COM and invoking COM interfaces with
    access to `Result`s.
  - `comedy::handle` implements `Drop` to automatically clean up a few kinds of
    handle.
  - `comedy::wide` converts between `[u16]`, `OsStr`, `Path`, and raw
    pointers to (possibly null-terminated) wide character arrays.

- filetime\_win

  Utilities for converting, serializing, and displaying `FILETIME`
  and `SYSTEMTIME`.

- guid\_win

  Ditto for `GUID`/`CLSID`.


test\_client example
-------------------

`examples/test_client.rs` shows how to use the API.
