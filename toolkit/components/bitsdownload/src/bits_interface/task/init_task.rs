/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::client::make_bits_client;

use crossbeam_utils::atomic::AtomicCell;
use log::warn;
use moz_task::Task;
use nserror::{nsresult, NS_ERROR_FAILURE};
use nsstring::nsCString;
use xpcom::{interfaces::nsIBITSInitCallback, RefPtr, ThreadBoundRefPtr};

pub struct InitTask {
    job_name: nsCString,
    save_path_prefix: nsCString,
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIBITSInitCallback>>>,
}

impl InitTask {
    pub fn new(
        job_name: nsCString,
        save_path_prefix: nsCString,
        callback: RefPtr<nsIBITSInitCallback>,
    ) -> InitTask {
        InitTask {
            job_name,
            save_path_prefix,
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
        }
    }
}

impl Task for InitTask {
    fn run(&self) {
        make_bits_client(&self.job_name, &self.save_path_prefix);
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

        let maybe_callback = maybe_tb_callback
            .as_ref()
            .ok_or("Unexpected: No callback")
            .and_then(|tc| {
                tc.get_ref()
                    .ok_or("Unexpected: Callback on the wrong thread")
            })
            .map_err(String::from);

        match maybe_callback {
            Ok(callback) => unsafe { callback.Done() },
            Err(error_message) => {
                warn!("{}", error_message);
                NS_ERROR_FAILURE
            }
        }
        .to_result()
    }
}
