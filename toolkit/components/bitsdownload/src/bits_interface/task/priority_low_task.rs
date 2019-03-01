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
use nserror::{nsresult, NsresultExt, NS_ERROR_FAILURE};
use nsstring::nsCString;
use std::fmt::Debug;
use xpcom::{
    interfaces::{nsIBITSSetPriorityLowCallback},
    RefPtr, ThreadBoundRefPtr
};

pub enum SetPriorityLowResult {
    Success,
    BITSFailure(nsCString),
    Failure(nsCString),
}

impl SetPriorityLowResult {
    pub fn bits_failure_from<E: Debug>(error: E) -> SetPriorityLowResult {
        SetPriorityLowResult::BITSFailure(nsCString::from(format!("{:?}", error)))
    }

    pub fn failure_from<E: Debug>(error: E) -> SetPriorityLowResult {
        SetPriorityLowResult::Failure(nsCString::from(format!("{:?}", error)))
    }
}

pub struct SetPriorityLowTask {
    id: nsCString,
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIBITSSetPriorityLowCallback>>>,
    result: AtomicCell<Option<SetPriorityLowResult>>,
}

impl SetPriorityLowTask {
    pub fn new(
        id: nsCString,
        callback: RefPtr<nsIBITSSetPriorityLowCallback>,
    ) -> SetPriorityLowTask {
        SetPriorityLowTask {
            id,
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            result: AtomicCell::new(None),
        }
    }
}

impl Task for SetPriorityLowTask {
    fn run(&self) {
        let result = with_bits_client(|client| -> Result<SetPriorityLowResult, BITSTaskError> {
            let result = client.set_job_priority(Guid_from_nsCString(&self.id)?, false);
            match result? {
                Ok(()) => Ok(SetPriorityLowResult::Success),
                Err(error) => Ok(SetPriorityLowResult::bits_failure_from(error)),
            }
        });
        match result {
            Ok(start_result) => { self.result.store(Some(start_result)); },
            Err(task_error) => {
                self.result.store(Some(SetPriorityLowResult::failure_from(task_error)));
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

        let maybe_callback = maybe_tb_callback.as_ref()
            .ok_or("Unexpected: No callback")
            .and_then(|tc| tc.get_ref().ok_or("Unexpected: Callback on the wrong thread"))
            .map_err(String::from);

        match maybe_callback {
            Ok(callback) => match self.result.swap(None) {
                Some(SetPriorityLowResult::Success) => unsafe { callback.Success() },
                Some(SetPriorityLowResult::BITSFailure(val)) => unsafe { callback.BITSFailure(&*val) },
                Some(SetPriorityLowResult::Failure(val)) => unsafe { callback.Failure(&*val) },
                None => unsafe { callback.Failure(&*nsCString::from("Unexpected: No result")) },
            },
            Err(error_message) => {
                warn!("{}", error_message);
                NS_ERROR_FAILURE
            }
        }.to_result()
    }
}
