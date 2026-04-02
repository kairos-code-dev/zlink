//! Ownership Tests – verify message ownership contracts across
//! send, recv, close, and callback boundaries.

use std::thread;
use std::time::Duration;

use zlink::*;

#[test]
fn send_consumes_message_ownership() {
    let ctx = Context::new().unwrap();
    let a = ctx.pair_socket().unwrap();
    a.bind("inproc://own-send-consume").unwrap();

    let b = ctx.pair_socket().unwrap();
    b.connect("inproc://own-send-consume").unwrap();
    thread::sleep(Duration::from_millis(50));

    // After send, the message is consumed (moved into native).
    // Rust's move semantics prevent reuse at compile time.
    let msg = Message::from_bytes(b"owned-data").unwrap();
    a.send(msg).unwrap();
    // `msg` cannot be used here – Rust ownership enforced

    let received = b.recv().unwrap();
    assert_eq!(received.parts()[0].data(), b"owned-data");
}

#[test]
fn send_multipart_consumes_all_parts() {
    let ctx = Context::new().unwrap();
    let a = ctx.pair_socket().unwrap();
    a.bind("inproc://own-multi-consume").unwrap();

    let b = ctx.pair_socket().unwrap();
    b.connect("inproc://own-multi-consume").unwrap();
    thread::sleep(Duration::from_millis(50));

    let parts = vec![
        Message::from_bytes(b"part-a").unwrap(),
        Message::from_bytes(b"part-b").unwrap(),
    ];
    // Vec is consumed by send
    a.send(parts).unwrap();
}

#[test]
fn recv_ownership_transfers_to_caller() {
    let ctx = Context::new().unwrap();
    let a = ctx.pair_socket().unwrap();
    a.bind("inproc://own-recv-transfer").unwrap();

    let b = ctx.pair_socket().unwrap();
    b.connect("inproc://own-recv-transfer").unwrap();
    thread::sleep(Duration::from_millis(50));

    let msg = Message::from_bytes(b"recv-test").unwrap();
    b.send(msg).unwrap();

    let received = a.recv().unwrap();
    // Caller owns the parts; dropping them calls zlink_msg_close
    let parts = received.into_parts();
    assert_eq!(parts.len(), 1);
    assert_eq!(parts[0].data(), b"recv-test");
    // Parts dropped here – native memory freed
}

#[test]
fn unsent_message_must_be_closed() {
    // Creating a message and dropping it without send must properly close
    // the native message (RAII Drop).
    for _ in 0..1000 {
        let msg = Message::from_bytes(b"never-sent").unwrap();
        drop(msg); // Must call zlink_msg_close
    }
}

#[test]
fn send_failure_does_not_leak() {
    // If send fails, messages are still consumed (ownership transferred to
    // native on any return path per the C API contract).
    let ctx = Context::new().unwrap();
    let router = ctx.router_socket().unwrap();
    router.bind("inproc://own-send-fail").unwrap();
    router.set_mandatory(true).unwrap();
    router.set_send_timeout(Duration::from_millis(50)).unwrap();

    let rid = RoutingId::new(b"ghost").unwrap();
    let msg = Message::from_bytes(b"will-fail").unwrap();
    let _ = router.send(&rid, msg);
    // msg is consumed regardless of success/failure – no native leak
}

#[test]
fn multipart_recv_shape_matches_callback_shape() {
    use std::sync::{Arc, Mutex};

    let ctx = Context::new().unwrap();

    // Direct recv path
    let a1 = ctx.pair_socket().unwrap();
    a1.bind("inproc://own-shape-direct").unwrap();
    let b1 = ctx.pair_socket().unwrap();
    b1.connect("inproc://own-shape-direct").unwrap();
    thread::sleep(Duration::from_millis(50));

    let parts = vec![
        Message::from_bytes(b"frame-x").unwrap(),
        Message::from_bytes(b"frame-y").unwrap(),
    ];
    b1.send(parts).unwrap();
    let direct = a1.recv().unwrap();
    let direct_count = direct.parts().len();
    let direct_data: Vec<Vec<u8>> = direct.parts().iter().map(|p| p.data().to_vec()).collect();

    // Callback recv path
    let mut a2 = ctx.pair_socket().unwrap();
    a2.bind("inproc://own-shape-callback").unwrap();

    let cb_parts: Arc<Mutex<Vec<Vec<u8>>>> = Arc::new(Mutex::new(Vec::new()));
    let cb_clone = cb_parts.clone();

    a2.on_receive(move |received| {
        let mut guard = cb_clone.lock().unwrap();
        for part in received.parts() {
            guard.push(part.data().to_vec());
        }
    })
    .unwrap();

    let b2 = ctx.pair_socket().unwrap();
    b2.connect("inproc://own-shape-callback").unwrap();
    thread::sleep(Duration::from_millis(50));

    let parts = vec![
        Message::from_bytes(b"frame-x").unwrap(),
        Message::from_bytes(b"frame-y").unwrap(),
    ];
    b2.send(parts).unwrap();
    thread::sleep(Duration::from_millis(100));

    let cb_data = cb_parts.lock().unwrap();
    // Both paths must see the same number of frames with the same content
    assert_eq!(direct_count, cb_data.len(), "frame count must match");
    assert_eq!(direct_data, *cb_data, "frame content must match");
}

#[test]
fn callback_receives_owned_parts() {
    use std::sync::{Arc, Mutex};

    let ctx = Context::new().unwrap();
    let mut server = ctx.pair_socket().unwrap();
    server.bind("inproc://own-callback").unwrap();

    let received_data: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(Vec::new()));
    let data_clone = received_data.clone();

    server
        .on_receive(move |received| {
            // Callback owns the parts
            let part = &received.parts()[0];
            let mut guard = data_clone.lock().unwrap();
            guard.extend_from_slice(part.data());
            // Parts dropped when `received` goes out of scope
        })
        .unwrap();

    let client = ctx.pair_socket().unwrap();
    client.connect("inproc://own-callback").unwrap();
    thread::sleep(Duration::from_millis(50));

    let msg = Message::from_bytes(b"cb-payload").unwrap();
    client.send(msg).unwrap();
    thread::sleep(Duration::from_millis(100));

    let guard = received_data.lock().unwrap();
    assert_eq!(&*guard, b"cb-payload");
}
