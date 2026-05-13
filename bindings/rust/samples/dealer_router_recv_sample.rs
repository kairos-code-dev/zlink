//! DEALER/ROUTER direct recv sample – demonstrates routed messaging.

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

    let router = ctx.router_socket().expect("router socket failed");
    let dealer = ctx.dealer_socket().expect("dealer socket failed");
    let rid = RoutingId::from_bytes(b"dealer-node-7");
    dealer.set_routing_id(&rid).expect("set routing id failed");

    let router_mon = SocketMonitor::open(&router).expect("router monitor open failed");
    let dealer_mon = SocketMonitor::open(&dealer).expect("dealer monitor open failed");

    router.bind(&endpoint).expect("bind failed");
    dealer.connect(&endpoint).expect("connect failed");

    wait_connected(&[&router_mon, &dealer_mon]);
    drop(router_mon);
    drop(dealer_mon);

    let req = Message::copy_from(b"ping").expect("message failed");
    dealer.send().message(req).submit().expect("send failed");

    let mut received = zlink::Received::empty();
    router
        .recv(&mut received, zlink::RecvFlags::NONE)
        .expect("router recv failed");
    assert!(received.routing_id().is_some());
    assert_eq!(received.parts()[0].as_str().unwrap(), "ping");

    let resp = Message::copy_from(b"pong").expect("message failed");
    received
        .send()
        .message(resp)
        .submit()
        .expect("received send failed");

    let mut response = zlink::Received::empty();
    dealer
        .recv(&mut response, zlink::RecvFlags::NONE)
        .expect("dealer recv failed");
    assert_eq!(response.parts()[0].as_str().unwrap(), "pong");
    println!(
        "[dealer-router/recv] send: \"ping\" → recv: \"{}\"",
        response.parts()[0].as_str().unwrap()
    );
}
