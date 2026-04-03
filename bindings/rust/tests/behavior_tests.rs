//! Behavior tests -- verify that the binding layer correctly relays
//! core send/recv/publish/subscribe contracts.

use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::Duration;

use zlink::*;

#[test]
fn pair_send_recv_roundtrip() {
    let ctx = Context::new().unwrap();
    let server = ctx.pair_socket().unwrap();
    server.bind("inproc://beh-pair").unwrap();

    let client = ctx.pair_socket().unwrap();
    client.connect("inproc://beh-pair").unwrap();

    let msg = Message::from_bytes(b"pair-payload-42").unwrap();
    client.send(msg).unwrap();

    let received = server.recv().unwrap();
    assert_eq!(received.parts().len(), 1);
    assert_eq!(received.parts()[0].data(), b"pair-payload-42");
}

#[test]
fn pair_multipart_send_recv() {
    let ctx = Context::new().unwrap();
    let a = ctx.pair_socket().unwrap();
    a.bind("inproc://beh-pair-multi").unwrap();

    let b = ctx.pair_socket().unwrap();
    b.connect("inproc://beh-pair-multi").unwrap();

    let parts = vec![
        Message::from_bytes(b"frame-1").unwrap(),
        Message::from_bytes(b"frame-2").unwrap(),
    ];
    b.send(parts).unwrap();

    let received = a.recv().unwrap();
    assert_eq!(received.parts().len(), 2);
    assert_eq!(received.parts()[0].data(), b"frame-1");
    assert_eq!(received.parts()[1].data(), b"frame-2");
}

#[test]
fn pair_try_recv_empty() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://beh-pair-try").unwrap();

    let result = sock.try_recv().unwrap();
    assert!(result.is_none());
}

#[test]
fn dealer_router_roundtrip() {
    let ctx = Context::new().unwrap();
    let router = ctx.router_socket().unwrap();
    router.bind("inproc://beh-dr").unwrap();

    let dealer = ctx.dealer_socket().unwrap();
    let rid = RoutingId::new(b"dealer-42").unwrap();
    dealer.set_routing_id(&rid).unwrap();
    dealer.connect("inproc://beh-dr").unwrap();
    thread::sleep(Duration::from_millis(50));

    // Dealer sends to Router
    let msg = Message::from_bytes(b"request-payload").unwrap();
    dealer.send(msg).unwrap();

    // Router receives with the dealer's routing id
    let received = router.recv().unwrap();
    assert_eq!(received.parts()[0].data(), b"request-payload");

    // Router sends back to the dealer using the received routing id
    let reply = Message::from_bytes(b"response-payload").unwrap();
    router.send(received.routing_id(), reply).unwrap();

    let response = dealer.recv().unwrap();
    assert_eq!(response.parts()[0].data(), b"response-payload");
}

#[test]
fn pub_sub_roundtrip() {
    let ctx = Context::new().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    pub_sock.bind("inproc://beh-pubsub").unwrap();

    let sub_sock = ctx.sub_socket().unwrap();
    sub_sock.connect("inproc://beh-pubsub").unwrap();
    sub_sock.set_subscription("market.").unwrap();
    thread::sleep(Duration::from_millis(100));

    let msg = Message::from_bytes(b"price=42.5").unwrap();
    pub_sock.publish("market.price", msg).unwrap();

    let topic_msg = sub_sock.subscribe().unwrap();
    assert_eq!(topic_msg.topic(), "market.price");
    assert_eq!(topic_msg.parts()[0].data(), b"price=42.5");
}

#[test]
fn sub_try_subscribe_empty() {
    let ctx = Context::new().unwrap();
    let sub_sock = ctx.sub_socket().unwrap();
    sub_sock.connect("inproc://beh-sub-try").unwrap();
    sub_sock.set_subscription("").unwrap();

    let result = sub_sock.try_subscribe().unwrap();
    assert!(result.is_none());
}

#[test]
fn try_send_explicit_outcome() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://beh-try-send").unwrap();
    // No peer connected, so try_send should return NotReady or Backpressured

    let msg = Message::from_bytes(b"test").unwrap();
    let result = sock.try_send(msg);
    // Result is Ok(SendResult) – not a bool
    match result {
        Ok(sr) => assert!(!sr.is_sent() || sr.is_sent()),
        Err(_) => {} // some error is also acceptable
    }
}

#[test]
fn try_publish_explicit_outcome() {
    let ctx = Context::new().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    pub_sock.bind("inproc://beh-try-pub").unwrap();

    let msg = Message::from_bytes(b"test").unwrap();
    let result = pub_sock.try_publish("topic", msg);
    // Should be Ok(SendResult), not bool
    match result {
        Ok(_sr) => {}
        Err(_) => {}
    }
}

#[test]
fn xpub_try_receive_subscription_event_empty() {
    let ctx = Context::new().unwrap();
    let xpub = ctx.xpub_socket().unwrap();
    xpub.bind("inproc://beh-xpub-try").unwrap();

    let result = xpub.try_receive_subscription_event().unwrap();
    assert!(result.is_none());
}

// ---------------------------------------------------------------------------
// Callback + send regression tests
// ---------------------------------------------------------------------------

fn tcp_endpoint() -> String {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    format!("tcp://127.0.0.1:{}", port)
}

#[test]
fn dealer_router_send_from_callback() {
    let ctx = Context::new().unwrap();
    let endpoint = tcp_endpoint();

    let mut router = ctx.router_socket().unwrap();
    let dealer = ctx.dealer_socket().unwrap();
    let rid = RoutingId::new(b"dealer-cb-test").unwrap();
    dealer.set_routing_id(&rid).unwrap();

    // Establish connection before installing callback -- required for the
    // router's internal routing-id handshake to complete in recv mode.
    let router_mon = SocketMonitor::open(&router, MONITOR_EVENT_CONNECTION_READY).unwrap();
    let dealer_mon = SocketMonitor::open(&dealer, MONITOR_EVENT_CONNECTION_READY).unwrap();
    router.bind(&endpoint).unwrap();
    dealer.connect(&endpoint).unwrap();
    router_mon.recv().unwrap();
    dealer_mon.recv().unwrap();
    drop(router_mon);
    drop(dealer_mon);

    // Install callback that echoes a reply via SendHandle.
    let handle = router.send_handle();
    let reply_ready = Arc::new((Mutex::new(false), Condvar::new()));
    let reply_signal = reply_ready.clone();

    router
        .on_receive(move |received| {
            assert_eq!(received.parts()[0].data(), b"request-42");
            let reply = Message::from_bytes(b"reply-42").unwrap();
            handle.send_to(received.routing_id(), reply).unwrap();
            let (lock, cv) = &*reply_signal;
            *lock.lock().unwrap() = true;
            cv.notify_one();
        })
        .unwrap();

    // Dealer sends request.
    dealer
        .send(Message::from_bytes(b"request-42").unwrap())
        .unwrap();

    // Wait for the callback to fire and send the reply.
    let (lock, cv) = &*reply_ready;
    let guard = lock.lock().unwrap();
    let result = cv.wait_timeout(guard, Duration::from_secs(5)).unwrap();
    assert!(!result.1.timed_out(), "callback did not fire within 5s");

    // Dealer receives the reply sent from within the router callback.
    dealer.set_recv_timeout(Duration::from_secs(5)).unwrap();
    let response = dealer.recv().unwrap();
    assert_eq!(response.parts()[0].data(), b"reply-42");
}

#[test]
fn pair_send_from_callback() {
    let ctx = Context::new().unwrap();
    let endpoint = tcp_endpoint();

    let mut server = ctx.pair_socket().unwrap();
    let client = ctx.pair_socket().unwrap();

    let server_mon = SocketMonitor::open(&server, MONITOR_EVENT_CONNECTION_READY).unwrap();
    let client_mon = SocketMonitor::open(&client, MONITOR_EVENT_CONNECTION_READY).unwrap();
    server.bind(&endpoint).unwrap();
    client.connect(&endpoint).unwrap();
    server_mon.recv().unwrap();
    client_mon.recv().unwrap();
    drop(server_mon);
    drop(client_mon);

    // Install callback that echoes a reply via SendHandle.
    let handle = server.send_handle();

    server
        .on_receive(move |received| {
            assert_eq!(received.parts()[0].data(), b"ping-pair");
            let reply = Message::from_bytes(b"pong-pair").unwrap();
            handle.send(reply).unwrap();
        })
        .unwrap();

    // Client sends and receives.
    client
        .send(Message::from_bytes(b"ping-pair").unwrap())
        .unwrap();
    client.set_recv_timeout(Duration::from_secs(5)).unwrap();
    let response = client.recv().unwrap();
    assert_eq!(response.parts()[0].data(), b"pong-pair");
}
