//! Request/reply async sample -- demonstrates direct dealer/router request surfaces.

#[path = "sample_support.rs"]
mod sample_support;

use std::future::Future;
use std::pin::pin;
use std::sync::mpsc;
use std::task::{Context as TaskContext, Poll, RawWaker, RawWakerVTable, Waker};
use std::thread;
use std::time::Duration;

use zlink::{Context, Message, RoutingId, SocketMonitor};

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
    let endpoint = sample_support::tcp_endpoint();

    let router_socket = ctx.router_socket().expect("router socket failed");
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
    let expected_routing_id = routing_id;
    let router_thread = router_socket;
    thread::spawn(move || {
        let mut received = zlink::Received::empty();
        router_thread
            .recv(&mut received, zlink::RecvFlags::NONE)
            .expect("router recv failed");
        assert_eq!(received.parts()[0].as_str().unwrap_or("?"), "ping");
        assert_eq!(
            received
                .routing_id()
                .expect("missing routing id")
                .as_bytes(),
            expected_routing_id.as_bytes()
        );
        received
            .reply()
            .message(Message::try_from(b"pong").expect("reply message failed"))
            .submit()
            .expect("reply send failed");
        request_done_tx.send(()).expect("request done send failed");
    });

    let reply = block_on(
        dealer_socket
            .request()
            .message(Message::try_from(b"ping").expect("request message failed"))
            .timeout(Duration::from_secs(2))
            .submit_async(),
    )
    .expect("dealer request failed");
    assert_eq!(reply[0].as_str().unwrap_or("?"), "pong");
    request_done_rx
        .recv_timeout(Duration::from_secs(2))
        .expect("request handler timed out");

    println!("[dealer-router/request-reply/async] send: \"ping\" -> recv: \"pong\"");
}
