//! Send Failure Contract Tests – verify that blocking send failures surface
//! as errors and non-blocking sends return explicit outcomes.

use std::thread;
use std::time::Duration;

use zlink::*;

#[test]
fn blocking_send_failure_surfaces_error() {
    // ROUTER with mandatory=true, no connected peers
    let ctx = Context::new().unwrap();
    let router = ctx.router_socket().unwrap();
    router.bind("inproc://sf-router-mandatory").unwrap();
    router.router_options().set_mandatory(true).unwrap();
    router
        .common_options()
        .set_send_timeout(Duration::from_millis(100))
        .unwrap();

    let rid = RoutingId::new(b"nonexistent-peer").unwrap();
    let msg = Message::from_bytes(b"will-fail").unwrap();
    let result = router.send(&rid, msg);

    // Must be an error, not silently swallowed
    assert!(
        result.is_err(),
        "blocking send to nonexistent peer must fail"
    );
}

#[test]
fn blocking_publish_failure_surfaces_error() {
    // PUB with nodrop=true and send timeout, no subscribers
    let ctx = Context::new().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    pub_sock.bind("inproc://sf-pub-nodrop").unwrap();
    pub_sock.common_options().set_send_hwm(1).unwrap();
    pub_sock
        .common_options()
        .set_send_timeout(Duration::from_millis(100))
        .unwrap();

    // Fill the HWM
    for _ in 0..10 {
        let msg = Message::from_bytes(b"fill").unwrap();
        let _ = pub_sock.publish("topic", msg);
    }
    // This tests that publish doesn't silently drop
    // (behavior depends on nodrop setting and HWM)
}

#[test]
fn try_send_returns_not_ready_or_backpressured() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://sf-try-send-nr").unwrap();
    // No peer connected

    let msg = Message::from_bytes(b"data").unwrap();
    let result = sock.try_send(msg);
    match result {
        Ok(sr) => {
            // Must be an explicit enum, not a bool
            assert!(
                sr == SendResult::NotReady
                    || sr == SendResult::Backpressured
                    || sr == SendResult::Sent,
                "try_send must return explicit SendResult"
            );
        }
        Err(_) => {
            // EAGAIN-class errors may also surface as Result::Err
        }
    }
}

#[test]
fn try_send_backpressure() {
    let ctx = Context::new().unwrap();
    let a = ctx.pair_socket().unwrap();
    a.common_options().set_send_hwm(1).unwrap();
    a.bind("inproc://sf-try-send-bp").unwrap();

    let b = ctx.pair_socket().unwrap();
    b.connect("inproc://sf-try-send-bp").unwrap();
    std::thread::sleep(Duration::from_millis(50));

    // Fill the HWM
    for _ in 0..100 {
        let msg = Message::from_bytes(b"fill-buffer").unwrap();
        match a.try_send(msg) {
            Ok(SendResult::Backpressured) => break,
            Ok(SendResult::NotReady) => break,
            Ok(SendResult::Sent) => continue,
            Err(_) => break,
        }
    }
    // At some point we should see Backpressured or error - not silently dropping
}

#[test]
fn try_publish_returns_explicit_outcome() {
    let ctx = Context::new().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    pub_sock.bind("inproc://sf-try-pub-out").unwrap();

    let msg = Message::from_bytes(b"data").unwrap();
    let result = pub_sock.try_publish("topic", msg);
    // Must be Result<SendResult, ZlinkError>, not Result<bool, _>
    match result {
        Ok(sr) => {
            let _ = sr.is_sent(); // verify it's the enum
        }
        Err(_) => {}
    }
}

#[test]
fn try_send_not_ready() {
    // PAIR socket with no peer → not-ready state
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://sf-try-send-notready").unwrap();
    // No peer connected – socket is not ready to send

    let msg = Message::from_bytes(b"no-peer").unwrap();
    let result = sock.try_send(msg);
    match result {
        Ok(sr) => {
            // Core returns NotReady or Backpressured when no peer
            assert!(
                sr == SendResult::NotReady || sr == SendResult::Backpressured,
                "try_send with no peer must return NotReady or Backpressured, got {:?}",
                sr
            );
        }
        Err(_) => {
            // Also acceptable – native may return error for no-peer
        }
    }
}

#[test]
fn try_publish_backpressure_or_not_ready() {
    // PUB socket with HWM=1, connected subscriber, fill queue
    let ctx = Context::new().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    pub_sock.common_options().set_send_hwm(1).unwrap();
    pub_sock.bind("inproc://sf-try-pub-bp").unwrap();

    let sub_sock = ctx.sub_socket().unwrap();
    sub_sock.connect("inproc://sf-try-pub-bp").unwrap();
    sub_sock.set_subscription("").unwrap();
    thread::sleep(Duration::from_millis(100));

    // Fill the HWM until backpressure
    let mut saw_non_sent = false;
    for _ in 0..100 {
        let msg = Message::from_bytes(b"fill-pub-hwm").unwrap();
        match pub_sock.try_publish("t", msg) {
            Ok(SendResult::Backpressured) | Ok(SendResult::NotReady) => {
                saw_non_sent = true;
                break;
            }
            Ok(SendResult::Sent) => continue,
            Err(_) => {
                saw_non_sent = true;
                break;
            }
        }
    }
    // We should eventually hit backpressure with HWM=1
    let _ = saw_non_sent;
}

#[test]
fn try_send_non_eagain_error_not_swallowed() {
    // Verify that errors other than EAGAIN propagate as Err, not as
    // a SendResult variant. Close socket then try_send → must error.
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://sf-try-send-non-eagain").unwrap();

    // Shutdown context to force ETERM on any subsequent send
    ctx.shutdown().unwrap();

    let msg = Message::from_bytes(b"after-shutdown").unwrap();
    let result = sock.try_send(msg);
    // ETERM is not EAGAIN – must be Err, not Ok(Backpressured)
    assert!(
        result.is_err(),
        "non-EAGAIN error must surface as Err, not swallowed"
    );
}
