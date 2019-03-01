/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::{
    error::BITSTaskError,
    client::with_bits_client,
    string::nsCString_to_OsString
};
use bits_client::{BitsMonitorClient, BitsProxyUsage, Guid};
use bits_interface::BITSInterface;
use crossbeam_utils::atomic::AtomicCell;
use log::warn;
use moz_task::Task;
use nserror::{nsresult, NsresultExt, NS_ERROR_FAILURE, NS_OK};
use nsstring::nsCString;
use std::fmt::Debug;
use xpcom::{
    interfaces::{nsIBITSStartDownloadCallback, nsIRequestObserver, nsISupports},
    RefPtr, ThreadBoundRefPtr
};

pub enum StartDownloadResult {
    Success(nsCString, BitsMonitorClient),
    BITSFailure(nsCString),
    Failure(nsCString),
}

impl StartDownloadResult {
    pub fn success_from(guid: Guid, client: BitsMonitorClient) -> StartDownloadResult {
        StartDownloadResult::Success(nsCString::from(guid.to_string()), client)
    }

    pub fn bits_failure_from<E: Debug>(error: E) -> StartDownloadResult {
        StartDownloadResult::BITSFailure(nsCString::from(format!("{:?}", error)))
    }

    pub fn failure_from<E: Debug>(error: E) -> StartDownloadResult {
        StartDownloadResult::Failure(nsCString::from(format!("{:?}", error)))
    }
}

pub struct StartDownloadTask {
    bits_interface: AtomicCell<Option<ThreadBoundRefPtr<BITSInterface>>>,
    download_url: nsCString,
    save_rel_path: nsCString,
    proxy: BitsProxyUsage,
    update_interval_ms: u32,
    observer: AtomicCell<Option<ThreadBoundRefPtr<nsIRequestObserver>>>,
    context: AtomicCell<Option<ThreadBoundRefPtr<nsISupports>>>,
    callback: AtomicCell<Option<ThreadBoundRefPtr<nsIBITSStartDownloadCallback>>>,
    result: AtomicCell<Option<StartDownloadResult>>,
}

impl StartDownloadTask {
    pub fn new(
        bits_interface: RefPtr<BITSInterface>,
        download_url: nsCString,
        save_rel_path: nsCString,
        proxy: BitsProxyUsage,
        update_interval_ms: u32,
        observer: RefPtr<nsIRequestObserver>,
        context: Option<RefPtr<nsISupports>>,
        callback: RefPtr<nsIBITSStartDownloadCallback>,
    ) -> StartDownloadTask {
        StartDownloadTask {
            bits_interface: AtomicCell::new(Some(ThreadBoundRefPtr::new(bits_interface))),
            download_url,
            save_rel_path,
            proxy,
            update_interval_ms,
            observer: AtomicCell::new(Some(ThreadBoundRefPtr::new(observer))),
            context: AtomicCell::new(context.map(ThreadBoundRefPtr::new)),
            callback: AtomicCell::new(Some(ThreadBoundRefPtr::new(callback))),
            result: AtomicCell::new(None),
        }
    }
}

impl Task for StartDownloadTask {
    fn run(&self) {
        let result = with_bits_client(|client| -> Result<StartDownloadResult, BITSTaskError> {
            let result = client.start_job(nsCString_to_OsString(&self.download_url)?,
                                          nsCString_to_OsString(&self.save_rel_path)?,
                                          self.proxy,
                                          self.update_interval_ms);
            match result? {
                Ok((start_success, monitor_client)) => {
                    Ok(StartDownloadResult::success_from(start_success.guid, monitor_client))
                },
                Err(error) => Ok(StartDownloadResult::bits_failure_from(error)),
            }
        });
        match result {
            Ok(start_result) => { self.result.store(Some(start_result)); },
            Err(task_error) => {
                self.result.store(Some(StartDownloadResult::failure_from(task_error)));
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
        // The observer and context are also an nsXPCWrappedJS that aren't safe
        // to release on the worker thread.
        let maybe_tb_observer = self.observer.swap(None);
        let maybe_tb_context = self.context.swap(None);

        let maybe_callback = maybe_tb_callback.as_ref()
            .ok_or("Unexpected: No callback")
            .and_then(|tc| tc.get_ref().ok_or("Unexpected: Callback on the wrong thread"))
            .map_err(String::from);
        let maybe_bits_interface = maybe_tb_interface.as_ref()
            .ok_or("Unexpected: No BITS interface")
            .and_then(|ti| ti.get_ref().ok_or("Unexpected: BITS interface on wrong thread"))
            .map_err(String::from);
        let maybe_observer = maybe_tb_observer.as_ref()
            .ok_or("Unexpected: No observer")
            .and_then(|to| to.get_ref().ok_or("Unexpected: Observer on wrong thread"))
            .map_err(String::from);
        let maybe_context: Result<Option<&nsISupports>, String> = match maybe_tb_context.as_ref() {
            Some(tb_context) => {
                tb_context.get_ref().ok_or("Unexpected: Observer context on wrong thread")
                    .map(Option::from).map_err(String::from)
            },
            None => Ok(None)
        };

        let callback_result: Result<nsresult, String> = match self.result.swap(None) {
            Some(StartDownloadResult::Success(guid, monitor_client)) => {
                let monitor_result = maybe_bits_interface.and_then(|bits_interface| {
                    let observer = maybe_observer?;
                    let context = maybe_context?;
                    let guid_string = nsCString::from(&guid.to_string());
                    let monitor_result =
                        bits_interface.start_monitor_thread(guid_string,
                                                            RefPtr::new(&observer),
                                                            context.map(RefPtr::new),
                                                            monitor_client)
                                      .map_err(|error| error.to_string());
                    if monitor_result.is_ok() {
                        bits_interface.set_pending(true);
                        bits_interface.set_download_id(nsCString::from(&guid.to_string()));
                        bits_interface.set_status(NS_OK);
                    } else {
                        bits_interface.set_status(NS_ERROR_FAILURE);
                    }
                    monitor_result
                });
                maybe_callback.map(|callback| {
                    match monitor_result {
                        Ok(()) => unsafe { callback.Success(&*guid) }
                        Err(error_message) => {
                            let error_message = nsCString::from(error_message);
                            unsafe { callback.Failure(&*error_message) }
                        }
                    }
                })
            },
            Some(StartDownloadResult::BITSFailure(error_message)) => {
                if let Ok(bits_interface) = maybe_bits_interface {
                    bits_interface.set_status(NS_ERROR_FAILURE);
                }
                maybe_callback.map(|callback| unsafe { callback.BITSFailure(&*error_message) })
            },
            Some(StartDownloadResult::Failure(error_message)) => {
                if let Ok(bits_interface) = maybe_bits_interface {
                    bits_interface.set_status(NS_ERROR_FAILURE);
                }
                maybe_callback.map(|callback| unsafe { callback.Failure(&*error_message) })
            },
            None => {
                if let Ok(bits_interface) = maybe_bits_interface {
                    bits_interface.set_status(NS_ERROR_FAILURE);
                }
                maybe_callback.map(|callback| unsafe {
                    callback.Failure(&*nsCString::from("Unexpected: No result"))
                })
            },
        };

        match callback_result {
            Ok(rv) => {
                rv.to_result()
            },
            Err(error_message) => {
                warn!("{}", error_message);
                Err(NS_ERROR_FAILURE)
            }
        }
    }
}
