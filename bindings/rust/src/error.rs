use std::ffi::CStr;
use std::fmt;

use crate::ffi;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum SubmitResult {
    Ok = 0,
    Backpressured = 1,
    NotConnected = 2,
    NotFound = 3,
    Terminated = 4,
    InvalidHandle = 5,
    InvalidArgument = 6,
    NotSupported = 7,
    InvalidState = 8,
    ThreadViolation = 9,
    OutOfMemory = 10,
    SeqExhausted = 11,
    InternalError = 12,
    NotAdmitted = 13,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum RequestResult {
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104,
    InternalError = 105,
    Rejected = 106,
    Conflict = 107,
    Busy = 108,
    NotConnected = 109,
    InvalidArgument = 110,
    InvalidState = 111,
    NotSupported = 112,
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
    InternalError = 206,
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
    InternalError = 306,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum CloseResult {
    Ok = 0,
    Busy = 401,
    Shutdown = 402,
    InvalidHandle = 403,
    InternalError = 404,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum BindResult {
    Ok = 0,
    InvalidArgument = 501,
    AddrInUse = 502,
    NotSupported = 503,
    InvalidHandle = 504,
    InternalError = 505,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ConnectResult {
    Ok = 0,
    InvalidArgument = 601,
    NotSupported = 602,
    InvalidHandle = 603,
    InternalError = 604,
    NotFound = 605,
    Conflict = 606,
    Busy = 607,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ConfigResult {
    Ok = 0,
    InvalidHandle = 701,
    InvalidArgument = 702,
    NotSupported = 703,
    InternalError = 704,
    InvalidState = 705,
    NotFound = 706,
}

macro_rules! define_error_type {
    ($name:ident, $result:ident, $variant:ident) => {
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub struct $name {
            pub code: $result,
            pub internal_errno: i32,
        }

        impl $name {
            pub const fn new(code: $result, internal_errno: i32) -> Self {
                Self {
                    code,
                    internal_errno,
                }
            }

            pub const fn code(&self) -> $result {
                self.code
            }

            pub const fn internal_errno(&self) -> i32 {
                self.internal_errno
            }
        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(
                    f,
                    "{}(code={:?}, internal_errno={})",
                    stringify!($name),
                    self.code,
                    self.internal_errno
                )
            }
        }

        impl std::error::Error for $name {}

        impl From<$name> for ZlinkError {
            fn from(value: $name) -> Self {
                ZlinkError::$variant(value)
            }
        }
    };
}

define_error_type!(SubmitError, SubmitResult, Submit);
define_error_type!(RequestError, RequestResult, Request);
define_error_type!(RecvError, RecvResult, Recv);
define_error_type!(HandlerError, HandlerResult, Handler);
define_error_type!(CloseError, CloseResult, Close);
define_error_type!(BindError, BindResult, Bind);
define_error_type!(ConnectError, ConnectResult, Connect);
define_error_type!(ConfigError, ConfigResult, Config);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ZlinkError {
    Submit(SubmitError),
    Request(RequestError),
    Recv(RecvError),
    Handler(HandlerError),
    Close(CloseError),
    Bind(BindError),
    Connect(ConnectError),
    Config(ConfigError),
}

impl ZlinkError {
    pub fn code(&self) -> i32 {
        match self {
            Self::Submit(err) => err.code as i32,
            Self::Request(err) => err.code as i32,
            Self::Recv(err) => err.code as i32,
            Self::Handler(err) => err.code as i32,
            Self::Close(err) => err.code as i32,
            Self::Bind(err) => err.code as i32,
            Self::Connect(err) => err.code as i32,
            Self::Config(err) => err.code as i32,
        }
    }

    pub fn internal_errno(&self) -> i32 {
        match self {
            Self::Submit(err) => err.internal_errno,
            Self::Request(err) => err.internal_errno,
            Self::Recv(err) => err.internal_errno,
            Self::Handler(err) => err.internal_errno,
            Self::Close(err) => err.internal_errno,
            Self::Bind(err) => err.internal_errno,
            Self::Connect(err) => err.internal_errno,
            Self::Config(err) => err.internal_errno,
        }
    }

    pub(crate) fn last() -> Self {
        Self::Config(ConfigError::new(
            config_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

impl fmt::Display for ZlinkError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Submit(err) => err.fmt(f),
            Self::Request(err) => err.fmt(f),
            Self::Recv(err) => err.fmt(f),
            Self::Handler(err) => err.fmt(f),
            Self::Close(err) => err.fmt(f),
            Self::Bind(err) => err.fmt(f),
            Self::Connect(err) => err.fmt(f),
            Self::Config(err) => err.fmt(f),
        }
    }
}

impl std::error::Error for ZlinkError {}

pub(crate) fn last_errno() -> i32 {
    unsafe { ffi::zlink_errno() }
}

fn strerror(errnum: i32) -> String {
    let ptr = unsafe { ffi::zlink_strerror(errnum) };
    if ptr.is_null() {
        return "unknown error".to_string();
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned()
}

#[allow(dead_code)]
pub(crate) fn last_message() -> String {
    strerror(last_errno())
}

fn submit_result_from_errno(err: i32) -> SubmitResult {
    match err {
        0 => SubmitResult::Ok,
        libc::EAGAIN => SubmitResult::Backpressured,
        libc::ENOTCONN | libc::EHOSTUNREACH => SubmitResult::NotConnected,
        libc::ENOENT => SubmitResult::NotFound,
        x if x == eterm() => SubmitResult::Terminated,
        libc::EFAULT => SubmitResult::InvalidHandle,
        libc::EINVAL => SubmitResult::InvalidArgument,
        libc::ENOTSUP => SubmitResult::NotSupported,
        x if x == eopnotsupp() => SubmitResult::NotSupported,
        libc::EBUSY => SubmitResult::InvalidState,
        x if x == emthread() => SubmitResult::ThreadViolation,
        libc::ENOMEM | libc::ENOBUFS => SubmitResult::OutOfMemory,
        _ => SubmitResult::InternalError,
    }
}

fn recv_result_from_errno(err: i32) -> RecvResult {
    match err {
        0 => RecvResult::Ok,
        libc::EAGAIN => RecvResult::NoData,
        libc::EBUSY => RecvResult::Busy,
        x if x == eterm() => RecvResult::Terminated,
        libc::EFAULT => RecvResult::InvalidHandle,
        libc::ENOTSUP => RecvResult::NotSupported,
        x if x == eopnotsupp() => RecvResult::NotSupported,
        _ => RecvResult::InternalError,
    }
}

fn handler_result_from_errno(err: i32) -> HandlerResult {
    match err {
        0 => HandlerResult::Ok,
        libc::EINVAL => HandlerResult::InvalidArgument,
        libc::EBUSY => HandlerResult::Busy,
        libc::ENOTSUP => HandlerResult::NotSupported,
        x if x == eopnotsupp() => HandlerResult::NotSupported,
        libc::EDEADLK => HandlerResult::Deadlock,
        libc::EFAULT => HandlerResult::InvalidHandle,
        _ => HandlerResult::InternalError,
    }
}

fn close_result_from_errno(err: i32) -> CloseResult {
    match err {
        0 => CloseResult::Ok,
        libc::EBUSY => CloseResult::Busy,
        x if x == eshutdown() => CloseResult::Shutdown,
        libc::EFAULT => CloseResult::InvalidHandle,
        _ => CloseResult::InternalError,
    }
}

fn bind_result_from_errno(err: i32) -> BindResult {
    match err {
        0 => BindResult::Ok,
        libc::EINVAL => BindResult::InvalidArgument,
        libc::EADDRINUSE => BindResult::AddrInUse,
        libc::ENOTSUP => BindResult::NotSupported,
        x if x == eopnotsupp() => BindResult::NotSupported,
        libc::EFAULT => BindResult::InvalidHandle,
        _ => BindResult::InternalError,
    }
}

fn connect_result_from_errno(err: i32) -> ConnectResult {
    match err {
        0 => ConnectResult::Ok,
        libc::EINVAL => ConnectResult::InvalidArgument,
        libc::ENOTSUP => ConnectResult::NotSupported,
        x if x == eopnotsupp() => ConnectResult::NotSupported,
        libc::EFAULT => ConnectResult::InvalidHandle,
        libc::ENOENT => ConnectResult::NotFound,
        libc::EBUSY => ConnectResult::Busy,
        _ => ConnectResult::InternalError,
    }
}

fn config_result_from_errno(err: i32) -> ConfigResult {
    match err {
        0 => ConfigResult::Ok,
        libc::EFAULT => ConfigResult::InvalidHandle,
        libc::EINVAL => ConfigResult::InvalidArgument,
        libc::ENOTSUP => ConfigResult::NotSupported,
        x if x == eopnotsupp() => ConfigResult::NotSupported,
        libc::ENOENT => ConfigResult::NotFound,
        libc::EBUSY => ConfigResult::InvalidState,
        _ => ConfigResult::InternalError,
    }
}

pub(crate) fn submit_validation_error() -> SubmitError {
    SubmitError::new(SubmitResult::InvalidArgument, libc::EINVAL)
}

pub(crate) fn submit_not_supported_error() -> SubmitError {
    SubmitError::new(SubmitResult::NotSupported, libc::ENOTSUP)
}

pub(crate) fn submit_error_from_errno(err: i32) -> SubmitError {
    SubmitError::new(submit_result_from_errno(err), err)
}

pub(crate) fn submit_error_from_config(err: ConfigError) -> SubmitError {
    submit_error_from_errno(err.internal_errno())
}

pub(crate) fn submit_state_error() -> SubmitError {
    SubmitError::new(SubmitResult::InvalidState, libc::EINVAL)
}

pub(crate) fn request_error_from_submit(err: SubmitError) -> RequestError {
    RequestError::new(RequestResult::ProtocolError, err.internal_errno())
}

pub(crate) fn request_error_from_result(code: RequestResult) -> RequestError {
    let internal_errno = match code {
        RequestResult::Ok => 0,
        RequestResult::TimedOut => libc::ETIMEDOUT,
        RequestResult::NotFound => libc::ENOENT,
        RequestResult::Terminated => eterm(),
        RequestResult::ProtocolError => libc::EPROTO,
        RequestResult::InternalError => libc::EIO,
        RequestResult::Rejected => libc::ECONNREFUSED,
        RequestResult::Conflict => libc::EINVAL,
        RequestResult::Busy => libc::EBUSY,
        RequestResult::NotConnected => libc::ENOTCONN,
        RequestResult::InvalidArgument => libc::EINVAL,
        RequestResult::InvalidState => libc::EINVAL,
        RequestResult::NotSupported => libc::ENOTSUP,
    };
    RequestError::new(code, internal_errno)
}

pub(crate) fn recv_state_error() -> RecvError {
    RecvError::new(RecvResult::Busy, libc::EINVAL)
}

pub(crate) fn config_validation_error() -> ConfigError {
    ConfigError::new(ConfigResult::InvalidArgument, libc::EINVAL)
}

pub(crate) fn check_rc(rc: i32) -> Result<(), ZlinkError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(ZlinkError::last())
    }
}

pub(crate) fn check_submit_rc(rc: i32) -> Result<(), SubmitError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(SubmitError::new(
            submit_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

pub(crate) fn check_recv_rc(rc: i32) -> Result<(), RecvError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(RecvError::new(
            recv_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

pub(crate) fn check_handler_rc(rc: i32) -> Result<(), HandlerError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(HandlerError::new(
            handler_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

pub(crate) fn check_close_rc(rc: i32) -> Result<(), CloseError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(CloseError::new(
            close_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

pub(crate) fn check_bind_rc(rc: i32) -> Result<(), BindError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(BindError::new(
            bind_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

pub(crate) fn check_connect_rc(rc: i32) -> Result<(), ConnectError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(ConnectError::new(
            connect_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

pub(crate) fn check_config_rc(rc: i32) -> Result<(), ConfigError> {
    if rc == 0 {
        Ok(())
    } else {
        Err(ConfigError::new(
            config_result_from_errno(last_errno()),
            last_errno(),
        ))
    }
}

const fn eterm() -> i32 {
    156_384_765
}

const fn emthread() -> i32 {
    156_384_766
}

const fn eopnotsupp() -> i32 {
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        libc::EOPNOTSUPP
    }
    #[cfg(not(any(target_os = "linux", target_os = "android")))]
    {
        libc::ENOTSUP
    }
}

const fn eshutdown() -> i32 {
    #[cfg(any(
        target_os = "linux",
        target_os = "android",
        target_os = "freebsd",
        target_os = "dragonfly",
        target_os = "netbsd",
        target_os = "openbsd",
        target_os = "macos",
        target_os = "ios"
    ))]
    {
        libc::ESHUTDOWN
    }
    #[cfg(not(any(
        target_os = "linux",
        target_os = "android",
        target_os = "freebsd",
        target_os = "dragonfly",
        target_os = "netbsd",
        target_os = "openbsd",
        target_os = "macos",
        target_os = "ios"
    )))]
    {
        libc::EPIPE
    }
}
