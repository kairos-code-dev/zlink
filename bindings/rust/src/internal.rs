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
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Condvar, Mutex, OnceLock};
use std::thread;
use std::time::Duration;

use crate::ffi;

pub(crate) struct CallbackBox {
    pub(crate) data: *mut c_void,
    pub(crate) drop_fn: unsafe fn(*mut c_void),
    set_closing_fn: unsafe fn(*mut c_void, bool),
    is_idle_fn: unsafe fn(*mut c_void) -> bool,
    wait_idle_fn: unsafe fn(*mut c_void),
}

unsafe impl Send for CallbackBox {}

impl CallbackBox {
    pub(crate) fn new<F: Send + 'static>(f: F) -> (Self, *mut c_void) {
        let ptr = Box::into_raw(Box::new(CallbackState::new(f)));
        let callback = Self {
            data: ptr as *mut c_void,
            drop_fn: drop_state::<F>,
            set_closing_fn: set_state_closing::<F>,
            is_idle_fn: state_is_idle::<F>,
            wait_idle_fn: state_wait_idle::<F>,
        };
        (callback, ptr as *mut c_void)
    }

    pub(crate) fn set_closing(&self, closing: bool) {
        unsafe { (self.set_closing_fn)(self.data, closing) }
    }

    pub(crate) fn is_idle(&self) -> bool {
        unsafe { (self.is_idle_fn)(self.data) }
    }

    pub(crate) fn wait_idle(&self) {
        unsafe { (self.wait_idle_fn)(self.data) }
    }

    /// Invokes an erased callback while keeping its storage alive until the
    /// invocation returns.  The callback mutex keeps the public `Send` bound
    /// sound even when a caller's closure is not `Sync`.
    pub(crate) unsafe fn invoke<F, R>(
        userdata: *mut c_void,
        invoke: impl FnOnce(&F) -> R,
    ) -> Option<R>
    where
        F: Send + 'static,
    {
        if userdata.is_null() {
            return None;
        }
        let state = unsafe { &*(userdata as *const CallbackState<F>) };
        let _active = state.enter()?;
        let result = {
            let callback = state.callback.lock().expect("callback state");
            invoke(&callback)
        };
        Some(result)
    }

    pub(crate) unsafe fn invoke_or<F, R>(
        userdata: *mut c_void,
        invoke: impl FnOnce(&F) -> R,
        fallback: impl FnOnce() -> R,
    ) -> R
    where
        F: Send + 'static,
    {
        unsafe { Self::invoke(userdata, invoke) }.unwrap_or_else(fallback)
    }
}

impl Drop for CallbackBox {
    fn drop(&mut self) {
        // All normal release paths wait for active invocations.  Keeping the
        // guard here makes an unexpected internal path safe as well; callback
        // callbacks themselves hand the box to the deferred worker instead of
        // dropping it recursively.
        self.set_closing(true);
        self.wait_idle();
        unsafe { (self.drop_fn)(self.data) }
    }
}

struct CallbackState<F> {
    callback: Mutex<F>,
    state: AtomicUsize,
    idle: Condvar,
    idle_guard: Mutex<()>,
}

const CALLBACK_CLOSING: usize = 1usize << (usize::BITS - 1);
const CALLBACK_ACTIVE_MASK: usize = !CALLBACK_CLOSING;

impl<F> CallbackState<F> {
    fn new(callback: F) -> Self {
        Self {
            callback: Mutex::new(callback),
            state: AtomicUsize::new(0),
            idle: Condvar::new(),
            idle_guard: Mutex::new(()),
        }
    }

    fn enter(&self) -> Option<CallbackGuard<'_, F>> {
        // A CAS reserves the active reference before close can observe zero;
        // setting CALLBACK_CLOSING makes every later CAS fail without taking
        // a mutex on the callback hot path.
        let mut state = self.state.load(Ordering::Acquire);
        loop {
            if state & CALLBACK_CLOSING != 0 {
                return None;
            }
            if state & CALLBACK_ACTIVE_MASK == CALLBACK_ACTIVE_MASK {
                return None;
            }
            match self.state.compare_exchange_weak(
                state,
                state + 1,
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(_) => return Some(CallbackGuard { state: self }),
                Err(next) => state = next,
            }
        }
    }

    fn leave(&self) {
        let previous = self.state.fetch_sub(1, Ordering::AcqRel);
        debug_assert_ne!(previous & CALLBACK_ACTIVE_MASK, 0);
        if previous & CALLBACK_ACTIVE_MASK == 1 {
            self.idle.notify_all();
        }
    }

    fn set_closing(&self, closing: bool) {
        if closing {
            self.state.fetch_or(CALLBACK_CLOSING, Ordering::AcqRel);
        } else {
            self.state
                .fetch_and(CALLBACK_ACTIVE_MASK, Ordering::Release);
        }
        if !closing {
            self.idle.notify_all();
        }
    }

    fn is_idle(&self) -> bool {
        self.state.load(Ordering::Acquire) & CALLBACK_ACTIVE_MASK == 0
    }

    fn wait_idle(&self) {
        if self.is_idle() {
            return;
        }
        let mut guard = self.idle_guard.lock().expect("callback idle state");
        while !self.is_idle() {
            guard = self.idle.wait(guard).expect("callback idle wait");
        }
    }
}

struct CallbackGuard<'a, F> {
    state: &'a CallbackState<F>,
}

impl<F> Drop for CallbackGuard<'_, F> {
    fn drop(&mut self) {
        self.state.leave();
    }
}

unsafe fn drop_state<F>(ptr: *mut c_void) {
    drop(unsafe { Box::from_raw(ptr as *mut CallbackState<F>) });
}

unsafe fn set_state_closing<F>(ptr: *mut c_void, closing: bool) {
    unsafe { (*(ptr as *const CallbackState<F>)).set_closing(closing) };
}

unsafe fn state_is_idle<F>(ptr: *mut c_void) -> bool {
    unsafe { (*(ptr as *const CallbackState<F>)).is_idle() }
}

unsafe fn state_wait_idle<F>(ptr: *mut c_void) {
    unsafe { (*(ptr as *const CallbackState<F>)).wait_idle() };
}

#[derive(Clone, Copy)]
pub(crate) enum DeferredCloseKind {
    Socket,
    Monitor,
    Timer,
}

enum DeferredCleanupAction {
    ReleaseCallbacks,
    CloseNative {
        kind: DeferredCloseKind,
        handle: *mut c_void,
    },
}

struct DeferredCleanup {
    action: DeferredCleanupAction,
    callbacks: Vec<CallbackBox>,
}

unsafe impl Send for DeferredCleanup {}

static DEFERRED_CLEANUP_QUEUE: OnceLock<(Mutex<Vec<DeferredCleanup>>, Condvar)> = OnceLock::new();
static DEFERRED_CLEANUP_WORKER: OnceLock<()> = OnceLock::new();

fn deferred_cleanup_queue() -> &'static (Mutex<Vec<DeferredCleanup>>, Condvar) {
    DEFERRED_CLEANUP_QUEUE.get_or_init(|| (Mutex::new(Vec::new()), Condvar::new()))
}

fn start_deferred_cleanup_worker() {
    DEFERRED_CLEANUP_WORKER.get_or_init(|| {
        let _ = thread::Builder::new()
            .name("zlink-rust-cleanup".to_owned())
            .spawn(deferred_cleanup_loop);
    });
}

fn enqueue_deferred_cleanup(cleanup: DeferredCleanup) {
    start_deferred_cleanup_worker();
    let queue = deferred_cleanup_queue();
    queue
        .0
        .lock()
        .expect("deferred cleanup queue")
        .push(cleanup);
    queue.1.notify_one();
}

fn deferred_cleanup_loop() {
    loop {
        let cleanup = {
            let queue = deferred_cleanup_queue();
            let mut pending = queue.0.lock().expect("deferred cleanup queue");
            while pending.is_empty() {
                pending = queue.1.wait(pending).expect("deferred cleanup wait");
            }
            pending.pop().expect("deferred cleanup item")
        };

        if let Some(cleanup) = try_deferred_cleanup(cleanup) {
            let queue = deferred_cleanup_queue();
            let guard = queue.0.lock().expect("deferred cleanup queue");
            let (mut pending, _) = queue
                .1
                .wait_timeout(guard, Duration::from_millis(10))
                .expect("deferred cleanup backoff wait");
            pending.push(cleanup);
            queue.1.notify_one();
        }
    }
}

fn try_deferred_cleanup(mut cleanup: DeferredCleanup) -> Option<DeferredCleanup> {
    if !cleanup.callbacks.iter().all(CallbackBox::is_idle) {
        return Some(cleanup);
    }

    match cleanup.action {
        DeferredCleanupAction::ReleaseCallbacks => None,
        DeferredCleanupAction::CloseNative { kind, handle } => {
            let rc = unsafe {
                match kind {
                    DeferredCloseKind::Socket => ffi::zlink_close(handle),
                    DeferredCloseKind::Monitor => {
                        let mut handle = handle;
                        ffi::zlink_monitor_close(&mut handle)
                    }
                    DeferredCloseKind::Timer => {
                        let mut handle = handle;
                        ffi::zlink_timer_destroy(&mut handle)
                    }
                }
            };
            if rc == 0 {
                cleanup.action = DeferredCleanupAction::ReleaseCallbacks;
                if cleanup.callbacks.iter().all(CallbackBox::is_idle) {
                    None
                } else {
                    Some(cleanup)
                }
            } else {
                // Drop cannot report a close failure.  Retain both the native
                // handle and callback userdata until Core accepts the retry;
                // this is safer than freeing userdata that Core may still call.
                Some(cleanup)
            }
        }
    }
}

pub(crate) fn release_callbacks(mut callbacks: Vec<CallbackBox>) {
    for callback in &callbacks {
        callback.set_closing(true);
    }
    if callbacks.iter().all(CallbackBox::is_idle) {
        callbacks.clear();
    } else {
        enqueue_deferred_cleanup(DeferredCleanup {
            action: DeferredCleanupAction::ReleaseCallbacks,
            callbacks,
        });
    }
}

pub(crate) fn defer_native_close(
    kind: DeferredCloseKind,
    handle: *mut c_void,
    callbacks: Vec<CallbackBox>,
) {
    for callback in &callbacks {
        callback.set_closing(true);
    }
    enqueue_deferred_cleanup(DeferredCleanup {
        action: DeferredCleanupAction::CloseNative { kind, handle },
        callbacks,
    });
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
    pub(crate) callback: Mutex<Option<CallbackBox>>,
}

unsafe impl Send for TimerStorage {}
unsafe impl Sync for TimerStorage {}

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

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Barrier};

    #[test]
    fn callback_close_waits_for_active_invocation() {
        let entered = Arc::new(Barrier::new(2));
        let release = Arc::new(Barrier::new(2));
        let callback: Box<dyn Fn() + Send> = {
            let entered = Arc::clone(&entered);
            let release = Arc::clone(&release);
            Box::new(move || {
                entered.wait();
                release.wait();
            })
        };
        let (callback_box, userdata) = CallbackBox::new(callback);
        let userdata = userdata as usize;
        let join = std::thread::spawn(move || unsafe {
            CallbackBox::invoke::<Box<dyn Fn() + Send>, _>(userdata as *mut c_void, |callback| {
                callback()
            })
        });

        entered.wait();
        callback_box.set_closing(true);
        assert!(!callback_box.is_idle());
        release.wait();
        assert!(join.join().expect("callback thread").is_some());
        assert!(callback_box.is_idle());
        assert!(
            unsafe {
                CallbackBox::invoke::<Box<dyn Fn() + Send>, _>(
                    userdata as *mut c_void,
                    |callback| callback(),
                )
            }
            .is_none()
        );
    }
}
