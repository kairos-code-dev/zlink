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
    let options = ctx.options();
    options.set_io_threads(2).unwrap();
    assert_eq!(options.io_threads().unwrap(), 2);
    options.set_max_sockets(512).unwrap();
    assert_eq!(options.max_sockets().unwrap(), 512);
    assert!(options.socket_limit().unwrap() > 0);
    assert!(options.msg_t_size().unwrap() > 0);
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
    assert_eq!(msg.len(), 0);
}

#[test]
fn message_from_bytes() {
    let data = b"contract-test-payload";
    let msg = Message::from_bytes(data).unwrap();
    assert_eq!(msg.data(), data);
    assert_eq!(msg.len(), data.len());
}

#[test]
fn message_with_size() {
    let msg = Message::with_size(128).unwrap();
    assert_eq!(msg.len(), 128);
}

#[test]
fn message_as_str() {
    let msg = Message::from_bytes(b"hello").unwrap();
    assert_eq!(msg.as_str().unwrap(), "hello");
}

#[test]
fn message_diagnostic_properties() {
    let msg = Message::from_bytes(b"diagnostic").unwrap();
    assert!(msg.ref_count() >= 1);
    assert_eq!(msg.get_property("missing").unwrap(), None);
}

#[test]
fn message_get_property_validates_input() {
    let msg = Message::from_bytes(b"diagnostic").unwrap();

    let empty = msg.get_property("");
    assert!(empty.is_err());
    assert_eq!(empty.unwrap_err().code(), libc::EINVAL);

    let nul = msg.get_property("bad\0name");
    assert!(nul.is_err());
    assert_eq!(nul.unwrap_err().code(), libc::EINVAL);
}

#[test]
fn message_drop_calls_close() {
    // Create and drop many messages to verify no native leak
    for _ in 0..1000 {
        let msg = Message::from_bytes(b"drop-test").unwrap();
        drop(msg);
    }
}

// ---------------------------------------------------------------------------
// RoutingId managed ↔ native conversion
// ---------------------------------------------------------------------------

#[test]
fn routing_id_roundtrip() {
    let rid = RoutingId::new(b"peer-42").unwrap();
    assert_eq!(rid.data(), b"peer-42");
    assert_eq!(rid.len(), 7);
}

#[test]
fn routing_id_max_length() {
    let data = vec![0xABu8; 255];
    let rid = RoutingId::new(&data).unwrap();
    assert_eq!(rid.len(), 255);
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
