//! Receive Failure Contract Tests – verify EAGAIN handling and
//! callback/direct recv conflict behavior.

use zlink::*;

#[test]
fn eagain_returns_none_not_error() {
    // Non-blocking recv with no data should surface a recv error.
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://rf-eagain").unwrap();

    let result = sock.recv_with_flags(RecvFlags::DONT_WAIT);
    assert!(result.is_err(), "EAGAIN must surface as an error");
}

#[test]
fn eagain_sub_returns_none() {
    let ctx = Context::new().unwrap();
    let sub = ctx.sub_socket().unwrap();
    sub.connect("inproc://rf-sub-eagain-target").unwrap();
    sub.set_subscription("").unwrap();

    let result = sub.subscribe_with_flags(RecvFlags::DONT_WAIT);
    assert!(result.is_err(), "EAGAIN on sub must surface as an error");
}

#[test]
fn eagain_xpub_subscription_event_returns_none() {
    let ctx = Context::new().unwrap();
    let xpub = ctx.xpub_socket().unwrap();
    xpub.bind("inproc://rf-xpub-eagain").unwrap();

    let result = xpub.receive_subscription_event_with_flags(RecvFlags::DONT_WAIT);
    assert!(result.is_err());
}

#[test]
fn callback_mode_blocks_direct_recv() {
    // After installing a callback, direct recv should fail with EBUSY
    let ctx = Context::new().unwrap();
    let mut sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://rf-cb-conflict").unwrap();

    sock.on_receive(|_received| {}).unwrap();

    // Direct recv should now fail
    let result = sock.recv_with_flags(RecvFlags::DONT_WAIT);
    assert!(
        result.is_err(),
        "direct recv after callback install must fail"
    );
}

#[test]
fn direct_recv_error_not_hidden_as_empty() {
    // Verify that a non-EAGAIN recv error surfaces as Err, not Ok(None).
    // Shutdown the context → recv returns ETERM, which must be Err.
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://rf-recv-error-nothidden").unwrap();

    ctx.shutdown().unwrap();

    let result = sock.recv_with_flags(RecvFlags::DONT_WAIT);
    // ETERM is not EAGAIN → must be Err, not Ok(None)
    assert!(
        result.is_err(),
        "non-EAGAIN recv error must be Err, not hidden"
    );
}
