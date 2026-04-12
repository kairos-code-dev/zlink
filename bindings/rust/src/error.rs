use std::ffi::CStr;
use std::fmt;

use crate::ffi;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum SubmitResult {
    Ok = 0,
    Backpressured = 1,
    NotConnected = 2,
    BadRoute = 3,
    MessageTooLarge = 4,
    Terminated = 5,
    InvalidArgument = 6,
    NotSupported = 7,
    InvalidState = 8,
    ThreadViolation = 9,
    OutOfMemory = 10,
    SeqExhausted = 11,
    InternalError = 12,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RequestResult {
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RecvResult {
    Ok = 0,
    NoData = 201,
    Busy = 202,
    Terminated = 203,
    InvalidHandle = 204,
    NotSupported = 205,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum HandlerResult {
    Ok = 0,
    InvalidArgument = 301,
    Busy = 302,
    NotSupported = 303,
    Deadlock = 304,
    InvalidHandle = 305,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum CloseResult {
    Ok = 0,
    Busy = 401,
    Shutdown = 402,
    InvalidHandle = 403,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum BindResult {
    Ok = 0,
    InvalidArgument = 501,
    AddrInUse = 502,
    NotSupported = 503,
    InvalidHandle = 504,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ConnectResult {
    Ok = 0,
    InvalidArgument = 601,
    NotSupported = 602,
    InvalidHandle = 603,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ConfigResult {
    Ok = 0,
    InvalidHandle = 701,
    InvalidArgument = 702,
    NotSupported = 703,
}

/// Error type for all zlink operations.
///
/// Wraps the native errno code and a human-readable description from the core
/// library. Binding-level validation errors (e.g. routing-id length overflow)
/// use dedicated constructors so they are distinguishable from native errors.
#[derive(Debug, Clone)]
pub struct ZlinkError {
    code: i32,
    message: String,
}

impl ZlinkError {
    /// Capture the current thread-local native error state.
    pub(crate) fn last() -> Self {
        let code = unsafe { ffi::zlink_errno() };
        let msg = unsafe {
            let ptr = ffi::zlink_strerror(code);
            if ptr.is_null() {
                String::from("unknown error")
            } else {
                CStr::from_ptr(ptr).to_string_lossy().into_owned()
            }
        };
        Self { code, message: msg }
    }

    /// Create a binding-level validation error.
    pub(crate) fn validation(message: impl Into<String>) -> Self {
        Self {
            code: libc::EINVAL,
            message: message.into(),
        }
    }

    /// Create an error for an invalid state / programming error.
    pub(crate) fn state(message: impl Into<String>) -> Self {
        Self {
            code: libc::EINVAL,
            message: message.into(),
        }
    }

    pub(crate) fn native(code: i32, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }

    /// The raw errno code.
    pub fn code(&self) -> i32 {
        self.code
    }

    pub fn internal_errno(&self) -> i32 {
        self.code
    }
}

impl fmt::Display for ZlinkError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "zlink error {}: {}", self.code, self.message)
    }
}

impl std::error::Error for ZlinkError {}

/// Check a public zlink result code; any non-zero value becomes an error.
pub(crate) fn check_rc(rc: i32) -> Result<(), ZlinkError> {
    if rc != 0 {
        Err(ZlinkError::last())
    } else {
        Ok(())
    }
}

pub type SubmitError = ZlinkError;
pub type RequestError = ZlinkError;
pub type RecvError = ZlinkError;
pub type HandlerError = ZlinkError;
pub type CloseError = ZlinkError;
pub type BindError = ZlinkError;
pub type ConnectError = ZlinkError;
pub type ConfigError = ZlinkError;
