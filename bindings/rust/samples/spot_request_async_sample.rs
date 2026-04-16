//! SPOT request async sample – demonstrates the dedicated spot request sample.

#[path = "sample_support.rs"]
mod sample_support;

use std::future::Future;
use std::pin::Pin;
use std::sync::mpsc;
use std::task::{Context as TaskContext, Poll, RawWaker, RawWakerVTable, Waker};
use std::thread;
use std::time::Duration;

use zlink::{Context, Message, RecvFlags, RequestResult, RoutingId, SpotNode};

fn noop_raw_waker() -> RawWaker {
    fn clone(_: *const ()) -> RawWaker { noop_raw_waker() }
    fn wake(_: *const ()) {}
    fn wake_by_ref(_: *const ()) {}
    fn drop(_: *const ()) {}

    static VTABLE: RawWakerVTable =
        RawWakerVTable::new(clone, wake, wake_by_ref, drop);

    RawWaker::new(std::ptr::null(), &VTABLE)
}

fn block_on<F: Future>(future: F) -> F::Output {
    let waker = unsafe { Waker::from_raw(noop_raw_waker()) };
    let mut future = Box::pin(future);
    let mut cx = TaskContext::from_waker(&waker);
    loop {
        match Pin::as_mut(&mut future).poll(&mut cx) {
            Poll::Ready(value) => return value,
            Poll::Pending => thread::yield_now(),
        }
    }
}

fn main() {
    let ctx = Context::new().expect("context creation failed");

    let requester_node = SpotNode::new(&ctx).expect("requester node failed");
    let responder_node = SpotNode::new(&ctx).expect("responder node failed");
    let requester = requester_node.create_spot().expect("requester spot failed");
    let responder = responder_node.create_spot().expect("responder spot failed");
    requester_node
        .set_routing_id(&RoutingId::from_bytes(b"spot-requester-node"))
        .expect("requester node routing id failed");
    responder_node
        .set_routing_id(&RoutingId::from_bytes(b"spot-responder-node"))
        .expect("responder node routing id failed");
    requester
        .set_routing_id(&RoutingId::from_bytes(b"spot-requester"))
        .expect("requester spot routing id failed");
    responder
        .set_routing_id(&RoutingId::from_bytes(b"spot-responder"))
        .expect("responder spot routing id failed");

    let endpoint = sample_support::tcp_endpoint();
    responder_node.bind(&endpoint).expect("bind failed");
    requester_node
        .connect_peer(&endpoint)
        .expect("connect_peer failed");
    sample_support::wait_spot_peer_connected(&requester_node, Duration::from_secs(5));
    let responder_node_rid = responder_node.routing_id().expect("responder node rid failed");
    let responder_spot_rid = responder.routing_id().expect("responder spot rid failed");

    let (server_done_tx, server_done_rx) = mpsc::channel();
    thread::spawn(move || {
        let received = loop {
            match responder.recv_routed_with_flags(RecvFlags::DONT_WAIT) {
                Ok(received) => break received,
                Err(_) => thread::sleep(Duration::from_millis(10)),
            }
        };
        assert_eq!(received.parts()[0].as_str().unwrap_or("?"), "spot-ping");
        received
            .reply(vec![Message::copy_from(b"spot-pong").expect("reply message failed")])
            .expect("reply send failed");
        server_done_tx.send(()).expect("server done send failed");
    });

    let request_deadline = std::time::Instant::now() + Duration::from_secs(5);
    let reply = loop {
        match block_on(requester.request_to_spot(
            responder_node_rid.clone(),
            responder_spot_rid.clone(),
            vec![Message::copy_from(b"spot-ping").expect("request message failed")],
            Duration::from_secs(5),
        )) {
            Ok(reply) => break reply,
            Err(err)
                if err.code() == RequestResult::NotFound as i32
                    && std::time::Instant::now() < request_deadline =>
            {
                thread::sleep(Duration::from_millis(10));
            }
            Err(err) => panic!("spot request failed: {err:?}"),
        }
    };
    assert_eq!(reply[0].as_str().unwrap_or("?"), "spot-pong");
    server_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("server did not reply in time");

    println!("[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
}
