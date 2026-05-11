//! Receive Failure Contract Tests – verify EAGAIN handling and
//! direct recv error propagation.

use zlink::*;

#[test]
fn eagain_returns_false_not_error() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://rf-eagain").unwrap();

    let mut received = Received::empty();
    let got = sock
        .recv(&mut received, RecvFlags::DONT_WAIT)
        .expect("EAGAIN must return Ok(false), not Err");
    assert!(!got, "EAGAIN must return Ok(false)");
}

#[test]
fn eagain_sub_returns_none() {
    let ctx = Context::new().unwrap();
    let sub = ctx.sub_socket().unwrap();
    sub.connect("inproc://rf-sub-eagain-target").unwrap();
    sub.set_subscription("").unwrap();

    let result = sub.subscribe_with_flags(RecvFlags::DONT_WAIT);
    assert!(
        result.unwrap().is_none(),
        "EAGAIN on sub must return Ok(None)"
    );
}

#[test]
fn eagain_xpub_subscription_event_returns_none() {
    let ctx = Context::new().unwrap();
    let xpub = ctx.xpub_socket().unwrap();
    xpub.bind("inproc://rf-xpub-eagain").unwrap();

    let result = xpub.receive_subscription_event_with_flags(RecvFlags::DONT_WAIT);
    assert!(
        result.unwrap().is_none(),
        "EAGAIN on xpub must return Ok(None)"
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

    let mut received = Received::empty();
    let result = sock.recv(&mut received, RecvFlags::DONT_WAIT);
    // ETERM is not EAGAIN → must be Err, not Ok(false)
    assert!(
        result.is_err(),
        "non-EAGAIN recv error must be Err, not hidden"
    );
}
