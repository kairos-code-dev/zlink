//! Contract tests – verify FFI/native call mapping, type conversions,
//! and resource lifecycle.

use zlink::*;

// ---------------------------------------------------------------------------
// Context lifecycle
// ---------------------------------------------------------------------------

#[test]
fn context_create_and_drop() {
    let ctx = Context::new().unwrap();
    // Context should create successfully and drop without leak
    drop(ctx);
}

#[test]
fn context_typed_options() {
    let ctx = Context::new().unwrap();
    let _options = ctx.options();
}

// ---------------------------------------------------------------------------
// Socket lifecycle
// ---------------------------------------------------------------------------

#[test]
fn socket_create_and_drop() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    drop(sock);
    // Socket close should complete without native leak
}

#[test]
fn socket_bind_connect_lifecycle() {
    let ctx = Context::new().unwrap();
    let server = ctx.pair_socket().unwrap();
    server.bind("inproc://contract-lifecycle").unwrap();

    let client = ctx.pair_socket().unwrap();
    client.connect("inproc://contract-lifecycle").unwrap();

    drop(client);
    drop(server);
    drop(ctx);
}

// ---------------------------------------------------------------------------
// Message lifecycle
// ---------------------------------------------------------------------------

#[test]
fn message_create_empty() {
    let msg = Message::new().unwrap();
    assert!(msg.is_empty());
    assert_eq!(msg.size(), 0);
}

#[test]
fn message_from_bytes() {
    let data = b"contract-test-payload";
    let msg = Message::copy_from(data).unwrap();
    assert_eq!(msg.as_bytes(), data);
    assert_eq!(msg.size(), data.len());
}

#[test]
fn message_with_size() {
    let msg = Message::with_size(128).unwrap();
    assert_eq!(msg.size(), 128);
}

#[test]
fn message_as_str() {
    let msg = Message::copy_from(b"hello").unwrap();
    assert_eq!(msg.as_str().unwrap(), "hello");
}

#[test]
fn message_diagnostic_properties() {
    let msg = Message::copy_from(b"diagnostic").unwrap();
    assert!(msg.ref_count() >= 1);
    assert_eq!(msg.get_property("missing").unwrap(), None);
}

#[test]
fn message_get_property_validates_input() {
    let msg = Message::copy_from(b"diagnostic").unwrap();

    let empty = msg.get_property("");
    assert!(empty.is_err());
    let empty_err = empty.unwrap_err();
    assert_eq!(empty_err.code(), ConfigResult::InvalidArgument);
    assert_eq!(empty_err.internal_errno(), libc::EINVAL);

    let nul = msg.get_property("bad\0name");
    assert!(nul.is_err());
    let nul_err = nul.unwrap_err();
    assert_eq!(nul_err.code(), ConfigResult::InvalidArgument);
    assert_eq!(nul_err.internal_errno(), libc::EINVAL);
}

#[test]
fn message_drop_calls_close() {
    // Create and drop many messages to verify no native leak
    for _ in 0..1000 {
        let msg = Message::copy_from(b"drop-test").unwrap();
        drop(msg);
    }
}

// ---------------------------------------------------------------------------
// RoutingId managed ↔ native conversion
// ---------------------------------------------------------------------------

#[test]
fn routing_id_roundtrip() {
    let rid = RoutingId::from_bytes(b"peer-42");
    assert_eq!(rid.as_bytes(), b"peer-42");
    assert_eq!(rid.size(), 7);
}

#[test]
fn routing_id_max_length() {
    let data = vec![0xABu8; 255];
    let rid = RoutingId::from_bytes(&data);
    assert_eq!(rid.size(), 255);
}

// ---------------------------------------------------------------------------
// Version / capability native mapping
// ---------------------------------------------------------------------------

#[test]
fn version_returns_valid_triple() {
    let (major, minor, patch) = version();
    assert!(major >= 5, "expected major >= 5, got {major}");
    assert!(minor >= 0);
    assert!(patch >= 0);
}

#[test]
fn has_capability_check() {
    // "tcp" should always be supported
    // Just verify the function doesn't crash
    let _ = has("tcp");
}

// ---------------------------------------------------------------------------
// Error path: native handle creation failure
// ---------------------------------------------------------------------------

#[test]
fn all_socket_types_create_successfully() {
    let ctx = Context::new().unwrap();
    let _ = ctx.pair_socket().unwrap();
    let _ = ctx.pub_socket().unwrap();
    let _ = ctx.sub_socket().unwrap();
    let _ = ctx.dealer_socket().unwrap();
    let _ = ctx.router_socket().unwrap();
    let _ = ctx.xpub_socket().unwrap();
    let _ = ctx.xsub_socket().unwrap();
    let _ = ctx.stream_socket().unwrap();
}

#[test]
fn request_reply_surface_exists() {
    let _dealer_request = DealerSocket::request;
    let _router_request = RouterSocket::request;
    let _router_reply = |socket: &RouterSocket, rid: &RoutingId, seq: u64, msg: Message| {
        socket.reply(rid, seq).message(msg).submit()
    };
}

#[test]
fn request_router_exposes_request_sequence() {
    let ctx = Context::new().unwrap();
    let router_socket = ctx.router_socket().unwrap();
    let dealer_socket = ctx.dealer_socket().unwrap();
    router_socket
        .bind("inproc://rust-request-reply-data")
        .unwrap();
    dealer_socket
        .connect("inproc://rust-request-reply-data")
        .unwrap();

    dealer_socket
        .send()
        .message(Message::copy_from(b"plain-data").unwrap())
        .submit()
        .unwrap();

    let mut received = Received::empty();
    router_socket.recv(&mut received, RecvFlags::NONE).unwrap();
    let request_seq = received.request_seq;
    assert_eq!(received.single_part().unwrap().as_bytes(), b"plain-data");
    assert_eq!(request_seq, None);
}

#[test]
fn router_reply_with_non_empty_flags_fails_explicitly() {
    let ctx = Context::new().unwrap();
    let router = ctx.router_socket().unwrap();
    let rid = RoutingId::from_bytes(b"peer-42");
    let err = router
        .reply(&rid, 1)
        .message(Message::copy_from(b"pong").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .unwrap_err();
    assert_eq!(err.code(), SubmitResult::NotSupported);
}

#[test]
fn spot_reply_with_non_empty_flags_fails_explicitly() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let spot = node.create_spot().unwrap();
    let rid = RoutingId::from_bytes(b"peer-42");

    let router_err = spot
        .reply_to_router(rid.clone(), 1)
        .message(Message::copy_from(b"pong").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .unwrap_err();
    assert_eq!(router_err.code(), SubmitResult::NotSupported);

    let spot_err = spot
        .reply_to_spot(rid.clone(), rid, 1)
        .message(Message::copy_from(b"pong").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .unwrap_err();
    assert_eq!(spot_err.code(), SubmitResult::NotSupported);
}
