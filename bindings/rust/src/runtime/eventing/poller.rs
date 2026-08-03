use std::collections::HashMap;
use std::ffi::c_void;
use std::os::fd::RawFd;
use std::sync::Mutex;

use crate::error::{ConfigError, HandlerError, RecvError};
use crate::ffi;
use crate::native_errors::{check_config_rc, check_handler_rc, check_recv_rc, last_errno};
use crate::poller_contracts::{
    POLLCOMPLETION, PollEvent, PollItem, PollSourceKind, Pollable, Poller, Timer,
};
use crate::request_progress::{acquire_external_progress, release_external_progress};
use crate::runtime_bridge::{PollerRuntime, TimerFireHandler, TimerRuntime};

struct NativePoller {
    handle: *mut c_void,
    sockets: Mutex<HashMap<usize, i16>>,
}

unsafe impl Send for NativePoller {}

pub(crate) fn poller_new() -> Result<Poller, ConfigError> {
    let handle = unsafe { ffi::zlink_poller_new() };
    if handle.is_null() {
        return Err(crate::error::ConfigError::new(
            crate::error::ConfigResult::InvalidArgument,
            last_errno(),
        ));
    }
    Ok(Poller {
        inner: Box::new(NativePoller {
            handle,
            sockets: Mutex::new(HashMap::new()),
        }),
    })
}

impl PollerRuntime for NativePoller {
    fn add_socket(
        &self,
        socket: &dyn Pollable,
        events: i16,
        slot: usize,
    ) -> Result<(), ConfigError> {
        let handle = pollable_handle(socket)?;
        check_config_rc(unsafe {
            ffi::zlink_poller_add(self.handle, handle, slot as *mut c_void, events)
        })?;
        if events & POLLCOMPLETION != 0 {
            acquire_external_progress(handle);
        }
        self.sockets
            .lock()
            .expect("poller sockets")
            .insert(handle as usize, events);
        Ok(())
    }

    /// Modify the event mask for a previously added socket.
    fn modify_socket(&self, socket: &dyn Pollable, events: i16) -> Result<(), ConfigError> {
        let handle = pollable_handle(socket)?;
        let mut sockets = self.sockets.lock().expect("poller sockets");
        let previous = sockets.get(&(handle as usize)).copied().unwrap_or(0);
        if previous & POLLCOMPLETION == 0 && events & POLLCOMPLETION != 0 {
            acquire_external_progress(handle);
        } else if previous & POLLCOMPLETION != 0 && events & POLLCOMPLETION == 0 {
            release_external_progress(handle);
        }
        sockets.insert(handle as usize, events);
        drop(sockets);
        check_config_rc(unsafe { ffi::zlink_poller_modify(self.handle, handle, events) })
    }

    /// Remove a socket from the poller.
    fn remove_socket(&self, socket: &dyn Pollable) -> Result<(), ConfigError> {
        let handle = pollable_handle(socket)?;
        check_config_rc(unsafe { ffi::zlink_poller_remove(self.handle, handle) })?;
        if let Some(events) = self
            .sockets
            .lock()
            .expect("poller sockets")
            .remove(&(handle as usize))
        {
            if events & POLLCOMPLETION != 0 {
                release_external_progress(handle);
            }
        }
        Ok(())
    }

    fn add_fd(&self, fd: RawFd, events: i16, slot: usize) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_add_fd(
                self.handle,
                fd as ffi::zlink_fd_t,
                slot as *mut c_void,
                events,
            )
        })
    }

    fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_modify_fd(self.handle, fd as ffi::zlink_fd_t, events)
        })
    }

    fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_poller_remove_fd(self.handle, fd as ffi::zlink_fd_t) })
    }

    fn add_timer(&self, timer: &Timer, slot: usize) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_add_timer(
                self.handle,
                timer_native_handle(timer),
                slot as *mut c_void,
            )
        })
    }

    fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError> {
        check_config_rc(unsafe {
            ffi::zlink_poller_remove_timer(self.handle, timer_native_handle(timer))
        })
    }

    /// Wait for events on any registered source.
    ///
    /// `timeout_ms`: -1 = block indefinitely, 0 = return immediately,
    /// positive = timeout in milliseconds.
    ///
    /// Returns the number of entries written to `events`.
    fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError> {
        if events.is_empty() {
            return Err(crate::error::RecvError::new(
                crate::error::RecvResult::InternalError,
                libc::EINVAL,
            ));
        }
        let mut raw_events = vec![
            ffi::zlink_poller_event_t {
                source_kind: ffi::zlink_poller_source_kind_t::ZLINK_POLLER_SOURCE_SOCKET,
                socket: std::ptr::null_mut(),
                fd: 0,
                timer: std::ptr::null_mut(),
                user_data: std::ptr::null_mut(),
                events: 0,
            };
            events.len()
        ];
        let rc = unsafe {
            ffi::zlink_poller_wait(
                self.handle,
                raw_events.as_mut_ptr(),
                raw_events.len() as i32,
                timeout_ms as std::ffi::c_long,
                std::ptr::null_mut(),
            )
        };
        if rc < 0 {
            let errno = last_errno();
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
        for (dst, src) in events.iter_mut().zip(raw_events.iter()).take(rc as usize) {
            *dst = PollEvent {
                source_kind: match src.source_kind {
                    ffi::zlink_poller_source_kind_t::ZLINK_POLLER_SOURCE_FD => PollSourceKind::Fd,
                    ffi::zlink_poller_source_kind_t::ZLINK_POLLER_SOURCE_TIMER => {
                        PollSourceKind::Timer
                    }
                    _ => PollSourceKind::Socket,
                },
                fd: src.fd as RawFd,
                slot: src.user_data as usize,
                revents: src.events,
            };
        }
        Ok(rc as usize)
    }

    fn size(&self) -> i32 {
        let mut error_out = 0;
        unsafe { ffi::zlink_poller_size(self.handle, &mut error_out) }
    }
}

impl Drop for NativePoller {
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

pub fn poll(items: &mut [PollItem], timeout_ms: i64) -> Result<i32, RecvError> {
    let mut raw: Vec<ffi::zlink_pollitem_t> = items
        .iter()
        .map(|item| ffi::zlink_pollitem_t {
            socket: std::ptr::null_mut(),
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
            last_errno(),
        ));
    }
    for (idx, item) in items.iter_mut().enumerate() {
        item.revents = raw[idx].revents;
    }
    Ok(rc)
}

struct NativeTimer {
    handle: *mut c_void,
    callback: Option<crate::socket::CallbackBox>,
}

unsafe impl Send for NativeTimer {}

pub(crate) fn timer_new() -> Result<Timer, ConfigError> {
    let handle = unsafe { ffi::zlink_timer_new() };
    if handle.is_null() {
        return Err(crate::error::ConfigError::new(
            crate::error::ConfigResult::InvalidArgument,
            last_errno(),
        ));
    }
    Ok(Timer {
        inner: Box::new(NativeTimer {
            handle,
            callback: None,
        }),
    })
}

impl TimerRuntime for NativeTimer {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_timer_start(self.handle, interval_ns, repeat_count) })
    }

    fn stop(&self) -> Result<(), ConfigError> {
        check_config_rc(unsafe { ffi::zlink_timer_stop(self.handle) })
    }

    /// Receive a timer fire count. Returns `Ok(None)` when no data is available (EAGAIN).
    fn recv(&self) -> Result<Option<u64>, RecvError> {
        let mut count = 0u64;
        let rc = unsafe { ffi::zlink_timer_recv(self.handle, &mut count) };
        if rc != 0 {
            let errno = last_errno();
            if errno == libc::EAGAIN {
                return Ok(None);
            }
            return Err(check_recv_rc(rc).unwrap_err());
        }
        Ok(Some(count))
    }

    fn on_fire(&mut self, timer_ptr: usize, handler: TimerFireHandler) -> Result<(), HandlerError> {
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
}

impl Drop for NativeTimer {
    fn drop(&mut self) {
        unsafe {
            let mut handle = self.handle;
            let _ = ffi::zlink_timer_destroy(&mut handle);
        }
    }
}

fn timer_native_handle(timer: &Timer) -> *mut c_void {
    timer
        .inner
        .as_any()
        .downcast_ref::<NativeTimer>()
        .expect("zlink native timer")
        .handle
}

pub(crate) fn pollable_handle(source: &dyn Pollable) -> Result<*mut c_void, ConfigError> {
    let any = source.as_any();
    if let Some(socket) = any.downcast_ref::<crate::PairSocket>() {
        return Ok(crate::socket::pair_handle(socket));
    }
    macro_rules! downcast_socket {
        ($ty:ident, $inner:path) => {
            if let Some(socket) = any.downcast_ref::<crate::$ty>() {
                return Ok($inner(socket).handle);
            }
        };
    }
    downcast_socket!(PubSocket, crate::socket::pub_inner);
    downcast_socket!(SubSocket, crate::socket::sub_inner);
    downcast_socket!(DealerSocket, crate::socket::dealer_inner);
    downcast_socket!(RouterSocket, crate::socket::router_inner);
    downcast_socket!(XPubSocket, crate::socket::xpub_inner);
    downcast_socket!(XSubSocket, crate::socket::xsub_inner);
    downcast_socket!(StreamSocket, crate::socket::stream_inner);
    Err(ConfigError::new(
        crate::error::ConfigResult::InvalidArgument,
        libc::EINVAL,
    ))
}

macro_rules! impl_pollable {
    ($ty:ident) => {
        impl Pollable for crate::$ty {
            fn as_any(&self) -> &dyn std::any::Any {
                self
            }
        }
    };
}

impl_pollable!(PubSocket);
impl_pollable!(SubSocket);
impl_pollable!(DealerSocket);
impl_pollable!(RouterSocket);
impl_pollable!(XPubSocket);
impl_pollable!(XSubSocket);
impl_pollable!(StreamSocket);

impl Pollable for crate::PairSocket {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }
}
