use std::ffi::CStr;
use std::fmt;

use crate::ffi;

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

    /// The raw errno code.
    pub fn code(&self) -> i32 {
        self.code
    }
}

impl fmt::Display for ZlinkError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "zlink error {}: {}", self.code, self.message)
    }
}

impl std::error::Error for ZlinkError {}

/// Check a C return code; -1 becomes `Err(ZlinkError::last())`.
pub(crate) fn check_rc(rc: i32) -> Result<(), ZlinkError> {
    if rc == -1 {
        Err(ZlinkError::last())
    } else {
        Ok(())
    }
}
