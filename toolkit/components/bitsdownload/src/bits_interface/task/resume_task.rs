/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::{
    error::BITSTaskError,
    client::with_bits_client,
    string::Guid_from_nsCString,
};
use crossbeam_utils::atomic::AtomicCell;
use log::warn;
use moz_task::Task;
use nserror::{nsresult, NS_ERROR_FAILURE, NS_OK};
use nsstring::nsCString;
use std::fmt::Debug;
use xpcom::{
    interfaces::{nsIBITSResumeDownloadCallback},
    RefPtr, ThreadBoundRefPtr
};

pub enum ResumeDownloadResult {
    Success,
    BITSFailure(nsCString),
    Failure(nsCString),
}

impl ResumeDownloadResult {
    pub fn bits_failure_from<E: Debug>(error: E) -> ResumeDownloadResult {
        ResumeDownloadResult::BITSFailure(nsCString::from(format!("{:?}", error)))
    }

    pub fn failure_from<E: Debug>(error: E) -> ResumeDownloadResult {
        ResumeDownloadResult::Failure(nsCString::from(format!("{:?}", error)))
    }
}

pub struct ResumeDownloadTask {
    id: nsCString,
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIBITSResumeDownloadCallback>>>,
    result: AtomicCell<Option<ResumeDownloadResult>>,
}

impl ResumeDownloadTask {
    pub fn new(
        id: nsCString,
        callback: Option<RefPtr<nsIBITSResumeDownloadCallback>>,
    ) -> ResumeDownloadTask {
        ResumeDownloadTask {
            id,
            callback: AtomicCell::new(callback.map(ThreadBoundRefPtr::new)),
            result: AtomicCell::new(None),
        }
    }
}

impl Task for ResumeDownloadTask {
    fn run(&self) {
        let result = with_bits_client(|client| -> Result<ResumeDownloadResult, BITSTaskError> {
            let result = client.resume_job(Guid_from_nsCString(&self.id)?);
            match result? {
                Ok(()) => Ok(ResumeDownloadResult::Success),
                Err(error) => Ok(ResumeDownloadResult::bits_failure_from(error)),
            }
        });
        match result {
            Ok(start_result) => { self.result.store(Some(start_result)); },
            Err(task_error) => {
                self.result.store(Some(ResumeDownloadResult::failure_from(task_error)));
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

        let maybe_callback = maybe_tb_callback.as_ref().and_then(|tc| tc.get_ref());

        match self.result.swap(None) {
            Some(ResumeDownloadResult::Success) => match maybe_callback {
                Some(callback) => unsafe { callback.Success() },
                None => NS_OK,
            },
            Some(ResumeDownloadResult::BITSFailure(error_message)) => match maybe_callback {
                Some(callback) => unsafe { callback.BITSFailure(&*error_message) },
                None => NS_ERROR_FAILURE,
            },
            Some(ResumeDownloadResult::Failure(error_message)) => match maybe_callback {
                Some(callback) => unsafe { callback.Failure(&*error_message) },
                None => NS_ERROR_FAILURE,
            },
            None => match maybe_callback {
                Some(callback) => {
                    unsafe { callback.Failure(&*nsCString::from("Unexpected: No result")) }
                },
                None => {
                    warn!("Unexpected: No result");
                    NS_ERROR_FAILURE
                }
            }
        }.to_result()
    }
}
