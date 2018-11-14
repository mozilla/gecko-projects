/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use std::convert::From;
use xpcom::interfaces::nsIBits;

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Action {
    StartDownload,
    MonitorDownload,
    Complete,
    Cancel,
    SetMonitorInterval,
    SetPriority,
    Resume,
    Suspend,
}

impl Action {
    pub fn description(&self) -> &'static str {
        match self {
            Action::StartDownload => "starting download",
            Action::MonitorDownload => "monitoring download",
            Action::Complete => "completing download",
            Action::Cancel => "cancelling download",
            Action::SetMonitorInterval => "changing monitor interval",
            Action::SetPriority => "setting download priority",
            Action::Resume => "resuming download",
            Action::Suspend => "suspending download",
        }
    }

    pub fn as_error_code(&self) -> i32 {
        let val = match self {
            Action::StartDownload => nsIBits::ERROR_ACTION_START_DOWNLOAD,
            Action::MonitorDownload => nsIBits::ERROR_ACTION_MONITOR_DOWNLOAD,
            Action::Complete => nsIBits::ERROR_ACTION_COMPLETE,
            Action::Cancel => nsIBits::ERROR_ACTION_CANCEL,
            Action::SetMonitorInterval => nsIBits::ERROR_ACTION_CHANGE_MONITOR_INTERVAL,
            Action::SetPriority => nsIBits::ERROR_ACTION_SET_PRIORITY,
            Action::Resume => nsIBits::ERROR_ACTION_RESUME,
            Action::Suspend => nsIBits::ERROR_ACTION_SUSPEND,
        };
        val as i32
    }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum InterfaceAction {
    StartDownload,
    MonitorDownload,
}

impl From<InterfaceAction> for Action {
    fn from(action: InterfaceAction) -> Action {
        match action {
            InterfaceAction::StartDownload => Action::StartDownload,
            InterfaceAction::MonitorDownload => Action::MonitorDownload,
        }
    }
}

impl InterfaceAction {
    pub fn as_error_code(&self) -> i32 {
        Action::as_error_code(&(self.clone()).into())
    }

    pub fn description(&self) -> &'static str {
        Action::description(&(self.clone()).into())
    }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum RequestAction {
    Complete,
    Cancel,
    SetMonitorInterval,
    SetPriority,
    Resume,
    Suspend,
}

impl From<RequestAction> for Action {
    fn from(action: RequestAction) -> Action {
        match action {
            RequestAction::Complete => Action::Complete,
            RequestAction::Cancel => Action::Cancel,
            RequestAction::SetMonitorInterval => Action::SetMonitorInterval,
            RequestAction::SetPriority => Action::SetPriority,
            RequestAction::Resume => Action::Resume,
            RequestAction::Suspend => Action::Suspend,
        }
    }
}

impl RequestAction {
    pub fn as_error_code(&self) -> i32 {
        Action::as_error_code(&(self.clone()).into())
    }

    pub fn description(&self) -> &'static str {
        Action::description(&(self.clone()).into())
    }
}
