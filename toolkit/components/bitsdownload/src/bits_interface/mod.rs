/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
mod monitor;
mod task;

pub use self::task::BITSTaskError;
use self::{
    monitor::MonitorRunnable,
    task::{
        CancelDownloadTask, ChangeMonitorIntervalTask, CompleteDownloadTask, InitTask,
        MonitorDownloadTask, ResumeDownloadTask, SetPriorityHighTask, SetPriorityLowTask,
        StartDownloadTask, SuspendDownloadTask,
    },
};

use bits_client::{BitsMonitorClient, BitsProxyUsage};
use libc::c_void;
use log::warn;
use moz_task::{create_thread, TaskRunnable};
use nserror::{
    nsresult, NS_ERROR_INVALID_ARG, NS_ERROR_NOT_INITIALIZED, NS_ERROR_NO_AGGREGATION, NS_OK,
};
use nsstring::{nsACString, nsCString};
use std::cell::Cell;
use std::ptr;
use xpcom::{
    ensure_param,
    interfaces::{
        nsIBITS, nsIBITSCancelDownloadCallback, nsIBITSChangeMonitorIntervalCallback,
        nsIBITSCompleteDownloadCallback, nsIBITSInitCallback, nsIBITSMonitorDownloadCallback,
        nsIBITSResumeDownloadCallback, nsIBITSSetPriorityHighCallback,
        nsIBITSSetPriorityLowCallback, nsIBITSStartDownloadCallback,
        nsIBITSSuspendDownloadCallback, nsILoadGroup, nsIRequest, nsIRequestObserver, nsISupports,
        nsIThread, nsLoadFlags, nsProxyUsage,
    },
    nsIID, xpcom, xpcom_method, RefPtr,
};

#[no_mangle]
pub unsafe extern "C" fn nsBITSConstructor(
    outer: *const nsISupports,
    iid: &nsIID,
    result: *mut *mut c_void,
) -> nsresult {
    *result = ptr::null_mut();
    if !outer.is_null() {
        return NS_ERROR_NO_AGGREGATION;
    }

    let service: RefPtr<BITSInterface> = BITSInterface::new();
    service.QueryInterface(iid, result)
}

#[derive(xpcom)]
#[xpimplements(nsIBITS)]
#[refcnt = "nonatomic"]
pub struct InitBITSInterface {
    command_thread: Cell<Option<RefPtr<nsIThread>>>,
    monitor_thread: Cell<Option<RefPtr<nsIThread>>>,
    // The monitor_timeout doubles as a flag for whether the interface has been
    // initialized. A value of 0 indicates that it needs initialization.
    monitor_timeout_ms: Cell<u32>,
    download_pending: Cell<bool>,
    download_id: Cell<Option<nsCString>>,
    download_status: Cell<nsresult>,
}

impl BITSInterface {
    pub fn new() -> RefPtr<BITSInterface> {
        BITSInterface::allocate(InitBITSInterface {
            command_thread: Cell::new(None),
            monitor_thread: Cell::new(None),
            monitor_timeout_ms: Cell::new(0),
            download_pending: Cell::new(false),
            download_id: Cell::new(None),
            download_status: Cell::new(NS_OK),
        })
    }

    fn ensure_initialized(&self) -> Result<(), nsresult> {
        if self.monitor_timeout_ms.get() == 0 {
            return Err(NS_ERROR_NOT_INITIALIZED);
        }
        return Ok(());
    }

    // Returns the handle to the command thread. If it has not been started yet,
    // the thread will be started and a clone of the handle will be both stored.
    fn get_command_thread(&self) -> Result<RefPtr<nsIThread>, nsresult> {
        let mut command_thread = self.command_thread.take();
        if command_thread.is_none() {
            command_thread.replace(create_thread("BITSCommander")?);
        }
        self.command_thread.set(command_thread.clone());
        Ok(command_thread.unwrap())
    }

    // Returns the handle to the monitor thread, if it has been started.
    fn get_monitor_thread(&self) -> Option<RefPtr<nsIThread>> {
        let maybe_monitor_thread = self.monitor_thread.take();
        match maybe_monitor_thread {
            Some(monitor_thread) => {
                self.monitor_thread.set(Some(monitor_thread.clone()));
                Some(monitor_thread)
            }
            None => None,
        }
    }

    // Starts the monitor thread, if it hasn't already been started.
    // This is only called by tasks, which is why the error type returned is a
    // BITSTaskError.
    fn start_monitor_thread(
        &self,
        id: nsCString,
        observer: RefPtr<nsIRequestObserver>,
        context: Option<RefPtr<nsISupports>>,
        monitor_client: BitsMonitorClient,
    ) -> Result<(), BITSTaskError> {
        let monitor_thread = self.monitor_thread.take();
        if monitor_thread.is_some() {
            warn!("Attempted to start monitor thread, but it is already running");
            self.monitor_thread.set(monitor_thread);
            return Err(BITSTaskError::with_message(
                "Monitor thread already running",
            ));
        }
        let monitor_runnable = MonitorRunnable::new(
            RefPtr::new(self),
            id,
            self.monitor_timeout_ms.get(),
            monitor_client,
            observer,
            context,
        );
        let monitor_thread = match create_thread("BITSMonitor") {
            Ok(thread) => thread,
            Err(rv) => {
                return Err(BITSTaskError::with_nsresult(
                    "Unable to create monitor thread",
                    rv,
                ));
            }
        };
        match monitor_runnable.dispatch(monitor_thread.clone()) {
            Ok(()) => {
                self.monitor_thread.set(Some(monitor_thread));
                Ok(())
            }
            Err(rv) => {
                if let Err(rv) = unsafe { monitor_thread.Shutdown() }.to_result() {
                    warn!("Unable to shutdown monitor thread: {}", rv);
                }
                Err(BITSTaskError::with_nsresult(
                    "Unable to dispatch monitor runnable",
                    rv,
                ))
            }
        }
    }

    fn set_download_id(&self, id: nsCString) {
        self.download_id.set(Some(id));
    }

    fn clear_download_id(&self) {
        self.download_id.take();
    }

    fn set_status(&self, status: nsresult) {
        self.download_status.set(status);
    }

    fn set_pending(&self, is_pending: bool) {
        self.download_pending.set(is_pending);
    }

    // Asynchronously shuts down the command and monitor threads. The threads
    // are not shutdown until the event queue is empty, so any tasks that were
    // dispatched before this is called will still run.
    // Leaves None in self.command_thread and self.monitor_thread
    fn shutdown_threads(&self) -> Result<(), nsresult> {
        let mut result = Ok(());
        if let Some(command_thread) = self.command_thread.take() {
            match unsafe { command_thread.AsyncShutdown() }.to_result() {
                Ok(()) => {
                    self.command_thread.take();
                }
                Err(rv) => {
                    result = Err(rv);
                }
            }
        }

        if let Some(monitor_thread) = self.monitor_thread.take() {
            match unsafe { monitor_thread.AsyncShutdown() }.to_result() {
                Ok(()) => {
                    self.command_thread.take();
                }
                Err(rv) => {
                    result = Err(rv);
                }
            }
        }

        result
    }

    xpcom_method!(
        init => Init(
            job_name: *const nsACString,
            save_path_prefix: *const nsACString,
            monitor_timeout_ms: u32,
            callback: *const nsIBITSInitCallback
        )
    );
    fn init(
        &self,
        job_name: &nsACString,
        save_path_prefix: &nsACString,
        monitor_timeout_ms: u32,
        callback: &nsIBITSInitCallback,
    ) -> Result<(), nsresult> {
        if monitor_timeout_ms == 0 {
            return Err(NS_ERROR_INVALID_ARG);
        }
        self.monitor_timeout_ms.set(monitor_timeout_ms);

        let task: Box<InitTask> = Box::new(InitTask::new(
            nsCString::from(job_name),
            nsCString::from(save_path_prefix),
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::init", task)?.dispatch(command_thread)
    }

    // Can't use the xpcom_method macro here, because context could be null, and
    // should not have ensure_param called on it
    #[allow(non_snake_case)]
    unsafe fn StartDownload(
        &self,
        download_url: *const nsACString,
        save_rel_path: *const nsACString,
        proxy: nsProxyUsage,
        update_interval_ms: u32,
        observer: *const nsIRequestObserver,
        context: *const nsISupports,
        callback: *const nsIBITSStartDownloadCallback,
    ) -> nsresult {
        ensure_param!(download_url);
        ensure_param!(save_rel_path);
        // Neither of these next two calls should be needed, but keep them just
        // to be consistent with the xpcom_method macro.
        ensure_param!(proxy);
        ensure_param!(update_interval_ms);
        ensure_param!(observer);
        ensure_param!(callback);

        let context: Option<&nsISupports> = if context.is_null() {
            None
        } else {
            Some(&*context)
        };

        match self.start_download(
            download_url,
            save_rel_path,
            proxy,
            update_interval_ms,
            observer,
            context,
            callback,
        ) {
            Ok(()) => NS_OK,
            Err(error) => error.into(),
        }
    }
    fn start_download(
        &self,
        download_url: &nsACString,
        save_rel_path: &nsACString,
        proxy: nsProxyUsage,
        update_interval_ms: u32,
        observer: &nsIRequestObserver,
        context: Option<&nsISupports>,
        callback: &nsIBITSStartDownloadCallback,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        if update_interval_ms >= self.monitor_timeout_ms.get() {
            return Err(NS_ERROR_INVALID_ARG);
        }
        let proxy = match proxy as i64 {
            nsIBITS::PROXY_NONE => BitsProxyUsage::NoProxy,
            nsIBITS::PROXY_PRECONFIG => BitsProxyUsage::Preconfig,
            nsIBITS::PROXY_AUTODETECT => BitsProxyUsage::AutoDetect,
            _ => return Err(NS_ERROR_INVALID_ARG),
        };

        let task: Box<StartDownloadTask> = Box::new(StartDownloadTask::new(
            RefPtr::new(self),
            nsCString::from(download_url),
            nsCString::from(save_rel_path),
            proxy,
            update_interval_ms,
            RefPtr::new(observer),
            context.map(RefPtr::new),
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::start_download", task)?.dispatch(command_thread)
    }

    // Can't use the xpcom_method macro here, because context could be null, and
    // should not have ensure_param called on it
    #[allow(non_snake_case)]
    unsafe fn MonitorDownload(
        &self,
        id: *const nsACString,
        update_interval_ms: u32,
        observer: *const nsIRequestObserver,
        context: *const nsISupports,
        callback: *const nsIBITSMonitorDownloadCallback,
    ) -> nsresult {
        ensure_param!(id);
        ensure_param!(update_interval_ms); // Shouldn't be needed, but just to be consistent with
                                           // the xpcom_method macro
        ensure_param!(observer);
        ensure_param!(callback);

        let context: Option<&nsISupports> = if context.is_null() {
            None
        } else {
            Some(&*context)
        };

        match self.monitor_download(id, update_interval_ms, observer, context, callback) {
            Ok(()) => NS_OK,
            Err(error) => error.into(),
        }
    }
    fn monitor_download(
        &self,
        id: &nsACString,
        update_interval_ms: u32,
        observer: &nsIRequestObserver,
        context: Option<&nsISupports>,
        callback: &nsIBITSMonitorDownloadCallback,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        if update_interval_ms >= self.monitor_timeout_ms.get() {
            return Err(NS_ERROR_INVALID_ARG);
        }

        let task: Box<MonitorDownloadTask> = Box::new(MonitorDownloadTask::new(
            RefPtr::new(self),
            nsCString::from(id),
            update_interval_ms,
            RefPtr::new(observer),
            context.map(RefPtr::new),
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::monitor_download", task)?.dispatch(command_thread)
    }

    xpcom_method!(
        change_monitor_interval => ChangeMonitorInterval(
            id: *const nsACString,
            update_interval_ms: u32,
            callback: *const nsIBITSChangeMonitorIntervalCallback
        )
    );
    fn change_monitor_interval(
        &self,
        id: &nsACString,
        update_interval_ms: u32,
        callback: &nsIBITSChangeMonitorIntervalCallback,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        if update_interval_ms >= self.monitor_timeout_ms.get() {
            return Err(NS_ERROR_INVALID_ARG);
        }

        let task: Box<ChangeMonitorIntervalTask> = Box::new(ChangeMonitorIntervalTask::new(
            nsCString::from(id),
            update_interval_ms,
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::change_monitor_interval", task)?.dispatch(command_thread)
    }

    xpcom_method!(
        cancel_download_nsIBITS => CancelDownload(
            id: *const nsACString,
            status: nsresult,
            callback: *const nsIBITSCancelDownloadCallback
        )
    );
    #[allow(non_snake_case)]
    fn cancel_download_nsIBITS(
        &self,
        id: &nsACString,
        status: nsresult,
        callback: &nsIBITSCancelDownloadCallback,
    ) -> Result<(), nsresult> {
        self.cancel_download(
            nsCString::from(id),
            Some(status),
            Some(RefPtr::new(callback)),
        )
    }
    xpcom_method!(
        cancel_download_nsIRequest => Cancel(
            status: nsresult
        )
    );
    #[allow(non_snake_case)]
    fn cancel_download_nsIRequest(&self, status: nsresult) -> Result<(), nsresult> {
        let mut id = nsCString::new();
        match self.download_id.take() {
            Some(download_id) => {
                id.assign(&download_id);
                self.download_id.set(Some(download_id));
            }
            None => return Err(NS_ERROR_NOT_INITIALIZED),
        }
        self.cancel_download(id, Some(status), None)
    }

    fn cancel_download(
        &self,
        id: nsCString,
        status: Option<nsresult>,
        callback: Option<RefPtr<nsIBITSCancelDownloadCallback>>,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        if let Some(cancel_reason) = status.clone() {
            if cancel_reason.succeeded() {
                return Err(NS_ERROR_INVALID_ARG);
            }
        }

        let task: Box<CancelDownloadTask> = Box::new(CancelDownloadTask::new(
            RefPtr::new(self),
            id,
            status,
            callback,
        ));

        let command_thread = self.get_command_thread()?;
        // We don't want to use the ? operator here, because we want to shutdown
        // the threads even if the dispatch fails.
        let result = match TaskRunnable::new("BITSInterface::cancel_download", task) {
            Ok(runnable) => runnable.dispatch(command_thread.clone()),
            Err(rv) => Err(rv),
        };

        if let Err(rv) = self.shutdown_threads() {
            warn!("Unable to shutdown threads: {}", rv);
        }

        result
    }

    xpcom_method!(
        set_priority_high => SetPriorityHigh(
            id: *const nsACString,
            callback: *const nsIBITSSetPriorityHighCallback
        )
    );
    fn set_priority_high(
        &self,
        id: &nsACString,
        callback: &nsIBITSSetPriorityHighCallback,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        let task: Box<SetPriorityHighTask> = Box::new(SetPriorityHighTask::new(
            nsCString::from(id),
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::set_priority_high", task)?.dispatch(command_thread)
    }

    xpcom_method!(
        set_priority_low => SetPriorityLow(
            id: *const nsACString,
            callback: *const nsIBITSSetPriorityLowCallback
        )
    );
    fn set_priority_low(
        &self,
        id: &nsACString,
        callback: &nsIBITSSetPriorityLowCallback,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        let task: Box<SetPriorityLowTask> = Box::new(SetPriorityLowTask::new(
            nsCString::from(id),
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::set_priority_low", task)?.dispatch(command_thread)
    }

    xpcom_method!(
        complete_download => CompleteDownload(
            id: *const nsACString,
            callback: *const nsIBITSCompleteDownloadCallback
        )
    );
    fn complete_download(
        &self,
        id: &nsACString,
        callback: &nsIBITSCompleteDownloadCallback,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        let task: Box<CompleteDownloadTask> = Box::new(CompleteDownloadTask::new(
            RefPtr::new(self),
            nsCString::from(id),
            RefPtr::new(callback),
        ));

        let command_thread = self.get_command_thread()?;
        // We don't want to use the ? operator here, because we want to shutdown
        // the threads even if the dispatch fails.
        let result = match TaskRunnable::new("BITSInterface::complete_download", task) {
            Ok(runnable) => runnable.dispatch(command_thread.clone()),
            Err(rv) => Err(rv),
        };

        if let Err(rv) = self.shutdown_threads() {
            warn!("Unable to shutdown threads: {}", rv);
        }

        result
    }

    xpcom_method!(
        suspend_download_nsIBITS => SuspendDownload(
            id: *const nsACString,
            callback: *const nsIBITSSuspendDownloadCallback
        )
    );
    #[allow(non_snake_case)]
    fn suspend_download_nsIBITS(
        &self,
        id: &nsACString,
        callback: &nsIBITSSuspendDownloadCallback,
    ) -> Result<(), nsresult> {
        self.suspend_download(nsCString::from(id), Some(RefPtr::new(callback)))
    }
    xpcom_method!(
        suspend_download_nsIRequest => Suspend()
    );
    #[allow(non_snake_case)]
    fn suspend_download_nsIRequest(&self) -> Result<(), nsresult> {
        let mut id = nsCString::new();
        match self.download_id.take() {
            Some(download_id) => {
                id.assign(&download_id);
                self.download_id.set(Some(download_id));
            }
            None => return Err(NS_ERROR_NOT_INITIALIZED),
        }
        self.suspend_download(id, None)
    }

    fn suspend_download(
        &self,
        id: nsCString,
        callback: Option<RefPtr<nsIBITSSuspendDownloadCallback>>,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        let task: Box<SuspendDownloadTask> = Box::new(SuspendDownloadTask::new(id, callback));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::suspend_download", task)?.dispatch(command_thread)
    }

    xpcom_method!(
        resume_download_nsIBITS => ResumeDownload(
            id: *const nsACString,
            callback: *const nsIBITSResumeDownloadCallback
        )
    );
    #[allow(non_snake_case)]
    fn resume_download_nsIBITS(
        &self,
        id: &nsACString,
        callback: &nsIBITSResumeDownloadCallback,
    ) -> Result<(), nsresult> {
        self.resume_download(nsCString::from(id), Some(RefPtr::new(callback)))
    }
    xpcom_method!(
        resume_download_nsIRequest => Resume()
    );
    #[allow(non_snake_case)]
    fn resume_download_nsIRequest(&self) -> Result<(), nsresult> {
        let mut id = nsCString::new();
        match self.download_id.take() {
            Some(download_id) => {
                id.assign(&download_id);
                self.download_id.set(Some(download_id));
            }
            None => return Err(NS_ERROR_NOT_INITIALIZED),
        }
        self.resume_download(id, None)
    }

    fn resume_download(
        &self,
        id: nsCString,
        callback: Option<RefPtr<nsIBITSResumeDownloadCallback>>,
    ) -> Result<(), nsresult> {
        self.ensure_initialized()?;
        let task: Box<ResumeDownloadTask> = Box::new(ResumeDownloadTask::new(id, callback));

        let command_thread = self.get_command_thread()?;
        TaskRunnable::new("BITSInterface::resume_download", task)?.dispatch(command_thread)
    }

    xpcom_method!(
        get_name => GetName() -> nsACString
    );
    fn get_name(&self) -> Result<nsCString, nsresult> {
        let mut name = nsCString::new();
        if let Some(id) = self.download_id.take() {
            name.assign(&id);
            self.download_id.set(Some(id));
        }
        Ok(name)
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
    fn get_status(&self) -> nsresult {
        self.download_status.get()
    }

    /**
     * As stated in nsIBITS.idl, nsIBITS interfaces are not expected to
     * implement the loadGroup or loadFlags attributes. This implementation
     * provides only null implementations only for these methods.
     */
    // xpcom_method does not make it easy to return null, so it is not used here.
    #[allow(non_snake_case)]
    unsafe fn GetLoadGroup(&self, outparam: *mut *const nsILoadGroup) -> nsresult {
        *outparam = ptr::null();
        NS_OK
    }

    xpcom_method!(
        set_load_group => SetLoadGroup(
            _load_group: *const nsILoadGroup
        )
    );
    fn set_load_group(&self, _load_group: &nsILoadGroup) -> Result<(), nsresult> {
        Ok(())
    }

    xpcom_method!(
        get_load_flags => GetLoadFlags() -> nsLoadFlags
    );
    fn get_load_flags(&self) -> Result<nsLoadFlags, nsresult> {
        Ok(nsIRequest::LOAD_NORMAL as nsLoadFlags)
    }

    xpcom_method!(
        set_load_flags => SetLoadFlags(
            _load_flags: nsLoadFlags
        )
    );
    fn set_load_flags(&self, _load_flags: nsLoadFlags) -> Result<(), nsresult> {
        Ok(())
    }
}
