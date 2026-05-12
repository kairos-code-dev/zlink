use std::ffi::c_void;

use crate::error::SubmitError;
use crate::ffi;
use crate::flags::SendFlags;
use crate::message::{IntoMultipart, RoutingId};

use super::{check_send_flags_rc, prepare_send_parts, submit_part_sequence};

/// A lightweight, cloneable handle for sending messages on a socket.
///
/// `SendHandle` captures only the raw native socket pointer and can be safely
/// moved into callback closures, other threads, or `Arc` containers. It does
/// **not** own the socket -- the original socket object must remain alive for
/// the duration of any sends through this handle.
///
/// # Safety contract
/// The underlying `zlink_send_part` / `zlink_send_part_rid` calls are
/// thread-safe in the core C library, so calling `send` or `send_to` from a
/// callback thread is valid as long as the socket has not been closed.
///
/// # Example
/// ```no_run
/// use zlink::{Context, Message, Received, RecvFlags, RoutingId};
///
/// let ctx = Context::new().unwrap();
/// let mut router = ctx.router_socket().unwrap();
/// let handle = router.send_handle();
///
/// let mut received = Received::empty();
/// router.recv(&mut received, RecvFlags::NONE).unwrap();
/// let reply = Message::copy_from(b"pong").unwrap();
/// handle
///     .send_to(received.routing_id().expect("missing routing id"), reply)
///     .unwrap();
/// ```
#[derive(Clone)]
pub struct SendHandle {
    handle: *mut c_void,
}

// The core C API is thread-safe for send operations.
unsafe impl Send for SendHandle {}
unsafe impl Sync for SendHandle {}

impl SendHandle {
    /// Create a send handle from a raw native socket pointer.
    pub(crate) fn new(handle: *mut c_void) -> Self {
        Self { handle }
    }

    /// Send a non-routed message (PAIR, DEALER, etc.).
    pub fn send(&self, parts: impl IntoMultipart) -> Result<(), SubmitError> {
        self.send_with_flags(parts, SendFlags::NONE).map(|_| ())
    }

    pub fn send_with_flags(
        &self,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<bool, SubmitError> {
        let mut parts = parts.into_parts();
        let mut native =
            prepare_send_parts(&mut parts).map_err(|_| crate::error::submit_state_error())?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_send_part(self.handle, part, flags.bits(), part_flag)
        })?;
        drop(parts);
        check_send_flags_rc(rc)
    }

    /// Send a routed message to a specific peer (ROUTER).
    pub fn send_to(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
    ) -> Result<(), SubmitError> {
        self.send_to_with_flags(target, parts, SendFlags::NONE)
    }

    pub fn send_to_with_flags(
        &self,
        target: &RoutingId,
        parts: impl IntoMultipart,
        flags: SendFlags,
    ) -> Result<(), SubmitError> {
        let mut parts = parts.into_parts();
        let mut native =
            prepare_send_parts(&mut parts).map_err(|_| crate::error::submit_state_error())?;
        let rc = submit_part_sequence(&mut native, |part, part_flag, _| unsafe {
            ffi::zlink_send_part_rid(self.handle, target.as_raw(), part, flags.bits(), part_flag)
        })?;
        drop(parts);
        check_send_flags_rc(rc).map(|_| ())
    }
}
