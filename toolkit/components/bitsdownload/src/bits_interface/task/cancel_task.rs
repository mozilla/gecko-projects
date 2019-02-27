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
use nserror::{nsresult, NS_ERROR_ABORT, NS_ERROR_FAILURE, NS_OK};
use nsstring::nsCString;
use std::fmt::Debug;
use xpcom::{
    interfaces::{nsIBITSCancelDownloadCallback},
    RefPtr, ThreadBoundRefPtr
};

pub enum CancelDownloadResult {
    Success,
    BITSFailure(nsCString),
    Failure(nsCString),
}

impl CancelDownloadResult {
    pub fn bits_failure_from<E: Debug>(error: E) -> CancelDownloadResult {
        CancelDownloadResult::BITSFailure(nsCString::from(format!("{:?}", error)))
    }

    pub fn failure_from<E: Debug>(error: E) -> CancelDownloadResult {
        CancelDownloadResult::Failure(nsCString::from(format!("{:?}", error)))
    }
}

pub struct CancelDownloadTask {
    bits_interface: AtomicCell<Option<ThreadBoundRefPtr<BITSInterface>>>,
    id: nsCString,
    status: AtomicCell<Option<nsresult>>,
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIBITSCancelDownloadCallback>>>,
    result: AtomicCell<Option<CancelDownloadResult>>,
}

impl CancelDownloadTask {
    pub fn new(
        bits_interface: RefPtr<BITSInterface>,
        id: nsCString,
        status: Option<nsresult>,
        callback: Option<RefPtr<nsIBITSCancelDownloadCallback>>,
    ) -> CancelDownloadTask {
        CancelDownloadTask {
            bits_interface: AtomicCell::new(Some(ThreadBoundRefPtr::new(bits_interface))),
            id,
            status: AtomicCell::new(status),
            callback: AtomicCell::new(callback.map(ThreadBoundRefPtr::new)),
            result: AtomicCell::new(None),
        }
    }
}

impl Task for CancelDownloadTask {
    fn run(&self) {
        let result = with_bits_client(|client| -> Result<CancelDownloadResult, BITSTaskError> {
            let result = client.cancel_job(Guid_from_nsCString(&self.id)?);
            match result? {
                Ok(()) => Ok(CancelDownloadResult::Success),
                Err(error) => Ok(CancelDownloadResult::bits_failure_from(error)),
            }
        });
        match result {
            Ok(start_result) => { self.result.store(Some(start_result)); },
            Err(task_error) => {
                self.result.store(Some(CancelDownloadResult::failure_from(task_error)));
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

        let maybe_callback = maybe_tb_callback.as_ref().and_then(|tc| tc.get_ref());
        let maybe_bits_interface = maybe_tb_interface.as_ref()
            .ok_or("Unexpected: No BITS interface")
            .and_then(|ti| ti.get_ref().ok_or("Unexpected: BITS interface on wrong thread"))
            .map_err(String::from);

        match self.result.swap(None) {
            Some(CancelDownloadResult::Success) => {
                match maybe_bits_interface {
                    Ok(bits_interface) => {
                        let status = self.status.swap(None).unwrap_or(NS_ERROR_ABORT);
                        bits_interface.set_status(status);
                        bits_interface.set_pending(false);
                        bits_interface.clear_download_id();
                        match maybe_callback {
                            Some(callback) => unsafe { callback.Success() },
                            None => NS_OK,
                        }
                    },
                    Err(error_message) => match maybe_callback {
                        Some(callback) => {
                            let error_message = nsCString::from(error_message);
                            unsafe { callback.Failure(&*error_message) }
                        },
                        None => {
                            warn!("{}", error_message);
                            NS_ERROR_FAILURE
                        }
                    }
                }
            },
            Some(CancelDownloadResult::BITSFailure(error_message)) => match maybe_callback {
                Some(callback) => unsafe { callback.BITSFailure(&*error_message) },
                None => NS_ERROR_FAILURE
            },
            Some(CancelDownloadResult::Failure(val)) => match maybe_callback {
                Some(callback) => unsafe { callback.Failure(&*val) }
                None => NS_ERROR_FAILURE
            },
            None => match maybe_callback {
                Some(callback) => unsafe {
                    callback.Failure(&*nsCString::from("Unexpected: No result"))
                }
                None => {
                    warn!("Unexpected: No result");
                    NS_ERROR_FAILURE
                }
            },
        }.to_result()
    }
}
