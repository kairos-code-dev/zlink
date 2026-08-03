// SPDX-License-Identifier: MPL-2.0

//! Crate-private resource storage shared by the public contracts and runtime.
//!
//! The public contract owns the handle wrappers, while this module owns the
//! concrete state required to execute them.  Keeping that state out of the
//! runtime modules prevents contract code from depending on runtime resource
//! types without reintroducing a per-handle trait object.

use std::cell::UnsafeCell;
use std::collections::HashMap;
use std::ffi::c_void;
use std::sync::Mutex;

use crate::ffi;

pub(crate) struct CallbackBox {
    pub(crate) data: *mut c_void,
    pub(crate) drop_fn: unsafe fn(*mut c_void),
}

unsafe impl Send for CallbackBox {}

impl CallbackBox {
    pub(crate) fn new<F: 'static>(f: F) -> (Self, *mut c_void) {
        let ptr = Box::into_raw(Box::new(f));
        let callback = Self {
            data: ptr as *mut c_void,
            drop_fn: drop_erased::<F>,
        };
        (callback, ptr as *mut c_void)
    }
}

impl Drop for CallbackBox {
    fn drop(&mut self) {
        unsafe { (self.drop_fn)(self.data) }
    }
}

unsafe fn drop_erased<F>(ptr: *mut c_void) {
    drop(unsafe { Box::from_raw(ptr as *mut F) });
}

pub(crate) struct ContextStorage {
    pub(crate) handle: *mut c_void,
    /// Cached thread name prefix (write-only in the C API; readable from cache).
    pub(crate) thread_name_prefix: Mutex<String>,
}

unsafe impl Send for ContextStorage {}
unsafe impl Sync for ContextStorage {}

pub(crate) struct PollerStorage {
    pub(crate) handle: *mut c_void,
    pub(crate) sockets: Mutex<HashMap<usize, i16>>,
    // Poller is Send but not Sync, so a wait call can reuse this ABI scratch
    // buffer without adding a mutex to every hot-path wait.
    pub(crate) raw_events: UnsafeCell<Vec<ffi::zlink_poller_event_t>>,
}

unsafe impl Send for PollerStorage {}

pub(crate) struct TimerStorage {
    pub(crate) handle: *mut c_void,
    pub(crate) callback: Option<CallbackBox>,
}

unsafe impl Send for TimerStorage {}

pub(crate) struct MonitorStorage {
    pub(crate) handle: *mut c_void,
    pub(crate) callback: Option<CallbackBox>,
}

unsafe impl Send for MonitorStorage {}

pub(crate) struct SocketStorage {
    pub(crate) handle: *mut c_void,
    pub(crate) send_ready_cb: Option<CallbackBox>,
    pub(crate) packet_cb: Option<CallbackBox>,
}

unsafe impl Send for SocketStorage {}
