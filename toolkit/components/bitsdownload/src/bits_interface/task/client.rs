/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::error::BITSTaskError;
use super::string::nsCString_to_OsString;

use bits_client::BitsClient;
use nsstring::nsCString;
use std::cell::Cell;

thread_local! {
    static BITS_CLIENT: Cell<Option<Result<BitsClient, BITSTaskError>>> = Cell::new(None);
}

pub fn make_bits_client(job_name: &nsCString, save_path_prefix: &nsCString) {
    // Immediately invoked closure just to be able to use the ? operator.
    let client_result = (|| -> Result<BitsClient, BITSTaskError> {
        Ok(BitsClient::new(
            nsCString_to_OsString(&job_name)?,
            nsCString_to_OsString(&save_path_prefix)?,
        )?)
    })();
    BITS_CLIENT.with(|cell| cell.set(Some(client_result)));
}

pub fn with_bits_client<F, R>(closure: F) -> Result<R, BITSTaskError>
where
    F: FnOnce(&mut BitsClient) -> Result<R, BITSTaskError>,
{
    BITS_CLIENT.with(|cell| {
        let maybe_client = cell.take();
        let client_result = match maybe_client {
            Some(r) => r,
            None => return Err(BITSTaskError::with_message("Missing BITS client")),
        };
        let mut client = match client_result {
            Ok(c) => c,
            Err(error) => {
                cell.set(Some(Err(error.clone())));
                return Err(BITSTaskError::from(error));
            }
        };
        let result = closure(&mut client);
        cell.set(Some(Ok(client)));
        result
    })
}
