use std::fmt;

pub use crate::results::{
    BindResult, CloseResult, ConfigResult, ConnectResult, HandlerResult, RecvResult, RequestResult,
    SubmitResult,
};

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
