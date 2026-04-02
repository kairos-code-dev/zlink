//! PUB/SUB direct recv sample – demonstrates topic publish/subscribe.

use zlink::{Context, MONITOR_EVENT_CONNECTION_READY_CHANGED, Message, SocketMonitor};

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
            if event.is_connection_ready_changed()
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
            || event.is_connection_ready_changed()
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

    let pub_sock = ctx.pub_socket().expect("pub socket failed");
    let sub_sock = ctx.sub_socket().expect("sub socket failed");

    sub_sock
        .set_subscription("prices")
        .expect("set_subscription failed");

    let pub_mon = SocketMonitor::open(&pub_sock, MONITOR_EVENT_CONNECTION_READY_CHANGED)
        .expect("pub monitor open failed");
    let sub_mon = SocketMonitor::open(&sub_sock, MONITOR_EVENT_CONNECTION_READY_CHANGED)
        .expect("sub monitor open failed");

    pub_sock.bind(&endpoint).expect("bind failed");
    sub_sock.connect(&endpoint).expect("connect failed");

    wait_connected(&[&pub_mon, &sub_mon]);
    drop(pub_mon);
    drop(sub_mon);

    let msg = Message::from_bytes(b"101.25").expect("message failed");
    pub_sock.publish("prices", msg).expect("publish failed");

    let topic_msg = sub_sock.subscribe().expect("subscribe recv failed");
    assert_eq!(topic_msg.topic(), "prices");
    assert_eq!(topic_msg.parts()[0].as_str().unwrap(), "101.25");
    println!(
        "[pubsub/recv] publish: \"{}/101.25\" → subscribe: \"{}/{}\"",
        topic_msg.topic(),
        topic_msg.topic(),
        topic_msg.parts()[0].as_str().unwrap()
    );
}
