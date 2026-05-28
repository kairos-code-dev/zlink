use std::any::Any;
use std::os::fd::RawFd;

use crate::error::{ConfigError, HandlerError, RecvError};

/// Poll event flags.
pub const POLLIN: i16 = 1;
pub const POLLOUT: i16 = 2;
pub const POLLCOMPLETION: i16 = 32;

pub trait Pollable: Any {
    fn as_any(&self) -> &dyn Any;
}

pub(crate) type TimerFireHandler = Box<dyn Fn(&Timer, u64) + Send>;

pub(crate) trait PollerRuntime: Send {
    fn add_socket(
        &self,
        socket: &dyn Pollable,
        events: i16,
        slot: usize,
    ) -> Result<(), ConfigError>;
    fn modify_socket(&self, socket: &dyn Pollable, events: i16) -> Result<(), ConfigError>;
    fn remove_socket(&self, socket: &dyn Pollable) -> Result<(), ConfigError>;
    fn add_fd(&self, fd: RawFd, events: i16, slot: usize) -> Result<(), ConfigError>;
    fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError>;
    fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError>;
    fn add_timer(&self, timer: &Timer, slot: usize) -> Result<(), ConfigError>;
    fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError>;
    fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError>;
    fn size(&self) -> i32;
}

pub(crate) trait TimerRuntime: Any + Send {
    fn as_any(&self) -> &dyn Any;
    fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError>;
    fn stop(&self) -> Result<(), ConfigError>;
    fn recv(&self) -> Result<Option<u64>, RecvError>;
    fn on_fire(&mut self, timer_ptr: usize, handler: TimerFireHandler) -> Result<(), HandlerError>;
}

/// Source kind reported by the poller when an event fires.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PollSourceKind {
    Socket,
    Fd,
    Timer,
}

/// A single poll result event.
#[derive(Debug, Clone, Copy)]
pub struct PollEvent {
    pub source_kind: PollSourceKind,
    pub fd: RawFd,
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
            fd: 0,
            slot: 0,
            revents: 0,
        }
    }
}

#[derive(Clone, Copy)]
pub struct PollItem {
    pub fd: RawFd,
    pub events: i16,
    pub revents: i16,
}

/// Poller for monitoring socket readiness, file descriptors, and timers.
pub struct Poller {
    pub(crate) inner: Box<dyn PollerRuntime>,
}

impl Poller {
    pub fn add_socket(
        &self,
        socket: &dyn Pollable,
        events: i16,
        slot: usize,
    ) -> Result<(), ConfigError> {
        self.inner.add_socket(socket, events, slot)
    }

    /// Modify the event mask for a previously added socket.
    pub fn modify_socket(&self, socket: &dyn Pollable, events: i16) -> Result<(), ConfigError> {
        self.inner.modify_socket(socket, events)
    }

    /// Remove a socket from the poller.
    pub fn remove_socket(&self, socket: &dyn Pollable) -> Result<(), ConfigError> {
        self.inner.remove_socket(socket)
    }

    pub fn add_fd(&self, fd: RawFd, events: i16, slot: usize) -> Result<(), ConfigError> {
        self.inner.add_fd(fd, events, slot)
    }

    pub fn modify_fd(&self, fd: RawFd, events: i16) -> Result<(), ConfigError> {
        self.inner.modify_fd(fd, events)
    }

    pub fn remove_fd(&self, fd: RawFd) -> Result<(), ConfigError> {
        self.inner.remove_fd(fd)
    }

    pub fn add_timer(&self, timer: &Timer, slot: usize) -> Result<(), ConfigError> {
        self.inner.add_timer(timer, slot)
    }

    pub fn remove_timer(&self, timer: &Timer) -> Result<(), ConfigError> {
        self.inner.remove_timer(timer)
    }

    /// Wait for events on any registered source.
    pub fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError> {
        self.inner.wait(events, timeout_ms)
    }

    pub fn size(&self) -> i32 {
        self.inner.size()
    }
}

pub struct Timer {
    pub(crate) inner: Box<dyn TimerRuntime>,
}

impl Timer {
    pub fn start(&self, interval_ns: u64, repeat_count: u64) -> Result<(), ConfigError> {
        self.inner.start(interval_ns, repeat_count)
    }

    pub fn stop(&self) -> Result<(), ConfigError> {
        self.inner.stop()
    }

    /// Receive a timer fire count. Returns `Ok(None)` when no data is available.
    pub fn recv(&self) -> Result<Option<u64>, RecvError> {
        self.inner.recv()
    }

    pub fn on_fire<F>(&mut self, handler: F) -> Result<(), HandlerError>
    where
        F: Fn(&Timer, u64) + Send + 'static,
    {
        let timer_ptr = self as *const Timer as usize;
        self.inner.on_fire(timer_ptr, Box::new(handler))
    }
}
