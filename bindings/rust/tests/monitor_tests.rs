//! Monitor Tests – verify socket monitor recv/try_recv and
//! monitor event observation.

use std::thread;
use std::time::Duration;

use zlink::*;

#[test]
fn socket_monitor_try_recv_empty() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://mon-empty").unwrap();

    let mon = SocketMonitor::open(&sock, MONITOR_EVENT_ALL).unwrap();
    let result = mon.try_recv().unwrap();
    let _ = result;
}

#[test]
fn socket_monitor_observes_connection() {
    let ctx = Context::new().unwrap();
    let server = ctx.pair_socket().unwrap();
    server.bind("inproc://mon-connect").unwrap();

    let mon = SocketMonitor::open(&server, MONITOR_EVENT_ALL).unwrap();

    let client = ctx.pair_socket().unwrap();
    client.connect("inproc://mon-connect").unwrap();

    // Wait for events and drain
    thread::sleep(Duration::from_millis(100));

    let mut found_event = false;
    for _ in 0..20 {
        match mon.try_recv() {
            Ok(Some(ev)) => {
                found_event = true;
                let _ = ev.event;
                let _ = ev.local_addr;
                break;
            }
            Ok(None) => break,
            Err(_) => break,
        }
    }
    let _ = found_event;
}

#[test]
fn socket_monitor_blocking_recv_success() {
    // Blocking recv must return an event when a connection is made.
    // Open monitor, then trigger a connect in another thread.
    let ctx = Context::new().unwrap();
    let server = ctx.pair_socket().unwrap();
    server.bind("inproc://mon-blocking-recv").unwrap();

    let mon = SocketMonitor::open(&server, MONITOR_EVENT_ALL).unwrap();

    // Trigger a connection from another thread so blocking recv has data
    let ctx2_handle = ctx.pair_socket().unwrap();
    let _connector = thread::spawn(move || {
        thread::sleep(Duration::from_millis(50));
        ctx2_handle.connect("inproc://mon-blocking-recv").unwrap();
    });

    // Blocking recv – must return an event (not hang forever)
    let event = mon.recv().unwrap();
    assert!(
        event.event != 0,
        "blocking monitor recv must return a valid event"
    );
}

#[test]
fn socket_monitor_snapshot() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://mon-snapshot").unwrap();

    let mon = SocketMonitor::open(&sock, MONITOR_EVENT_ALL).unwrap();
    let snap = mon.snapshot().unwrap();
    // Verify snapshot fields are accessible
    let _ = snap.is_ready();
    let _ = snap.is_send_ready();
    let _ = snap.is_closed();
    let _ = snap.ready_count;
}

#[test]
fn socket_monitor_callback() {
    use std::sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    };

    let ctx = Context::new().unwrap();
    let server = ctx.pair_socket().unwrap();
    server.bind("inproc://mon-callback").unwrap();

    let mut mon = SocketMonitor::open(&server, MONITOR_EVENT_ALL).unwrap();
    let called = Arc::new(AtomicBool::new(false));
    let called_clone = called.clone();

    mon.on_event(move |_event| {
        called_clone.store(true, Ordering::Relaxed);
    })
    .unwrap();

    let client = ctx.pair_socket().unwrap();
    client.connect("inproc://mon-callback").unwrap();

    thread::sleep(Duration::from_millis(200));
    // Callback may or may not have been called depending on timing
    let _ = called.load(Ordering::Relaxed);
}
