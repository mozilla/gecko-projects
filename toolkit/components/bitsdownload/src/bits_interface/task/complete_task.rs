/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::{
    error::BITSTaskError,
    client::with_bits_client,
    string::Guid_from_nsCString,
};
use bits_interface::BITSInterface;
use crossbeam_utils::atomic::AtomicCell;
use log::warn;
use moz_task::Task;
use nserror::{nsresult, NsresultExt, NS_ERROR_FAILURE, NS_OK};
use nsstring::nsCString;
use std::fmt::Debug;
use xpcom::{
    interfaces::{nsIBITSCompleteDownloadCallback},
    RefPtr, ThreadBoundRefPtr
};

pub enum CompleteDownloadResult {
    Success,
    BITSFailure(nsCString),
    Failure(nsCString),
}

impl CompleteDownloadResult {
    pub fn bits_failure_from<E: Debug>(error: E) -> CompleteDownloadResult {
        CompleteDownloadResult::BITSFailure(nsCString::from(format!("{:?}", error)))
    }

    pub fn failure_from<E: Debug>(error: E) -> CompleteDownloadResult {
        CompleteDownloadResult::Failure(nsCString::from(format!("{:?}", error)))
    }
}

pub struct CompleteDownloadTask {
    bits_interface: AtomicCell<Option<ThreadBoundRefPtr<BITSInterface>>>,
    id: nsCString,
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIBITSCompleteDownloadCallback>>>,
    result: AtomicCell<Option<CompleteDownloadResult>>,
}

impl CompleteDownloadTask {
    pub fn new(
        bits_interface: RefPtr<BITSInterface>,
        id: nsCString,
        callback: RefPtr<nsIBITSCompleteDownloadCallback>,
    ) -> CompleteDownloadTask {
        CompleteDownloadTask {
            bits_interface: AtomicCell::new(Some(ThreadBoundRefPtr::new(bits_interface))),
            id,
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            result: AtomicCell::new(None),
        }
    }
}

impl Task for CompleteDownloadTask {
    fn run(&self) {
        let result = with_bits_client(|client| -> Result<CompleteDownloadResult, BITSTaskError> {
            let result = client.complete_job(Guid_from_nsCString(&self.id)?);
            match result? {
                Ok(()) => Ok(CompleteDownloadResult::Success),
                Err(error) => Ok(CompleteDownloadResult::bits_failure_from(error)),
            }
        });
        match result {
            Ok(start_result) => { self.result.store(Some(start_result)); },
            Err(task_error) => {
                self.result.store(Some(CompleteDownloadResult::failure_from(task_error)));
            },
        }
    }

    fn done(&self) -> Result<(), nsresult> {
        // If TaskRunnable.run() calls Task.done() to return a result
        // on the main thread before TaskRunnable.run() returns on the worker
        // thread, then the Task will get dropped on the worker thread.
        //
        // But the callback is an nsXPCWrappedJS that isn't safe to release
        // on the worker thread.  So we move it out of the Task here to ensure
        // it gets released on the main thread.
        let maybe_tb_callback = self.callback.swap(None);
        // It also isn't safe to drop the BITSInterface RefPtr off-thread,
        // because BITSInterface refcounting is non-atomic
        let maybe_tb_interface = self.bits_interface.swap(None);

        let maybe_callback = maybe_tb_callback.as_ref()
            .ok_or("Unexpected: No callback")
            .and_then(|tc| tc.get_ref().ok_or("Unexpected: Callback on the wrong thread"))
            .map_err(String::from);
        let maybe_bits_interface = maybe_tb_interface.as_ref()
            .ok_or("Unexpected: No BITS interface")
            .and_then(|ti| ti.get_ref().ok_or("Unexpected: BITS interface on wrong thread"))
            .map_err(String::from);


        let callback_result: Result<nsresult, String> = match self.result.swap(None) {
            Some(CompleteDownloadResult::Success) => {
                match maybe_bits_interface {
                    Ok(bits_interface) => {
                        bits_interface.set_status(NS_OK);
                        bits_interface.set_pending(false);
                        bits_interface.clear_download_id();
                        maybe_callback.map(|callback| unsafe { callback.Success() })
                    },
                    Err(error_message) => {
                        let error_message = nsCString::from(error_message);
                        maybe_callback.map(|callback| unsafe { callback.Failure(&*error_message) })
                    }
                }
            },
            Some(CompleteDownloadResult::BITSFailure(error_message)) => {
                maybe_callback.map(|callback| unsafe { callback.BITSFailure(&*error_message) })
            },
            Some(CompleteDownloadResult::Failure(error_message)) => {
                maybe_callback.map(|callback| unsafe { callback.Failure(&*error_message) })
            },
            None => maybe_callback.map(|callback| unsafe {
                callback.Failure(&*nsCString::from("Unexpected: No result"))
            })
        };

        match callback_result {
            Ok(rv) => rv.to_result(),
            Err(error_message) => {
                warn!("{}", error_message);
                Err(NS_ERROR_FAILURE)
            }
        }
    }
}
