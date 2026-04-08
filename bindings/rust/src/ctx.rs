use std::ffi::c_void;
use std::time::Duration;

use crate::error::{ZlinkError, check_rc};
use crate::ffi;
use crate::socket::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, StreamSocket, SubSocket, XPubSocket,
    XSubSocket,
};

/// The zlink context – foundation for creating sockets.
///
/// A context manages I/O threads and internal infrastructure. Dropping a
/// `Context` calls `zlink_ctx_term`, which blocks until all sockets created
/// from it are closed.
pub struct Context {
    handle: *mut c_void,
}

/// Typed facade over `zlink_ctx_*` options.
pub struct ContextOptions<'a> {
    context: &'a Context,
}

unsafe impl Send for Context {}
unsafe impl Sync for Context {}

impl Context {
    /// Create a new context with default settings.
    pub fn new() -> Result<Self, ZlinkError> {
        let handle = unsafe { ffi::zlink_ctx_new() };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle })
    }

    /// Interrupt all blocking calls on sockets of this context with ETERM.
    /// `drop` / `zlink_ctx_term` must still be called for final cleanup.
    pub fn shutdown(&self) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_ctx_shutdown(self.handle) })
    }

    /// Access typed context options.
    pub fn options(&self) -> ContextOptions<'_> {
        ContextOptions { context: self }
    }

    pub(crate) fn set_int_option(&self, option: i32, value: i32) -> Result<(), ZlinkError> {
        check_rc(unsafe { ffi::zlink_ctx_set(self.handle, raw_option(option), value) })
    }

    pub(crate) fn get_int_option(&self, option: i32) -> Result<i32, ZlinkError> {
        let value = unsafe { ffi::zlink_ctx_get(self.handle, raw_option(option)) };
        if value == -1 {
            Err(ZlinkError::last())
        } else {
            Ok(value)
        }
    }

    // -- Socket factories --------------------------------------------------

    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }

    /// Create a PAIR socket.
    pub fn pair_socket(&self) -> Result<PairSocket, ZlinkError> {
        PairSocket::new(self)
    }

    /// Create a PUB socket.
    pub fn pub_socket(&self) -> Result<PubSocket, ZlinkError> {
        PubSocket::new(self)
    }

    /// Create a SUB socket.
    pub fn sub_socket(&self) -> Result<SubSocket, ZlinkError> {
        SubSocket::new(self)
    }

    /// Create a DEALER socket.
    pub fn dealer_socket(&self) -> Result<DealerSocket, ZlinkError> {
        DealerSocket::new(self)
    }

    /// Create a ROUTER socket.
    pub fn router_socket(&self) -> Result<RouterSocket, ZlinkError> {
        RouterSocket::new(self)
    }

    /// Create an XPUB socket.
    pub fn xpub_socket(&self) -> Result<XPubSocket, ZlinkError> {
        XPubSocket::new(self)
    }

    /// Create an XSUB socket.
    pub fn xsub_socket(&self) -> Result<XSubSocket, ZlinkError> {
        XSubSocket::new(self)
    }

    /// Create a STREAM socket.
    pub fn stream_socket(&self) -> Result<StreamSocket, ZlinkError> {
        StreamSocket::new(self)
    }
}

impl<'a> ContextOptions<'a> {
    pub fn io_threads(&self) -> Result<i32, ZlinkError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_IO_THREADS as i32)
    }

    pub fn set_io_threads(&self, threads: i32) -> Result<(), ZlinkError> {
        self.context
            .set_int_option(ffi::zlink_ctx_option_t::ZLINK_IO_THREADS as i32, threads)
    }

    pub fn max_sockets(&self) -> Result<i32, ZlinkError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_SOCKETS as i32)
    }

    pub fn set_max_sockets(&self, max: i32) -> Result<(), ZlinkError> {
        self.context
            .set_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_SOCKETS as i32, max)
    }

    pub fn socket_limit(&self) -> Result<i32, ZlinkError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_SOCKET_LIMIT as i32)
    }

    pub fn thread_priority(&self) -> Result<i32, ZlinkError> {
        self.context.get_int_option(3)
    }

    pub fn set_thread_priority(&self, priority: i32) -> Result<(), ZlinkError> {
        self.context.set_int_option(3, priority)
    }

    pub fn thread_scheduling_policy(&self) -> Result<i32, ZlinkError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_THREAD_SCHED_POLICY as i32)
    }

    pub fn set_thread_scheduling_policy(&self, policy: i32) -> Result<(), ZlinkError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_SCHED_POLICY as i32,
            policy,
        )
    }

    pub fn max_msg_size(&self) -> Result<i32, ZlinkError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_MSGSZ as i32)
    }

    pub fn set_max_msg_size(&self, size: i32) -> Result<(), ZlinkError> {
        self.context
            .set_int_option(ffi::zlink_ctx_option_t::ZLINK_MAX_MSGSZ as i32, size)
    }

    pub fn msg_t_size(&self) -> Result<i32, ZlinkError> {
        self.context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_MSG_T_SIZE as i32)
    }

    pub fn blocky(&self) -> Result<bool, ZlinkError> {
        Ok(self
            .context
            .get_int_option(ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_BLOCKY as i32)?
            != 0)
    }

    pub fn set_blocky(&self, blocky: bool) -> Result<(), ZlinkError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_CTX_OPT_BLOCKY as i32,
            if blocky { 1 } else { 0 },
        )
    }

    pub fn add_thread_affinity(&self, cpu: i32) -> Result<(), ZlinkError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_AFFINITY_CPU_ADD as i32,
            cpu,
        )
    }

    pub fn remove_thread_affinity(&self, cpu: i32) -> Result<(), ZlinkError> {
        self.context.set_int_option(
            ffi::zlink_ctx_option_t::ZLINK_THREAD_AFFINITY_CPU_REMOVE as i32,
            cpu,
        )
    }
}

fn raw_option(value: i32) -> ffi::zlink_ctx_option_t {
    unsafe { std::mem::transmute(value) }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            ffi::zlink_ctx_term(self.handle);
        }
    }
}

// ---------------------------------------------------------------------------
// Version helper
// ---------------------------------------------------------------------------

/// Return the runtime library version as `(major, minor, patch)`.
pub fn version() -> (i32, i32, i32) {
    let (mut major, mut minor, mut patch) = (0i32, 0i32, 0i32);
    unsafe {
        ffi::zlink_version(&mut major, &mut minor, &mut patch);
    }
    (major, minor, patch)
}

/// Check whether the library supports a given capability (e.g. `"ipc"`, `"tls"`).
pub fn has(capability: &str) -> bool {
    let c = std::ffi::CString::new(capability).unwrap_or_default();
    unsafe { ffi::zlink_has(c.as_ptr()) != 0 }
}

// ---------------------------------------------------------------------------
// Duration → millis helper (with overflow check per Boundary Cost Policy)
// ---------------------------------------------------------------------------

pub(crate) fn duration_to_millis(d: Duration) -> Result<i32, ZlinkError> {
    let ms = d.as_millis();
    if ms > i32::MAX as u128 {
        return Err(ZlinkError::validation(format!(
            "duration {ms}ms overflows i32"
        )));
    }
    Ok(ms as i32)
}
