/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::{
    action::{
        Action::{Complete, Cancel, SetMonitorInterval, SetPriority, Resume, Suspend},
        InterfaceAction
    },
    error::{
        ErrorStage::{MainThread, Pretask},
        ErrorType::{
            FailedToDispatchRunnable, FailedToStartThread,
            InvalidArgument, TransferAlreadyComplete, OperationAlreadyInProgress
        },
    },
    monitor::MonitorRunnable,
    task::{
        CancelTask, ChangeMonitorIntervalTask, CompleteTask, ResumeTask, SetPriorityTask,
        SuspendTask, Priority,
    },
    BitsInterface, BitsTaskError,
};
use nsIBitsRequest_method; // From xpcom_method.rs

use bits_client::{BitsMonitorClient, Guid};
use log::{error, info, warn};
use moz_task::create_thread;
use nserror::{nsresult, NS_ERROR_NOT_IMPLEMENTED, NS_OK};
use nsstring::{nsACString, nsCString};
use std::{cell::Cell, fmt, ptr};
use xpcom::{
    ensure_param,
    interfaces::{
        nsIBitsCallback, nsILoadGroup, nsIProgressEventSink, nsIRequestObserver, nsISupports,
        nsIThread, nsLoadFlags,
    },
    xpcom, xpcom_method, GetterAddrefs, RefPtr, XpCom,
};

/**
 * This structure exists to resolve a race condition. If cancel is called, we
 * might not know if it succeeded or failed until after MonitorRunnable calls
 * on_stop. This structure allows us to keep track of whether a cancel action is
 * in progress and, if it is, to delay the on_stop call until after we know the
 * result of the cancel call.
 * This is necessary to determine whether the request status should be set to
 * the one passed to cancel() before calling onStopRequest.
 */
#[derive(Clone, Copy, PartialEq)]
enum CancelAction {
    NotInProgress,
    InProgress(Option<nsresult>),
    RequestEndedWhileInProgress(Option<nsresult>),
}

#[derive(xpcom)]
#[xpimplements(nsIBitsRequest)]
#[refcnt = "nonatomic"]
pub struct InitBitsRequest {
    bits_id: Guid,
    bits_interface: RefPtr<BitsInterface>,
    download_pending: Cell<bool>,
    download_status: Cell<nsresult>,
    // This option will be None only after OnStopRequest has been fired.
    monitor_thread: Cell<Option<RefPtr<nsIThread>>>,
    monitor_timeout_ms: u32,
    observer: RefPtr<nsIRequestObserver>,
    context: Option<RefPtr<nsISupports>>,
    // started indicates whether or not OnStartRequest has been fired.
    started: Cell<bool>,
    // finished indicates whether or not we have called
    // BitsInterface::dec_request_count() to (assuming that there are no other
    // requests) shutdown the command thread.
    finished: Cell<bool>,
    cancel_action: Cell<CancelAction>,
}

impl fmt::Debug for BitsRequest {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "BitsRequest {{ id: {} }}", self.bits_id)
    }
}

impl BitsRequest {
    pub fn new(
        id: Guid,
        bits_interface: RefPtr<BitsInterface>,
        monitor_timeout_ms: u32,
        observer: RefPtr<nsIRequestObserver>,
        context: Option<RefPtr<nsISupports>>,
        monitor_client: BitsMonitorClient,
        action: InterfaceAction,
    ) -> Result<RefPtr<BitsRequest>, BitsTaskError> {
        let monitor_thread = create_thread("BitsMonitor").map_err(|rv| {
            BitsTaskError::from_nsresult(FailedToStartThread, action.into(), MainThread, rv)
        })?;

        // BitsRequest.drop() will call dec_request_count
        bits_interface.inc_request_count();
        let request = BitsRequest::allocate(InitBitsRequest {
            bits_id: id.clone(),
            bits_interface,
            download_pending: Cell::new(true),
            download_status: Cell::new(NS_OK),
            monitor_thread: Cell::new(Some(monitor_thread.clone())),
            monitor_timeout_ms,
            observer,
            context,
            started: Cell::new(false),
            finished: Cell::new(false),
            cancel_action: Cell::new(CancelAction::NotInProgress),
        });

        let monitor_runnable =
            MonitorRunnable::new(request.clone(), id, monitor_timeout_ms, monitor_client);

        if let Err(rv) = monitor_runnable.dispatch(monitor_thread.clone()) {
            request.shutdown_monitor_thread();
            let error_type = FailedToDispatchRunnable;
            return Err(BitsTaskError::from_nsresult(error_type, action.into(), MainThread, rv));
        }

        Ok(request)
    }

    pub fn get_monitor_thread(&self) -> Option<RefPtr<nsIThread>> {
        let monitor_thread = self.monitor_thread.take();
        self.monitor_thread.set(monitor_thread.clone());
        monitor_thread
    }

    fn has_monitor_thread(&self) -> bool {
        let maybe_monitor_thread = self.monitor_thread.take();
        let transferred = maybe_monitor_thread.is_some();
        self.monitor_thread.set(maybe_monitor_thread);
        transferred
    }

    /**
     * If this returns an true, it means that:
     *   - The monitor thread and monitor runnable may have been shut down
     *   - The BITS job is not in the TRANSFERRING state
     *   - The download either completed, failed, or was cancelled
     *   - The BITS job may or may not still need complete() or cancel() to be
     *     called on it
     */
    fn request_has_transferred(&self) -> bool {
        if self.request_has_completed() {
            return true;
        }
        !self.has_monitor_thread()
    }

    /**
     * If this returns an error, it means that:
     *   - complete() or cancel() has been called on the BITS job.
     *   - BitsInterface::dec_request_count has already been called.
     *   - The BitsClient object that this request was using may have been
     *     dropped.
     */
    fn request_has_completed(&self) -> bool {
        self.finished.get()
    }

    fn shutdown_monitor_thread(&self) {
        if let Some(monitor_thread) = self.monitor_thread.take() {
            if let Err(rv) = unsafe { monitor_thread.AsyncShutdown() }.to_result() {
                warn!("Failed to shut down monitor thread: {:?}", rv);
                warn!("Releasing reference to thread that failed to shut down!");
            }
        }
    }

    /**
     * To be called when the transfer starts. Fires observer.OnStartRequest exactly once.
     */
    pub fn on_start(&self) {
        if self.started.get() {
            return;
        }
        self.started.set(true);
        if let Err(rv) = unsafe { self.observer.OnStartRequest(self.coerce()) }.to_result() {
            info!(
                "Cancelling download because OnStartRequest rejected with: {:?}",
                rv
            );
            if let Err(rv) = self.cancel(None, None) {
                warn!("Failed to cancel download: {:?}", rv);
            }
        }
    }

    pub fn on_progress(&self, transferred_bytes: i64, total_bytes: i64) {
        let maybe_progress_event_sink: Option<RefPtr<nsIProgressEventSink>> = {
            let mut ga = GetterAddrefs::<nsIProgressEventSink>::new();
            unsafe {
                if self
                    .observer
                    .QueryInterface(&nsIProgressEventSink::IID, ga.void_ptr())
                    .succeeded()
                {
                    ga.refptr()
                } else {
                    None
                }
            }
        };

        if let Some(progress_event_sink) = maybe_progress_event_sink {
            let context: *const nsISupports = match self.context.as_ref() {
                Some(context) => &**context,
                None => ptr::null(),
            };
            unsafe {
                progress_event_sink.OnProgress(
                    self.coerce(),
                    context,
                    transferred_bytes,
                    total_bytes,
                );
            }
        }
    }

    /**
     * To be called when the transfer stops (fails or completes). Fires
     * observer. OnStopRequest exactly once. If this is called before a cancel
     * action has been resolved, the observer.OnStopRequest call will be delayed
     * until the cancel action has resolved so that the correct status code will
     * be used.
     */
    pub fn on_stop(&self, status: Option<nsresult>) {
        if !self.has_monitor_thread() {
            // If the request has already stopped, don't stop it again
            return;
        }

        if let Some(status) = status {
            self.download_status.set(status);
        }

        match self.cancel_action.get() {
            CancelAction::InProgress(maybe_status)
            | CancelAction::RequestEndedWhileInProgress(maybe_status) => {
                info!("Deferring OnStopRequest until Cancel Task completes");
                self.cancel_action
                    .set(CancelAction::RequestEndedWhileInProgress(maybe_status));
            }
            CancelAction::NotInProgress => {
                self.download_pending.set(false);
                self.shutdown_monitor_thread();

                unsafe {
                    self.observer
                        .OnStopRequest(self.coerce(), self.download_status.get())
                };
            }
        }
    }

    /**
     * To be called after a cancel or complete task has run successfully. If
     * this is the only BitsRequest running, this will shut down BitsInterface's
     * command thread, destroying the BitsClient.
     */
    pub fn on_finished(&self) {
        if self.finished.get() {
            return;
        }
        self.finished.set(true);
        println!("XXXbytesized: Request Finished");
        self.bits_interface.dec_request_count();
    }

    xpcom_method!(
        get_bits_id_nsIBitsRequest => GetBitsId() -> nsACString
    );
    #[allow(non_snake_case)]
    fn get_bits_id_nsIBitsRequest(&self) -> Result<nsCString, nsresult> {
        Ok(self.get_bits_id())
    }
    pub fn get_bits_id(&self) -> nsCString {
        nsCString::from(self.bits_id.to_string())
    }

    xpcom_method!(
        get_name => GetName() -> nsACString
    );
    fn get_name(&self) -> Result<nsCString, nsresult> {
        Ok(self.get_bits_id())
    }

    xpcom_method!(
        is_pending => IsPending() -> bool
    );
    fn is_pending(&self) -> Result<bool, nsresult> {
        Ok(self.download_pending.get())
    }

    xpcom_method!(
        get_status_nsIRequest => GetStatus() -> nsresult
    );
    #[allow(non_snake_case)]
    fn get_status_nsIRequest(&self) -> Result<nsresult, nsresult> {
        Ok(self.get_status())
    }
    pub fn get_status(&self) -> nsresult {
        self.download_status.get()
    }

    nsIBitsRequest_method!(
        [SetMonitorInterval]
        change_monitor_interval => ChangeMonitorInterval(
            update_interval_ms: u32,
        )
    );
    fn change_monitor_interval(
        &self,
        update_interval_ms: u32,
        callback: &nsIBitsCallback,
    ) -> Result<(), BitsTaskError> {
        if update_interval_ms == 0 || update_interval_ms >= self.monitor_timeout_ms {
            return Err(BitsTaskError::new(InvalidArgument, SetMonitorInterval, Pretask));
        }
        if self.request_has_transferred() {
            return Err(BitsTaskError::new(TransferAlreadyComplete, SetMonitorInterval, Pretask));
        }

        let task: Box<ChangeMonitorIntervalTask> = Box::new(ChangeMonitorIntervalTask::new(
            RefPtr::new(self),
            self.bits_id.clone(),
            update_interval_ms,
            RefPtr::new(callback),
        ));

        self.bits_interface.dispatch_runnable_to_command_thread(
            task,
            "BitsRequest::change_monitor_interval",
            SetMonitorInterval,
        )
    }

    nsIBitsRequest_method!(
        [Cancel]
        cancel_nsIBitsRequest => CancelAsync(
            status: nsresult,
        )
    );
    #[allow(non_snake_case)]
    fn cancel_nsIBitsRequest(
        &self,
        status: nsresult,
        callback: &nsIBitsCallback,
    ) -> Result<(), BitsTaskError> {
        self.cancel(Some(status), Some(RefPtr::new(callback)))
    }
    xpcom_method!(
        cancel_nsIRequest => Cancel(
            status: nsresult
        )
    );
    #[allow(non_snake_case)]
    fn cancel_nsIRequest(&self, status: nsresult) -> Result<(), BitsTaskError> {
        self.cancel(Some(status), None)
    }

    fn cancel(
        &self,
        status: Option<nsresult>,
        callback: Option<RefPtr<nsIBitsCallback>>,
    ) -> Result<(), BitsTaskError> {
        if let Some(cancel_reason) = status.as_ref() {
            if cancel_reason.succeeded() {
                return Err(BitsTaskError::new(InvalidArgument, Cancel, Pretask));
            }
        }
        if self.request_has_completed() {
            return Err(BitsTaskError::new(TransferAlreadyComplete, Cancel, Pretask));
        }

        if self.cancel_action.get() != CancelAction::NotInProgress {
            return Err(BitsTaskError::new(OperationAlreadyInProgress, Cancel, Pretask));
        }
        self.cancel_action.set(CancelAction::InProgress(status));

        let task: Box<CancelTask> = Box::new(CancelTask::new(
            RefPtr::new(self),
            self.bits_id.clone(),
            callback,
        ));

        self.bits_interface.dispatch_runnable_to_command_thread(
            task,
            "BitsRequest::cancel",
            Cancel,
        )
    }

    pub fn finish_cancel_action(&self, cancelled_successfully: bool) {
        let (maybe_status, request_stop_is_pending) = match self.cancel_action.get() {
            CancelAction::InProgress(maybe_status) => (maybe_status, false),
            CancelAction::RequestEndedWhileInProgress(maybe_status) => (maybe_status, true),
            CancelAction::NotInProgress => {
                error!("End of cancel action, but cancel action is not in progress!");
                return;
            }
        };
        info!("Finishing cancel action. cancel success = {}", cancelled_successfully);
        println!("XXXbytesized: Finishing cancel action. cancel success = {}", cancelled_successfully);
        if cancelled_successfully {
            if let Some(status) = maybe_status {
                self.download_status.set(status);
            }
        }
        self.cancel_action.set(CancelAction::NotInProgress);

        if request_stop_is_pending {
            info!("Running deferred OnStopRequest");
            self.on_stop(None);
        }

        if cancelled_successfully {
            self.on_finished();
        }
    }

    nsIBitsRequest_method!(
        [SetPriority]
        set_priority_high => SetPriorityHigh()
    );
    fn set_priority_high(&self, callback: &nsIBitsCallback) -> Result<(), BitsTaskError> {
        self.set_priority(Priority::High, callback)
    }

    nsIBitsRequest_method!(
        [SetPriority]
        set_priority_low => SetPriorityLow()
    );
    fn set_priority_low(&self, callback: &nsIBitsCallback) -> Result<(), BitsTaskError> {
        self.set_priority(Priority::Low, callback)
    }

    fn set_priority(
        &self,
        priority: Priority,
        callback: &nsIBitsCallback
    ) -> Result<(), BitsTaskError> {
        if self.request_has_transferred() {
            return Err(BitsTaskError::new(TransferAlreadyComplete, SetPriority, Pretask));
        }

        let task: Box<SetPriorityTask> = Box::new(SetPriorityTask::new(
            RefPtr::new(self),
            self.bits_id.clone(),
            priority,
            RefPtr::new(callback),
        ));

        self.bits_interface.dispatch_runnable_to_command_thread(
            task,
            "BitsRequest::set_priority",
            SetPriority,
        )
    }

    nsIBitsRequest_method!(
        [Complete]
        complete => Complete()
    );
    fn complete(&self, callback: &nsIBitsCallback) -> Result<(), BitsTaskError> {
        if self.request_has_completed() {
            return Err(BitsTaskError::new(TransferAlreadyComplete, Complete, Pretask));
        }

        let task: Box<CompleteTask> = Box::new(CompleteTask::new(
            RefPtr::new(self),
            self.bits_id.clone(),
            RefPtr::new(callback),
        ));

        self.bits_interface.dispatch_runnable_to_command_thread(
            task,
            "BitsRequest::complete",
            Complete,
        )
    }

    nsIBitsRequest_method!(
        [Suspend]
        suspend_nsIBitsRequest => SuspendAsync()
    );
    #[allow(non_snake_case)]
    fn suspend_nsIBitsRequest(&self, callback: &nsIBitsCallback) -> Result<(), BitsTaskError> {
        self.suspend(Some(RefPtr::new(callback)))
    }
    xpcom_method!(
        suspend_nsIRequest => Suspend()
    );
    #[allow(non_snake_case)]
    fn suspend_nsIRequest(&self) -> Result<(), BitsTaskError> {
        self.suspend(None)
    }

    fn suspend(&self, callback: Option<RefPtr<nsIBitsCallback>>) -> Result<(), BitsTaskError> {
        if self.request_has_transferred() {
            return Err(BitsTaskError::new(TransferAlreadyComplete, Suspend, Pretask));
        }

        let task: Box<SuspendTask> = Box::new(SuspendTask::new(
            RefPtr::new(self),
            self.bits_id.clone(),
            callback
        ));

        self.bits_interface.dispatch_runnable_to_command_thread(
            task,
            "BitsRequest::suspend",
            Suspend,
        )
    }

    nsIBitsRequest_method!(
        [Resume]
        resume_nsIBitsRequest => ResumeAsync()
    );
    #[allow(non_snake_case)]
    fn resume_nsIBitsRequest(&self, callback: &nsIBitsCallback) -> Result<(), BitsTaskError> {
        self.resume(Some(RefPtr::new(callback)))
    }
    xpcom_method!(
        resume_nsIRequest => Resume()
    );
    #[allow(non_snake_case)]
    fn resume_nsIRequest(&self) -> Result<(), BitsTaskError> {
        self.resume(None)
    }

    fn resume(&self, callback: Option<RefPtr<nsIBitsCallback>>) -> Result<(), BitsTaskError> {
        if self.request_has_transferred() {
            return Err(BitsTaskError::new(TransferAlreadyComplete, Resume, Pretask));
        }

        let task: Box<ResumeTask> = Box::new(ResumeTask::new(
            RefPtr::new(self),
            self.bits_id.clone(),
            callback
        ));

        self.bits_interface.dispatch_runnable_to_command_thread(
            task,
            "BitsRequest::resume",
            Resume,
        )
    }

    /**
     * As stated in nsIBits.idl, nsIBits interfaces are not expected to
     * implement the loadGroup or loadFlags attributes. This implementation
     * provides only null implementations only for these methods.
     */
    xpcom_method!(
        get_load_group => GetLoadGroup() -> *const nsILoadGroup
    );
    fn get_load_group(&self) -> Result<RefPtr<nsILoadGroup>, nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(
        set_load_group => SetLoadGroup(
            _load_group: *const nsILoadGroup
        )
    );
    fn set_load_group(&self, _load_group: &nsILoadGroup) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(
        get_load_flags => GetLoadFlags() -> nsLoadFlags
    );
    fn get_load_flags(&self) -> Result<nsLoadFlags, nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(
        set_load_flags => SetLoadFlags(
            _load_flags: nsLoadFlags
        )
    );
    fn set_load_flags(&self, _load_flags: nsLoadFlags) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }
}

impl Drop for BitsRequest {
    fn drop(&mut self) {
        // Make sure that the monitor thread gets cleaned up.
        self.shutdown_monitor_thread();
        // Make sure we tell BitsInterface that we are done with the command thread.
        self.on_finished();
    }
}
