use std::ffi::c_void;
use std::marker::PhantomData;

use crate::error::{ZlinkError, check_rc};
use crate::ffi;

/// Poll event flags.
pub const POLLIN: i16 = ffi::ZLINK_POLLIN;
pub const POLLOUT: i16 = ffi::ZLINK_POLLOUT;

/// A single poll result event.
#[derive(Debug, Clone)]
pub struct PollEvent {
    pub token: usize,
    pub events: i16,
}

impl PollEvent {
    pub fn is_readable(&self) -> bool {
        self.events & POLLIN != 0
    }

    pub fn is_writable(&self) -> bool {
        self.events & POLLOUT != 0
    }
}

/// Typed poll target for socket readiness monitoring.
pub struct PollTarget<'a> {
    handle: *mut c_void,
    _marker: PhantomData<&'a ()>,
}

impl<'a> PollTarget<'a> {
    fn raw(&self) -> *mut c_void {
        self.handle
    }
}

/// Poller for monitoring socket readiness (POLLIN / POLLOUT).
pub struct Poller {
    handle: *mut c_void,
}

unsafe impl Send for Poller {}

impl Poller {
    pub fn new() -> Result<Self, ZlinkError> {
        let handle = unsafe { ffi::zlink_poller_new() };
        if handle.is_null() {
            return Err(ZlinkError::last());
        }
        Ok(Self { handle })
    }

    /// Add a socket to the poller with the given event mask and user data.
    pub fn add_socket<'a>(
        &self,
        socket: impl Into<PollTarget<'a>>,
        token: usize,
        events: i16,
    ) -> Result<(), ZlinkError> {
        let target = socket.into();
        check_rc(unsafe {
            ffi::zlink_poller_add(self.handle, target.raw(), token as *mut c_void, events)
        })
    }

    /// Modify the event mask for a previously added socket.
    pub fn modify_socket<'a>(
        &self,
        socket: impl Into<PollTarget<'a>>,
        events: i16,
    ) -> Result<(), ZlinkError> {
        let target = socket.into();
        check_rc(unsafe { ffi::zlink_poller_modify(self.handle, target.raw(), events) })
    }

    /// Remove a socket from the poller.
    pub fn remove_socket<'a>(&self, socket: impl Into<PollTarget<'a>>) -> Result<(), ZlinkError> {
        let target = socket.into();
        check_rc(unsafe { ffi::zlink_poller_remove(self.handle, target.raw()) })
    }

    /// Wait for events on any registered socket.
    ///
    /// `timeout_ms`: -1 = block indefinitely, 0 = return immediately,
    /// positive = timeout in milliseconds.
    ///
    /// Returns `Ok(None)` on timeout, `Ok(Some(event))` when an event occurs.
    pub fn wait(&self, timeout_ms: i64) -> Result<Option<PollEvent>, ZlinkError> {
        let mut raw = ffi::zlink_poller_event_t {
            socket: std::ptr::null_mut(),
            fd: 0 as ffi::zlink_fd_t,
            user_data: std::ptr::null_mut(),
            events: 0,
        };
        let rc = unsafe {
            ffi::zlink_poller_wait(self.handle, &mut raw, timeout_ms as std::ffi::c_long)
        };
        if rc == -1 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN || errno == libc::ETIMEDOUT {
                return Ok(None);
            }
            return Err(ZlinkError::last());
        }
        if rc == 0 {
            return Ok(None);
        }
        Ok(Some(PollEvent {
            token: raw.user_data as usize,
            events: raw.events,
        }))
    }

    /// Wait for multiple events at once.
    ///
    /// Returns the events that fired. Empty vec on timeout.
    pub fn wait_all(
        &self,
        max_events: usize,
        timeout_ms: i64,
    ) -> Result<Vec<PollEvent>, ZlinkError> {
        let mut raw = vec![
            ffi::zlink_poller_event_t {
                socket: std::ptr::null_mut(),
                fd: 0 as ffi::zlink_fd_t,
                user_data: std::ptr::null_mut(),
                events: 0,
            };
            max_events
        ];
        let rc = unsafe {
            ffi::zlink_poller_wait_all(
                self.handle,
                raw.as_mut_ptr(),
                max_events as i32,
                timeout_ms as std::ffi::c_long,
            )
        };
        if rc == -1 {
            let errno = unsafe { ffi::zlink_errno() };
            if errno == libc::EAGAIN || errno == libc::ETIMEDOUT {
                return Ok(Vec::new());
            }
            return Err(ZlinkError::last());
        }
        if rc == 0 {
            return Ok(Vec::new());
        }
        let n = rc as usize;
        let mut out = Vec::with_capacity(n);
        for i in 0..n {
            out.push(PollEvent {
                token: raw[i].user_data as usize,
                events: raw[i].events,
            });
        }
        Ok(out)
    }

    pub fn size(&self) -> i32 {
        unsafe { ffi::zlink_poller_size(self.handle) }
    }
}

impl Drop for Poller {
    fn drop(&mut self) {
        unsafe {
            let mut h = self.handle;
            ffi::zlink_poller_destroy(&mut h);
        }
    }
}

macro_rules! impl_poll_target_from_ref {
    ($ty:ident) => {
        impl<'a> From<&'a crate::socket::$ty> for PollTarget<'a> {
            fn from(socket: &'a crate::socket::$ty) -> Self {
                Self {
                    handle: socket.inner.handle,
                    _marker: PhantomData,
                }
            }
        }
    };
}

impl_poll_target_from_ref!(PairSocket);
impl_poll_target_from_ref!(PubSocket);
impl_poll_target_from_ref!(SubSocket);
impl_poll_target_from_ref!(DealerSocket);
impl_poll_target_from_ref!(RouterSocket);
impl_poll_target_from_ref!(XPubSocket);
impl_poll_target_from_ref!(XSubSocket);
impl_poll_target_from_ref!(StreamSocket);
