/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::BITSInterface;

use bits_client::{BitsJobState, JobStatus, BitsMonitorClient, PipeError};
use crossbeam_utils::atomic::AtomicCell;
use log::{error, warn};
use moz_task::{get_main_thread, is_main_thread};
use nserror::{nsresult, NsresultExt, NS_ERROR_ABORT, NS_ERROR_FAILURE, NS_OK};
use nsstring::{nsACString, nsCString};
use std::ptr;
use xpcom::{
    GetterAddrefs,
    interfaces::{
        nsIEventTarget, nsIProgressEventSink, nsIRequestObserver, nsIThread, nsISupports
    },
    RefPtr, ThreadBoundRefPtr, xpcom, XpCom, xpcom_method,
};

fn transfer_started(
    status: &Result<JobStatus, PipeError>
) -> bool {
    match status.as_ref() {
        Ok(job_status) => match job_status.state {
            BitsJobState::Queued | BitsJobState::Connecting => false,
            _ => true
        },
        Err(_) => true
    }
}

fn transfer_completed(
    status: &Result<JobStatus, PipeError>
) -> bool {
    match status.as_ref() {
        Ok(job_status) => match job_status.state {
            BitsJobState::Error |
            BitsJobState::Transferred |
            BitsJobState::Acknowledged |
            BitsJobState::Cancelled => true,
            _ => false
        },
        Err(_) => true
    }
}

fn status_to_nsresult(
    status: &Result<JobStatus, PipeError>
) -> nsresult {
    match status.as_ref() {
        Ok(job_status) => match job_status.state {
            BitsJobState::Cancelled => NS_ERROR_ABORT,
            BitsJobState::Transferred | BitsJobState::Acknowledged => NS_OK,
            _ => NS_ERROR_FAILURE
        },
        Err(_) => NS_ERROR_FAILURE
    }
}

#[derive(xpcom)]
#[xpimplements(nsIRunnable, nsINamed)]
#[refcnt = "atomic"]
pub struct InitMonitorRunnable {
    bits_interface: AtomicCell<Option<ThreadBoundRefPtr<BITSInterface>>>,
    id: nsCString,
    timeout: u32,
    monitor_client: AtomicCell<Option<BitsMonitorClient>>,
    observer: AtomicCell<Option<ThreadBoundRefPtr<nsIRequestObserver>>>,
    context: AtomicCell<Option<ThreadBoundRefPtr<nsISupports>>>,
    status: AtomicCell<Option<Result<JobStatus, PipeError>>>,
    request_started: AtomicCell<bool>,
    in_error_state: AtomicCell<bool>,
}

impl MonitorRunnable {
    pub fn new(
        bits_interface: RefPtr<BITSInterface>,
        id: nsCString,
        timeout: u32,
        monitor_client: BitsMonitorClient,
        observer: RefPtr<nsIRequestObserver>,
        context: Option<RefPtr<nsISupports>>,
    ) -> RefPtr<MonitorRunnable> {
        MonitorRunnable::allocate(InitMonitorRunnable {
            bits_interface: AtomicCell::new(Some(ThreadBoundRefPtr::new(bits_interface))),
            id,
            timeout,
            monitor_client: AtomicCell::new(Some(monitor_client)),
            observer: AtomicCell::new(Some(ThreadBoundRefPtr::new(observer))),
            context: AtomicCell::new(context.map(ThreadBoundRefPtr::new)),
            status: AtomicCell::new(None),
            request_started: AtomicCell::new(false),
            in_error_state: AtomicCell::new(false),
        })
    }

    pub fn dispatch(&self, thread: RefPtr<nsIThread>) -> Result<(), nsresult> {
        unsafe {
            thread.DispatchFromScript(self.coerce(), nsIEventTarget::DISPATCH_NORMAL as u32)
        }.to_result()
    }

    fn free_mainthread_data(&self) {
        if is_main_thread() {
            // These objects are not safe to free on the main thread
            self.bits_interface.swap(None);
            self.observer.swap(None);
            self.context.swap(None);
        } else {
            error!("Attempting to free data on the main thread, but not on the main thread");
        }
    }

    xpcom_method!(run => Run());
    pub fn run(&self) -> Result<(), nsresult> {
        if self.in_error_state.load() {
            self.free_mainthread_data();
            return Err(NS_ERROR_FAILURE);
        }

        self.try_run().or_else(|error_message| {
            error!("{}", error_message);

            // Once an error has been encountered, we need to free all of our
            // data, but it all needs to be freed on the main thread.
            self.in_error_state.store(true);
            if is_main_thread() {
                self.free_mainthread_data();
                Err(NS_ERROR_FAILURE)
            } else {
                self.dispatch(get_main_thread()?)
            }
        })
    }

    pub fn try_run(&self) -> Result<(), String> {
        if is_main_thread() {
            let status = self.status.swap(None)
                .ok_or("Missing status object")?;

            let tb_observer = self.observer.swap(None)
                .ok_or("Missing observer")?;
            let maybe_tb_context = self.context.swap(None);
            let tb_bits_interface = self.bits_interface.swap(None)
                .ok_or("Missing BITS interface")?;

            // This block bounds the scopes for the objects that were taken
            // out of AtomicCells to ensure that the scope ends before putting
            // the threadbounds back in their cells.
            let maybe_next_thread: Option<RefPtr<nsIThread>> = {
                let observer = tb_observer.get_ref()
                    .ok_or("Observer on the wrong thread")?;
                let context: *const nsISupports = match maybe_tb_context.as_ref() {
                    Some(tb_context) => {
                        tb_context.get_ref().ok_or("Context on the wrong thread")?
                    },
                    None => ptr::null()
                };
                let bits_interface = tb_bits_interface.get_ref()
                    .ok_or("BITS interface on the wrong thread")?;

                if !self.request_started.load() && transfer_started(&status) {
                    self.request_started.store(true);
                    let observer_result = unsafe {
                        observer.OnStartRequest(bits_interface.coerce(), context)
                    }.to_result();
                    if observer_result.is_err() {
                        let mut id = nsCString::new();
                        id.assign(&self.id);
                        if let Err(rv) = bits_interface.cancel_download(id, None, None) {
                            warn!("Failed to cancel download: {}", rv);
                        }
                    }
                }

                if let Ok(job_status) = status.as_ref() {
                    let maybe_progress_event_sink: Option<RefPtr<nsIProgressEventSink>> = {
                        let mut ga = GetterAddrefs::<nsIProgressEventSink>::new();
                        unsafe {
                            if observer.QueryInterface(&nsIProgressEventSink::IID,
                                                       ga.void_ptr()).succeeded() {
                                ga.refptr()
                            } else {
                                None
                            }
                        }
                    };
                    if let Some(progress_event_sink) = maybe_progress_event_sink {
                        let transferred_bytes = job_status.progress.transferred_bytes as i64;
                        let total_bytes = match job_status.progress.total_bytes {
                            Some(total) => total as i64,
                            None => -1i64
                        };
                        unsafe {
                            progress_event_sink.OnProgress(bits_interface.coerce(),
                                                           context,
                                                           transferred_bytes,
                                                           total_bytes);
                        }
                    }
                }

                if transfer_completed(&status) {
                    let transfer_status = status_to_nsresult(&status);
                    let mut job_status = bits_interface.get_status();

                    // If job_status and transfer_status are both errors, do not
                    // update the job_status. It is probably set that way due to
                    // a nsIResult::Cancel(error) call.
                    if job_status.succeeded() != transfer_status.succeeded() {
                        job_status = transfer_status;
                        bits_interface.set_status(transfer_status);
                    }

                    unsafe {
                        observer.OnStopRequest(bits_interface.coerce(),
                                               context,
                                               job_status);
                    }

                    None
                } else {
                    Some(bits_interface.get_monitor_thread().ok_or("Missing monitor thread")?)
                }
            };

            self.observer.store(Some(tb_observer));
            self.context.store(maybe_tb_context);
            self.bits_interface.store(Some(tb_bits_interface));

            match maybe_next_thread {
                Some(next_thread) => {
                    self.dispatch(next_thread)
                        .map_err(|rv| format!("Unable to dispatch to thread: {}", rv))
                },
                None => Ok(())
            }
        } else {
            let mut monitor_client = self.monitor_client.swap(None)
                .ok_or("Missing monitor client")?;
            self.status.store(Some(monitor_client.get_status(self.timeout)));
            self.monitor_client.store(Some(monitor_client));

            let next_thread = get_main_thread()
                .map_err(|rv| format!("Unable to get main thread: {}", rv))?;

            self.dispatch(next_thread)
                .map_err(|rv| format!("Unable to dispatch to thread: {}", rv))
        }
    }

    xpcom_method!(get_name => GetName() -> nsACString);
    fn get_name(&self) -> Result<nsCString, nsresult> {
        Ok(nsCString::from("BITSInterface::Monitor"))
    }
}
