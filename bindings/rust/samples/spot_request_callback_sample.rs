//! SPOT request callback sample - demonstrates Spot.request_to_channel callback completion.

#[path = "sample_support.rs"]
mod sample_support;

use std::sync::mpsc;
use std::thread;
use std::time::Duration;

use zlink::{Context, Message, SpotNode};

fn main() {
    let ctx = Context::new().expect("context creation failed");

    let requester_node = SpotNode::new(&ctx).expect("requester node failed");
    let requester = requester_node.create_spot().expect("requester spot failed");
    let requester_dealer = ctx.dealer_socket().expect("requester dealer failed");
    let responder = ctx.router_socket().expect("responder router failed");
    let channel_name = "orders";

    let endpoint = sample_support::tcp_endpoint();
    responder.bind(&endpoint).expect("bind failed");
    requester_dealer.connect(&endpoint).expect("connect failed");
    requester_node
        .attach_channel_dealer_manual(channel_name, &requester_dealer)
        .expect("attach_channel_dealer_manual failed");

    let (server_done_tx, server_done_rx) = mpsc::channel();
    thread::spawn(move || {
        let mut received = zlink::Received::empty();
        responder
            .recv(&mut received, zlink::RecvFlags::NONE)
            .expect("router recv failed");
        assert_eq!(received.parts()[0].as_str().unwrap_or("?"), "spot-ping");
        responder
            .reply(
                received.routing_id().expect("missing routing id"),
                received.request_seq().expect("missing request seq"),
            )
            .message(Message::try_from(b"spot-pong").expect("reply message failed"))
            .submit()
            .expect("reply send failed");
        server_done_tx.send(()).expect("server done send failed");
    });

    let (reply_tx, reply_rx) = mpsc::channel();
    requester
        .request_to_channel(channel_name)
        .message(Message::try_from(b"spot-ping").expect("request message failed"))
        .timeout(Duration::from_secs(5))
        .submit(move |result| {
            let _ = reply_tx.send(result);
        })
        .expect("spot request submit failed");
    let reply = reply_rx
        .recv_timeout(Duration::from_secs(5))
        .expect("reply callback timed out")
        .expect("spot request failed");
    assert_eq!(reply[0].as_str().unwrap_or("?"), "spot-pong");
    server_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("server did not reply in time");

    println!("[spot/request/callback] request: \"spot-ping\" -> reply: \"spot-pong\"");
}
