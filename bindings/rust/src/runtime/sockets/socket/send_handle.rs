use std::ffi::c_void;

use crate::message::RoutingId;
use crate::service::{Empty, SendOp};

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
/// let reply = Message::try_from(b"pong").unwrap();
/// handle
///     .send_to(received.routing_id().expect("missing routing id"))
///     .message(reply)
///     .submit()
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
    pub fn send(&self) -> SendOp<Empty> {
        crate::service::socket_send_op(self.handle)
    }

    /// Send a routed message to a specific peer (ROUTER).
    pub fn send_to(&self, target: &RoutingId) -> SendOp<Empty> {
        crate::service::socket_send_to_op(self.handle, target.clone())
    }
}
