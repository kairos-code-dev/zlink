//! DEALER/ROUTER callback sample -- demonstrates routed send inside a
//! receive callback (request/reply pattern with callback-mode router).

use std::time::Duration;

use zlink::{Context, Message, RoutingId, SocketMonitor};

pub fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    port
}

pub fn wait_connected(monitors: &[&zlink::SocketMonitor]) {
    for monitor in monitors {
        loop {
            let event = monitor.recv().expect("monitor recv failed");
            if event.is_connection_ready()
                || monitor
                    .snapshot()
                    .expect("monitor snapshot failed")
                    .is_ready()
            {
                break;
            }
        }
    }
}

pub fn wait_stream_connected(monitor: &zlink::SocketMonitor) {
    loop {
        let event = monitor.recv().expect("monitor recv failed");
        if event.is_accepted()
            || event.is_connection_ready()
            || monitor
                .snapshot()
                .expect("monitor snapshot failed")
                .is_ready()
        {
            break;
        }
    }
}

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let port = reserve_tcp_port();
    let endpoint = format!("tcp://127.0.0.1:{port}");

    let mut router = ctx.router_socket().expect("router socket failed");
    let dealer = ctx.dealer_socket().expect("dealer socket failed");
    let rid = RoutingId::from_bytes(b"dealer-cb-3");
    dealer.set_routing_id(&rid).expect("set routing id failed");

    let router_mon = SocketMonitor::open(&router).expect("router monitor open failed");
    let dealer_mon = SocketMonitor::open(&dealer).expect("dealer monitor open failed");

    router.bind(&endpoint).expect("bind failed");
    dealer.connect(&endpoint).expect("connect failed");

    wait_connected(&[&router_mon, &dealer_mon]);
    drop(router_mon);
    drop(dealer_mon);

    let handle = router.send_handle();

    router
        .on_receive(move |received| {
            let payload = received.parts()[0].as_str().unwrap_or("?");
            assert_eq!(payload, "ping");
            let reply = Message::copy_from(b"pong").unwrap();
            handle
                .send_to(received.routing_id().expect("missing routing id"), reply)
                .unwrap();
        })
        .expect("on_receive failed");

    let msg = Message::copy_from(b"ping").unwrap();
    dealer.send(msg).expect("send failed");

    dealer
        .common_options()
        .set_recv_timeout(Duration::from_secs(5))
        .expect("set recv timeout failed");
    let response = dealer.recv().expect("dealer recv failed");
    let reply_payload = response.parts()[0].as_str().unwrap_or("?");
    assert_eq!(reply_payload, "pong");

    println!(
        "[dealer-router/callback] send: \"ping\" → recv: \"{}\"",
        reply_payload
    );
}
