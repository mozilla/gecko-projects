/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This macro is very similar to the xpcom_macro, but works a bit differently.
 * It allows parameters to be specified as Option<const* nsI...> type, which
 * allows them to be null (null pointers will be converted to None).
 * It also returns errors via the callback rather than via the return value.
 *
 * This macro implicitly adds the callback argument.
 *
 * This macro also needs an action type, to be specified before the rust name,
 * in square brackets.
 */
#[macro_export]
macro_rules! nsIBits_method {
    // The internal rule @ensure_param converts raw pointer arguments to
    // references, calling dispatch_pretask_interface_error and returning if the
    // argument is null.
    // If, however, the type is optional, the reference will also be wrapped
    // in an option and null pointers will be converted to None.
    (@ensure_param [optional] $name:ident, $action:ident, $callback:ident) => {
        let $name = match Ensure::ensure($name) {
            Ok(val) => Some(val),
            Err(_) => None,
        };
    };
    (@ensure_param $name:ident, $action:ident, $callback:ident) => {
        let $name = match Ensure::ensure($name) {
            Ok(val) => val,
            Err(_) => {
                dispatch_pretask_interface_error(BitsTaskError::new(NullArgument, $action.into(), Pretask), $callback);
                return NS_OK;
            }
        };
    };
    
    ([$action:ident] $rust_name:ident => $xpcom_name:ident($($([$param_required:ident])* $param_name:ident: $param_type:ty $(,)*)*)) => {
        #[allow(non_snake_case)]
        unsafe fn $xpcom_name(&self, $($param_name: $param_type, )* callback: *const nsIBitsNewRequestCallback) -> nsresult {
            use xpcom::Ensure;
            use nserror::NS_OK;
            // When no params are passed, the imports below will not be used, so silence the
            // warning
            #[allow(unused_imports)]
            use bits_interface::{
                dispatch_callback::{
                    dispatch_pretask_interface_error, dispatch_request_via_callback,
                },
                error::{BitsTaskError, ErrorStage::Pretask, ErrorType::NullArgument},
            };

            let callback: &nsIBitsNewRequestCallback = match Ensure::ensure(callback) {
                Ok(val) => val,
                Err(result) => return result,
            };
            $(nsIBits_method!(@ensure_param $([$param_required])* $param_name, $action, callback);)*
            if let Err(error) = self.$rust_name($($param_name, )* callback) {
                let _ = dispatch_request_via_callback(Err(error), callback);
            }
            NS_OK
        }
    };
}

/*
 * Same as above, but expects a nsIBitsCallback as its callback.
 */
#[macro_export]
macro_rules! nsIBitsRequest_method {
    // The internal rule @ensure_param converts raw pointer arguments to
    // references, calling dispatch_pretask_interface_error and returning if the
    // argument is null.
    // If, however, the type is optional, the reference will also be wrapped
    // in an option and null pointers will be converted to None.
    (@ensure_param [optional] $name:ident, $action:ident, $callback:ident) => {
        let $name = match Ensure::ensure($name) {
            Ok(val) => Some(val),
            Err(_) => None,
        };
    };
    (@ensure_param $name:ident, $action:ident, $callback:ident) => {
        let $name = match Ensure::ensure($name) {
            Ok(val) => val,
            Err(_) => {
                dispatch_pretask_request_error(BitsTaskError::new(NullArgument, $action.into(), Pretask), $callback);
                return NS_OK;
            }
        };
    };
    
    ([$action:ident] $rust_name:ident => $xpcom_name:ident($($([$param_required:ident])* $param_name:ident: $param_type:ty $(,)*)*)) => {
        #[allow(non_snake_case)]
        unsafe fn $xpcom_name(&self, $($param_name: $param_type, )* callback: *const nsIBitsCallback) -> nsresult {
            use xpcom::Ensure;
            use nserror::NS_OK;
            // When no params are passed, the imports below will not be used, so silence the
            // warning
            #[allow(unused_imports)]
            use bits_interface::{
                dispatch_callback::{
                    dispatch_pretask_request_error, dispatch_via_callback,
                },
                error::{BitsTaskError, ErrorStage::Pretask, ErrorType::NullArgument},
            };

            let callback: &nsIBitsCallback = match Ensure::ensure(callback) {
                Ok(val) => val,
                Err(result) => return result,
            };
            $(nsIBitsRequest_method!(@ensure_param $([$param_required])* $param_name, $action, callback);)*
            if let Err(error) = self.$rust_name($($param_name, )* callback) {
                let _ = dispatch_via_callback(Err(error), callback);
            }
            NS_OK
        }
    };
}
