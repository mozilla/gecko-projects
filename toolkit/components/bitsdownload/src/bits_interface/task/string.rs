/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::error::BITSTaskError;

use bits_client::Guid;
use nsstring::nsCString;
use std::ffi::OsString;
use std::str::FromStr;

#[allow(non_snake_case)]
pub fn nsCString_to_String(value: &nsCString) -> Result<String, BITSTaskError> {
    Ok(String::from_utf8(value[..].to_vec())?)
}

#[allow(non_snake_case)]
pub fn nsCString_to_OsString(value: &nsCString) -> Result<OsString, BITSTaskError> {
    Ok(OsString::from(nsCString_to_String(value)?))
}

#[allow(non_snake_case)]
pub fn Guid_from_nsCString(value: &nsCString) -> Result<Guid, BITSTaskError> {
    let guid_string = nsCString_to_String(&value)?;
    Ok(Guid::from_str(&guid_string)?)
}
