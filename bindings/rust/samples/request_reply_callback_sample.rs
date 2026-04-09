//! Request/reply callback sample -- demonstrates callback completion.

#[path = "sample_support.rs"]
mod sample_support;

use std::sync::mpsc;
use std::time::Duration;

use zlink::{Context, Message, RequestDealer, RequestRouter, RoutingId, SocketMonitor};

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let endpoint = sample_support::tcp_endpoint();

    let router_socket = ctx.router_socket().expect("router socket failed");
    let dealer_socket = ctx.dealer_socket().expect("dealer socket failed");
    let router_send_handle = router_socket.send_handle();
    let router_monitor = SocketMonitor::open(&router_socket).expect("router monitor open failed");
    let dealer_monitor = SocketMonitor::open(&dealer_socket).expect("dealer monitor open failed");
    let routing_id = RoutingId::new(b"request-reply-client").expect("routing id failed");
    dealer_socket
        .set_routing_id(&routing_id)
        .expect("set routing id failed");
    router_socket.bind(&endpoint).expect("bind failed");
    dealer_socket.connect(&endpoint).expect("connect failed");
    sample_support::wait_connected(&[&router_monitor, &dealer_monitor]);
    drop(router_monitor);
    drop(dealer_monitor);

    let router = RequestRouter::new(router_socket).expect("request router failed");
    let dealer = RequestDealer::new(dealer_socket).expect("request dealer failed");

    let (request_done_tx, request_done_rx) = mpsc::channel();
    let (reply_done_tx, reply_done_rx) = mpsc::channel();
    let expected_routing_id = routing_id.clone();
    router.on_receive(move |received| {
        assert_eq!(received.parts()[0].as_str().unwrap_or("?"), "ping");
        assert_eq!(received.routing_id().data(), expected_routing_id.data());
        let (msg_type, correlation_id) = received.parts()[0]
            .request_info()
            .expect("request info failed");
        assert_eq!(msg_type, 1);
        let mut reply = Message::from_bytes(b"pong").expect("reply message failed");
        reply
            .set_reply(correlation_id)
            .expect("set reply metadata failed");
        router_send_handle
            .send_to(received.routing_id(), vec![reply])
            .expect("reply send failed");
        request_done_tx.send(()).expect("request done send failed");
    });

    dealer.request_callback_with_timeout(
        Message::from_bytes(b"ping").expect("request message failed"),
        move |result| {
            let reply = result.expect("dealer callback failed");
            assert_eq!(reply.parts()[0].as_str().unwrap_or("?"), "pong");
            reply_done_tx.send(()).expect("reply done send failed");
        },
        Duration::from_secs(2),
    );

    request_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("request handler timed out");
    reply_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("reply callback timed out");

    println!("[dealer-router/request-reply/callback] send: \"ping\" -> recv: \"pong\"");
}
