//! SPOT request async sample – demonstrates Spot.request_channel async completion.

#[path = "sample_support.rs"]
mod sample_support;

use std::future::Future;
use std::pin::pin;
use std::sync::mpsc;
use std::task::{Context as TaskContext, Poll, RawWaker, RawWakerVTable, Waker};
use std::thread;
use std::time::Duration;

use zlink::{Context, Message, SpotNode};

fn noop_waker() -> Waker {
    unsafe fn clone(_: *const ()) -> RawWaker {
        RawWaker::new(std::ptr::null(), &VTABLE)
    }
    unsafe fn wake(_: *const ()) {}
    unsafe fn wake_by_ref(_: *const ()) {}
    unsafe fn drop(_: *const ()) {}

    static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, wake, wake_by_ref, drop);
    let raw = RawWaker::new(std::ptr::null(), &VTABLE);
    unsafe { Waker::from_raw(raw) }
}

fn block_on<F: Future>(future: F) -> F::Output {
    let waker = noop_waker();
    let mut context = TaskContext::from_waker(&waker);
    let mut future = pin!(future);
    loop {
        match future.as_mut().poll(&mut context) {
            Poll::Ready(value) => return value,
            Poll::Pending => thread::yield_now(),
        }
    }
}

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
        let received = responder.recv().expect("router recv failed");
        assert_eq!(received.parts()[0].as_str().unwrap_or("?"), "spot-ping");
        responder
            .reply(
                received.routing_id().expect("missing routing id"),
                received.request_seq().expect("missing request seq"),
                vec![Message::copy_from(b"spot-pong").expect("reply message failed")],
            )
            .expect("reply send failed");
        server_done_tx.send(()).expect("server done send failed");
    });

    let reply = block_on(requester.request_channel(
        channel_name,
        vec![Message::copy_from(b"spot-ping").expect("request message failed")],
        Duration::from_secs(5),
    ))
    .expect("spot request failed");
    assert_eq!(reply[0].as_str().unwrap_or("?"), "spot-pong");
    server_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("server did not reply in time");

    println!("[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
}
