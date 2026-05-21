use std::collections::HashMap;
use std::ffi::c_void;
use std::marker::PhantomData;
use std::os::fd::RawFd;
use std::sync::Mutex;

use crate::error::{
    ConfigError, HandlerError, RecvError, check_config_rc, check_handler_rc, check_recv_rc,
};
use crate::ffi;
use crate::request_progress::{acquire_external_progress, release_external_progress};

/// Poll event flags.
pub const POLLIN: i16 = ffi::ZLINK_POLLIN;
pub const POLLOUT: i16 = ffi::ZLINK_POLLOUT;
pub const POLLCOMPLETION: i16 = ffi::ZLINK_POLLCOMPLETION;

/// Source kind reported by the poller when an event fires.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PollSourceKind {
    Socket = 1,
    Fd = 2,
    Timer = 3,
}

/// A single poll result event.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct PollEvent {
    pub source_kind: PollSourceKind,
    socket: *mut c_void,
    pub fd: RawFd,
    timer: *mut c_void,
    pub slot: usize,
    pub revents: i16,
}

impl PollEvent {
    pub fn is_readable(&self) -> bool {
        self.revents & POLLIN != 0
    }

    pub fn is_writable(&self) -> bool {
        self.revents & POLLOUT != 0
    }
}

impl Default for PollEvent {
    fn default() -> Self {
        Self {
            source_kind: PollSourceKind::Socket,
            socket: std::ptr::null_mut(),
            fd: 0,
            timer: std::ptr::null_mut(),
            slot: 0,
            revents: 0,
        }
    }
}

/// Typed poll target for socket readiness monitoring.
pub struct PollTarget<'a> {
    handle: *mut c_void,
    _marker: PhantomData<&'a ()>,
}

impl PollTarget<'_> {
    pub(crate) fn raw(&self) -> *mut c_void {
        self.handle
    }
}

/// Poller for monitoring socket readiness (POLLIN / POLLOUT).
pub struct Poller {
    handle: *mut c_void,
    sockets: Mutex<HashMap<usize, i16>>,
}

unsafe impl Send for Poller {}

impl Poller {
    pub fn new() -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_poller_new() };
        if handle.is_null() {
            return Err(crate::error::ConfigError::new(
                crate::error::ConfigResult::InvalidArgument,
                crate::error::last_errno(),
            ));
        }
        Ok(Self {
            handle,
            sockets: Mutex::new(HashMap::new()),
        })
    }

    pub fn add_socket<'a>(
        &self,
        socket: impl Into<PollTarget<'a>>,
        events: i16,
        slot: usize,
    ) -> Result<(), ConfigError> {
        let target = socket.into();
        check_config_rc(unsafe {
            ffi::zlink_poller_add(self.handle, target.raw(), slot as *mut c_void, events)
        })?;
        if events & POLLCOMPLETION != 0 {
            acquire_external_progress(target.raw());
        }
        self.sockets
            .lock()
            .expect("poller sockets")
            .insert(target.raw() as usize, events);
        Ok(())
    }

    /// Modify the event mask for a previously added socket.
    pub fn modify_socket<'a>(
        &self,
        socket: impl Into<PollTarget<'a>>,
        events: i16,
    ) -> Result<(), ConfigError> {
        let target = socket.into();
        let mut sockets = self.sockets.lock().expect("poller sockets");
        let previous = sockets.get(&(target.raw() as usize)).copied().unwrap_or(0);
        if previous & POLLCOMPLETION == 0 && events & POLLCOMPLETION != 0 {
            acquire_external_progress(target.raw());
        } else if previous & POLLCOMPLETION != 0 && events & POLLCOMPLETION == 0 {
            release_external_progress(target.raw());
        }
        sockets.insert(target.raw() as usize, events);
        drop(sockets);
        check_config_rc(unsafe { ffi::zlink_poller_modify(self.handle, target.raw(), events) })
    }

    /// Remove a socket from the poller.
    pub fn remove_socket<'a>(&self, socket: impl Into<PollTarget<'a>>) -> Result<(), ConfigError> {
        let target = socket.into();
        check_config_rc(unsafe { ffi::zlink_poller_remove(self.handle, target.raw()) })?;
        if let Some(events) = self
            .sockets
            .lock()
            .expect("poller sockets")
            .remove(&(target.raw() as usize))
        {
            if events & POLLCOMPLETION != 0 {
                release_external_progress(target.raw());
            }
        }
        Ok(())
    }

    pub fn add_fd(&self, fd: RawFd, events: i16, slot: usize) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_add_fd(
                self.handle,
                fd as ffi::zlink_fd_t,
                slot as *mut c_void,
                events,
            )
        })
    }

    pub fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_modify_fd(self.handle, fd as ffi::zlink_fd_t, events)
        })
    }

    pub fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_poller_remove_fd(self.handle, fd as ffi::zlink_fd_t) })
    }

    pub fn add_timer(&self, timer: &Timer, slot: usize) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_add_timer(self.handle, timer.raw_handle(), slot as *mut c_void)
        })
    }

    pub fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_poller_remove_timer(self.handle, timer.raw_handle()) })
    }

    /// Wait for events on any registered source.
    ///
    /// `timeout_ms`: -1 = block indefinitely, 0 = return immediately,
    /// positive = timeout in milliseconds.
    ///
    /// Returns the number of entries written to `events`.
    pub fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError> {
        if events.is_empty() {
            return Err(crate::error::RecvError::new(
                crate::error::RecvResult::InternalError,
                libc::EINVAL,
            ));
        }
        let rc = unsafe {
            ffi::zlink_poller_wait(
                self.handle,
                events.as_mut_ptr() as *mut ffi::zlink_poller_event_t,
                events.len() as i32,
                timeout_ms as std::ffi::c_long,
                std::ptr::null_mut(),
            )
        };
        if rc < 0 {
            let errno = crate::error::last_errno();
            if errno == libc::EAGAIN || errno == libc::ETIMEDOUT {
                return Ok(0);
            }
            return Err(crate::error::RecvError::new(
                crate::error::RecvResult::Terminated,
                errno,
            ));
        }
        if rc == 0 {
            return Ok(0);
        }
        Ok(rc as usize)
    }

    pub fn size(&self) -> i32 {
        let mut error_out = 0;
        unsafe { ffi::zlink_poller_size(self.handle, &mut error_out) }
    }
}

impl Drop for Poller {
    fn drop(&mut self) {
        if let Ok(mut sockets) = self.sockets.lock() {
            for (handle, events) in sockets.drain() {
                if events & POLLCOMPLETION != 0 {
                    release_external_progress(handle as *mut c_void);
                }
            }
        }
        unsafe {
            let mut h = self.handle;
            ffi::zlink_poller_destroy(&mut h);
        }
    }
}

#[derive(Clone, Copy)]
pub struct PollItem {
    pub socket: *mut c_void,
    pub fd: RawFd,
    pub events: i16,
    pub revents: i16,
}

pub fn poll(items: &mut [PollItem], timeout_ms: i64) -> Result<i32, RecvError> {
    let mut raw: Vec<ffi::zlink_pollitem_t> = items
        .iter()
        .map(|item| ffi::zlink_pollitem_t {
            socket: item.socket,
            fd: item.fd as ffi::zlink_fd_t,
            events: item.events,
            revents: item.revents,
        })
        .collect();
    let rc = unsafe {
        ffi::zlink_poll(
            raw.as_mut_ptr(),
            raw.len() as i32,
            timeout_ms as std::ffi::c_long,
            std::ptr::null_mut(),
        )
    };
    if rc < 0 {
        return Err(crate::error::RecvError::new(
            crate::error::RecvResult::Terminated,
            crate::error::last_errno(),
        ));
    }
    for (idx, item) in items.iter_mut().enumerate() {
        item.revents = raw[idx].revents;
    }
    Ok(rc)
}

pub struct Timer {
    handle: *mut c_void,
    callback: Option<crate::socket::CallbackBox>,
}

unsafe impl Send for Timer {}

impl Timer {
    pub fn new() -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_timer_new() };
        if handle.is_null() {
            return Err(crate::error::ConfigError::new(
                crate::error::ConfigResult::InvalidArgument,
                crate::error::last_errno(),
            ));
        }
        Ok(Self {
            handle,
            callback: None,
        })
    }

    /// Create a timer that belongs to (and is dispatched by) the given `Spot`.
    pub fn from_spot(spot: &crate::service::Spot) -> Result<Self, ConfigError> {
        let handle = unsafe { ffi::zlink_spot_timer_new(spot.raw()) };
        if handle.is_null() {
            return Err(crate::error::ConfigError::new(
                crate::error::ConfigResult::InvalidArgument,
                crate::error::last_errno(),
            ));
        }
        Ok(Self {
            handle,
            callback: None,
        })
    }

    pub fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_timer_start(self.handle, interval_ns, repeat_count) })
    }

    pub fn stop(&self) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_timer_stop(self.handle) })
    }

    /// Receive a timer fire count. Returns `Ok(None)` when no data is available (EAGAIN).
    pub fn recv(&self) -> Result<Option<u64>, RecvError> {
        let mut count = 0u64;
        let rc = unsafe { ffi::zlink_timer_recv(self.handle, &mut count) };
        if rc != 0 {
            let errno = crate::error::last_errno();
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(check_recv_rc(rc).unwrap_err());
        }
        Ok(Some(count))
    }

    pub fn on_fire<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(&Timer, u64) + Send + 'static,
    {
        let timer_ptr = self as *const Timer as usize;
        let wrapped: Box<dyn Fn(u64) + Send> = Box::new(move |count: u64| {
            let timer = unsafe { &*(timer_ptr as *const Timer) };
            handler(timer, count);
        });
        let (cb, userdata) = crate::socket::CallbackBox::new(wrapped);
        unsafe extern "C" fn trampoline(_timer: *mut c_void, count: u64, userdata: *mut c_void) {
            let handler = unsafe { &*(userdata as *const Box<dyn Fn(u64) + Send>) };
            handler(count);
        }
        let rc = unsafe { ffi::zlink_timer_handler(self.handle, trampoline, userdata) };
        if rc != 0 {
            drop(cb);
            return Err(check_handler_rc(rc).unwrap_err());
        }
        self.callback = Some(cb);
        Ok(())
    }

    /// Native timer handle, used by `Poller::add_timer`/`remove_timer` to
    /// register the timer subject via `zlink_poller_add_timer` /
    /// `zlink_poller_remove_timer`.
    pub(crate) fn raw_handle(&self) -> *mut c_void {
        self.handle
    }
}

impl Drop for Timer {
    fn drop(&mut self) {
        unsafe {
            let mut handle = self.handle;
            let _ = ffi::zlink_timer_destroy(&mut handle);
        }
    }
}

pub struct Stopwatch {
    handle: *mut c_void,
}

impl Stopwatch {
    pub fn start() -> Self {
        Self {
            handle: unsafe { ffi::zlink_stopwatch_start() },
        }
    }

    pub fn intermediate(&self) -> u64 {
        unsafe { ffi::zlink_stopwatch_intermediate(self.handle) as u64 }
    }

    pub fn stop(self) -> u64 {
        unsafe { ffi::zlink_stopwatch_stop(self.handle) as u64 }
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

impl<'a> From<&'a crate::service::Spot> for PollTarget<'a> {
    fn from(spot: &'a crate::service::Spot) -> Self {
        Self {
            handle: spot.raw(),
            _marker: PhantomData,
        }
    }
}
