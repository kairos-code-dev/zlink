use std::collections::HashMap;
use std::ffi::c_void;
use std::mem::MaybeUninit;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock, Weak};
use std::thread;
use std::time::Duration;

use crate::ffi;

const POLL_COMPLETION: i16 = 32;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
enum ProgressKind {
    Socket,
}

struct ProgressWorker {
    handle: usize,
    pending: AtomicUsize,
    gate: Mutex<()>,
    wake: Condvar,
}

type ProgressKey = (ProgressKind, usize);
type ProgressWorkerMap = HashMap<ProgressKey, Weak<ProgressWorker>>;

static WORKERS: OnceLock<Mutex<ProgressWorkerMap>> = OnceLock::new();
static EXTERNAL_PROGRESS: OnceLock<Mutex<HashMap<usize, usize>>> = OnceLock::new();

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
    let refs = EXTERNAL_PROGRESS.get_or_init(|| Mutex::new(HashMap::new()));
    let mut refs = refs.lock().expect("external progress map");
    *refs.entry(handle as usize).or_insert(0) += 1;
}

pub(crate) fn release_external_progress(handle: *mut c_void) {
    if handle.is_null() {
        return;
    }
    let Some(refs) = EXTERNAL_PROGRESS.get() else {
        return;
    };
    let mut refs = refs.lock().expect("external progress map");
    let key = handle as usize;
    if let Some(count) = refs.get_mut(&key) {
        *count = count.saturating_sub(1);
        if *count == 0 {
            refs.remove(&key);
        }
    }
}

fn external_progress_active(handle: *mut c_void) -> bool {
    let Some(refs) = EXTERNAL_PROGRESS.get() else {
        return false;
    };
    refs.lock()
        .expect("external progress map")
        .get(&(handle as usize))
        .copied()
        .unwrap_or(0)
        > 0
}

fn acquire_worker(kind: ProgressKind, handle: usize) -> Arc<ProgressWorker> {
    let workers = WORKERS.get_or_init(|| Mutex::new(HashMap::new()));
    let key = (kind, handle);
    let mut workers = workers.lock().expect("request progress worker map");
    if let Some(worker) = workers.get(&key).and_then(Weak::upgrade) {
        return worker;
    }
    let worker = Arc::new(ProgressWorker {
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
    thread::spawn(move || run_worker(weak));
}

fn run_worker(worker: Weak<ProgressWorker>) {
    loop {
        let Some(worker_ref) = worker.upgrade() else {
            return;
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
                return;
            }
        }
        drop(gate);

        loop {
            let Some(active_worker) = worker.upgrade() else {
                return;
            };
            if active_worker.pending.load(Ordering::Acquire) == 0 {
                break;
            }
            unsafe {
                let poller = ffi::zlink_poller_new();
                if poller.is_null() {
                    thread::yield_now();
                    continue;
                }
                if ffi::zlink_poller_add(
                    poller,
                    active_worker.handle as *mut c_void,
                    std::ptr::null_mut(),
                    POLL_COMPLETION,
                ) == 0
                {
                    let mut event = MaybeUninit::<ffi::zlink_poller_event_t>::uninit();
                    while active_worker.pending.load(Ordering::Acquire) > 0 {
                        let _ = ffi::zlink_poller_wait(
                            poller,
                            event.as_mut_ptr(),
                            1,
                            -1,
                            std::ptr::null_mut(),
                        );
                    }
                    let _ = ffi::zlink_poller_remove(poller, active_worker.handle as *mut c_void);
                }
                let mut poller_to_destroy = poller;
                let _ = ffi::zlink_poller_destroy(&mut poller_to_destroy);
            }
            thread::yield_now();
        }
    }
}
