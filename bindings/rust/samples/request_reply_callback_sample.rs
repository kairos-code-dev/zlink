//! Request/reply callback sample -- demonstrates callback completion.

#[path = "sample_support.rs"]
mod sample_support;

use std::sync::mpsc;
use std::time::Duration;

use zlink::{Context, Message, RoutingId, SendFlags, SocketMonitor};

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let endpoint = sample_support::tcp_endpoint();

    let mut router_socket = ctx.router_socket().expect("router socket failed");
    let dealer_socket = ctx.dealer_socket().expect("dealer socket failed");
    let router_monitor = SocketMonitor::open(&router_socket).expect("router monitor open failed");
    let dealer_monitor = SocketMonitor::open(&dealer_socket).expect("dealer monitor open failed");
    let routing_id = RoutingId::from_bytes(b"request-reply-client");
    dealer_socket
        .set_routing_id(&routing_id)
        .expect("set routing id failed");
    router_socket.bind(&endpoint).expect("bind failed");
    dealer_socket.connect(&endpoint).expect("connect failed");
    sample_support::wait_connected(&[&router_monitor, &dealer_monitor]);
    drop(router_monitor);
    drop(dealer_monitor);

    let (request_done_tx, request_done_rx) = mpsc::channel();
    let (reply_done_tx, reply_done_rx) = mpsc::channel();
    let expected_routing_id = routing_id.clone();
    let mut router_thread = router_socket;
    std::thread::spawn(move || {
        let received = router_thread.recv().expect("router recv failed");
        assert_eq!(received.parts()[0].as_str().unwrap_or("?"), "ping");
        assert_eq!(
            received.routing_id().expect("missing routing id").as_bytes(),
            expected_routing_id.as_bytes()
        );
        received
            .reply(vec![Message::copy_from(b"pong").expect("reply message failed")])
            .expect("reply send failed");
        request_done_tx.send(()).expect("request done send failed");
    });

    dealer_socket
        .request_callback(
            &[b"ping"],
            move |result| {
                let reply = result.expect("dealer callback failed");
                assert_eq!(reply[0].as_str().unwrap_or("?"), "pong");
                reply_done_tx.send(()).expect("reply done send failed");
            },
            SendFlags::NONE,
            Some(Duration::from_secs(2)),
        )
        .expect("dealer request callback failed");

    request_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("request handler timed out");
    reply_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("reply callback timed out");

    println!("[dealer-router/request-reply/callback] send: \"ping\" -> recv: \"pong\"");
}
