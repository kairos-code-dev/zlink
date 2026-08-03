use std::collections::HashMap;
use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock, RwLock, Weak};
use std::thread;
use std::time::Duration;

use crate::ffi;

const POLL_COMPLETION: i16 = 32;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
enum ProgressKind {
    Socket,
}

struct ProgressWorker {
    key: ProgressKey,
    handle: usize,
    pending: AtomicUsize,
    gate: Mutex<()>,
    wake: Condvar,
}

type ProgressKey = (ProgressKind, usize);
type ProgressWorkerMap = HashMap<ProgressKey, Weak<ProgressWorker>>;

static WORKERS: OnceLock<RwLock<ProgressWorkerMap>> = OnceLock::new();
static EXTERNAL_PROGRESS: OnceLock<RwLock<HashMap<usize, ProgressOwnership>>> = OnceLock::new();
static PROGRESS_CHANGE: OnceLock<(Mutex<()>, Condvar)> = OnceLock::new();

#[derive(Default)]
struct ProgressOwnership {
    external_refs: usize,
    internal_active: bool,
}

pub(crate) struct RequestProgressGuard {
    worker: Option<Arc<ProgressWorker>>,
}

impl RequestProgressGuard {
    pub(crate) fn attach_socket(handle: *mut c_void) -> Self {
        Self::attach(ProgressKind::Socket, handle)
    }

    fn attach(kind: ProgressKind, handle: *mut c_void) -> Self {
        if external_progress_active(handle) {
            return Self { worker: None };
        }
        let worker = acquire_worker(kind, handle as usize);
        worker.pending.fetch_add(1, Ordering::AcqRel);
        worker.wake.notify_one();
        Self {
            worker: Some(worker),
        }
    }
}

impl Drop for RequestProgressGuard {
    fn drop(&mut self) {
        if let Some(worker) = &self.worker {
            worker.pending.fetch_sub(1, Ordering::AcqRel);
            worker.wake.notify_one();
        }
    }
}

pub(crate) fn acquire_external_progress(handle: *mut c_void) {
    if handle.is_null() {
        return;
    }
    let refs = EXTERNAL_PROGRESS.get_or_init(|| RwLock::new(HashMap::new()));
    {
        let mut refs = refs.write().expect("external progress map");
        refs.entry(handle as usize).or_default().external_refs += 1;
    }
    notify_worker(handle as usize);

    // The internal worker may currently be blocked in the native poller.  Do
    // not let the caller finish registering the external poller until the
    // worker has released its native completion registration.  This keeps the
    // two completion owners from observing the same socket concurrently.
    loop {
        let internal_active = refs
            .read()
            .expect("external progress map")
            .get(&(handle as usize))
            .is_some_and(|state| state.internal_active);
        if !internal_active {
            return;
        }
        let change = PROGRESS_CHANGE.get_or_init(|| (Mutex::new(()), Condvar::new()));
        let guard = change.0.lock().expect("external progress change");
        let _ = change
            .1
            .wait_timeout(guard, Duration::from_millis(100))
            .expect("external progress change wait");
    }
}

pub(crate) fn release_external_progress(handle: *mut c_void) {
    if handle.is_null() {
        return;
    }
    let Some(refs) = EXTERNAL_PROGRESS.get() else {
        return;
    };
    let mut refs = refs.write().expect("external progress map");
    let key = handle as usize;
    if let Some(state) = refs.get_mut(&key) {
        state.external_refs = state.external_refs.saturating_sub(1);
        if state.external_refs == 0 && !state.internal_active {
            refs.remove(&key);
        }
    }
    drop(refs);
    notify_worker(key);
    notify_progress_change();
}

pub(crate) fn external_progress_active(handle: *mut c_void) -> bool {
    let Some(refs) = EXTERNAL_PROGRESS.get() else {
        return false;
    };
    refs.read()
        .expect("external progress map")
        .get(&(handle as usize))
        .is_some_and(|state| state.external_refs > 0)
}

fn notify_worker(handle: usize) {
    let Some(workers) = WORKERS.get() else {
        return;
    };
    let Some(worker) = workers
        .read()
        .expect("request progress worker map")
        .get(&(ProgressKind::Socket, handle))
        .and_then(Weak::upgrade)
    else {
        return;
    };
    worker.wake.notify_one();
}

fn notify_progress_change() {
    if let Some(change) = PROGRESS_CHANGE.get() {
        change.1.notify_all();
    }
}

fn claim_internal_progress(handle: *mut c_void) -> bool {
    let refs = EXTERNAL_PROGRESS.get_or_init(|| RwLock::new(HashMap::new()));
    let mut refs = refs.write().expect("external progress map");
    let state = refs.entry(handle as usize).or_default();
    if state.external_refs != 0 || state.internal_active {
        return false;
    }
    state.internal_active = true;
    true
}

fn release_internal_progress(handle: *mut c_void) {
    let Some(refs) = EXTERNAL_PROGRESS.get() else {
        return;
    };
    let mut refs = refs.write().expect("external progress map");
    let key = handle as usize;
    if let Some(state) = refs.get_mut(&key) {
        state.internal_active = false;
        if state.external_refs == 0 {
            refs.remove(&key);
        }
    }
    drop(refs);
    notify_worker(key);
    notify_progress_change();
}

fn acquire_worker(kind: ProgressKind, handle: usize) -> Arc<ProgressWorker> {
    let workers = WORKERS.get_or_init(|| RwLock::new(HashMap::new()));
    let key = (kind, handle);
    if let Some(worker) = workers
        .read()
        .expect("request progress worker map")
        .get(&key)
        .and_then(Weak::upgrade)
    {
        return worker;
    }
    let mut workers = workers.write().expect("request progress worker map");
    if let Some(worker) = workers.get(&key).and_then(Weak::upgrade) {
        return worker;
    }
    let worker = Arc::new(ProgressWorker {
        key,
        handle,
        pending: AtomicUsize::new(0),
        gate: Mutex::new(()),
        wake: Condvar::new(),
    });
    workers.insert(key, Arc::downgrade(&worker));
    spawn_worker(&worker);
    worker
}

fn spawn_worker(worker: &Arc<ProgressWorker>) {
    let weak = Arc::downgrade(worker);
    let key = worker.key;
    thread::spawn(move || run_worker(key, weak));
}

struct ProgressPoller {
    handle: *mut c_void,
    socket: *mut c_void,
}

impl ProgressPoller {
    fn new(socket: *mut c_void) -> Option<Self> {
        if !claim_internal_progress(socket) {
            return None;
        }
        let handle = unsafe { ffi::zlink_poller_new() };
        if handle.is_null() {
            release_internal_progress(socket);
            return None;
        }
        let added =
            unsafe { ffi::zlink_poller_add(handle, socket, std::ptr::null_mut(), POLL_COMPLETION) }
                == 0;
        if !added {
            unsafe {
                let mut handle = handle;
                let _ = ffi::zlink_poller_destroy(&mut handle);
            }
            release_internal_progress(socket);
            return None;
        }
        Some(Self { handle, socket })
    }

    fn wait(&self) {
        let mut event = MaybeUninit::<ffi::zlink_poller_event_t>::uninit();
        unsafe {
            // A bounded wait lets the worker observe a dropped final request
            // even when no completion event is generated for the socket.
            let _ = ffi::zlink_poller_wait(
                self.handle,
                event.as_mut_ptr(),
                1,
                100,
                std::ptr::null_mut(),
            );
        }
    }
}

impl Drop for ProgressPoller {
    fn drop(&mut self) {
        unsafe {
            let _ = ffi::zlink_poller_remove(self.handle, self.socket);
            let mut handle = self.handle;
            let _ = ffi::zlink_poller_destroy(&mut handle);
        }
        release_internal_progress(self.socket);
    }
}

fn run_worker(key: ProgressKey, worker: Weak<ProgressWorker>) {
    let mut poller = None;

    'worker: loop {
        let Some(worker_ref) = worker.upgrade() else {
            break;
        };
        let mut gate = worker_ref.gate.lock().expect("request progress gate");
        while worker_ref.pending.load(Ordering::Acquire) == 0 {
            let (next_gate, timeout) = worker_ref
                .wake
                .wait_timeout(gate, Duration::from_secs(30))
                .expect("request progress wait");
            gate = next_gate;
            if timeout.timed_out()
                && worker_ref.pending.load(Ordering::Acquire) == 0
                && Arc::strong_count(&worker_ref) == 1
            {
                break 'worker;
            }
        }
        drop(gate);

        loop {
            let Some(active_worker) = worker.upgrade() else {
                break 'worker;
            };
            if active_worker.pending.load(Ordering::Acquire) == 0 {
                break;
            }
            if external_progress_active(active_worker.handle as *mut c_void) {
                // External Poller registration owns completion dispatch while
                // it is present.  Release the internal native poller before
                // waiting for the external owner to be removed.
                poller.take();
                let gate = active_worker.gate.lock().expect("request progress gate");
                let _ = active_worker
                    .wake
                    .wait_timeout(gate, Duration::from_millis(100))
                    .expect("request progress external owner wait");
                continue;
            }
            if poller.is_none() {
                poller = ProgressPoller::new(active_worker.handle as *mut c_void);
                if poller.is_none() {
                    if external_progress_active(active_worker.handle as *mut c_void) {
                        continue;
                    }
                    let gate = active_worker.gate.lock().expect("request progress gate");
                    let _ = active_worker
                        .wake
                        .wait_timeout(gate, Duration::from_millis(100))
                        .expect("request progress retry wait");
                    continue;
                }
            }
            if let Some(poller) = poller.as_ref() {
                poller.wait();
            }
        }
    }

    remove_worker(key, &worker);
}

fn remove_worker(key: ProgressKey, worker: &Weak<ProgressWorker>) {
    let Some(workers) = WORKERS.get() else {
        return;
    };
    let mut workers = workers.write().expect("request progress worker map");
    if workers
        .get(&key)
        .is_some_and(|current| Weak::ptr_eq(current, worker))
    {
        workers.remove(&key);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::mpsc;
    use std::time::Duration;

    #[test]
    fn external_completion_waits_for_internal_owner_release() {
        let handle = 0x7f00_0000usize;
        assert!(claim_internal_progress(handle as *mut c_void));

        let (tx, rx) = mpsc::channel();
        let join = thread::spawn(move || {
            acquire_external_progress(handle as *mut c_void);
            tx.send(()).unwrap();
        });

        assert!(rx.recv_timeout(Duration::from_millis(20)).is_err());
        release_internal_progress(handle as *mut c_void);
        assert!(rx.recv_timeout(Duration::from_secs(1)).is_ok());
        join.join().unwrap();
        release_external_progress(handle as *mut c_void);
    }
}
