//! PUB/SUB callback sample – demonstrates monitor callback plus direct recv.

use std::sync::mpsc;
use std::time::Duration;

use zlink::{Context, Message, SocketMonitor};

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

    let pub_sock = ctx.pub_socket().expect("pub socket failed");
    let mut sub_sock = ctx.sub_socket().expect("sub socket failed");

    sub_sock
        .set_subscription("prices")
        .expect("set_subscription failed");

    let pub_mon = SocketMonitor::open(&pub_sock).expect("pub monitor open failed");
    let mut sub_mon = SocketMonitor::open(&sub_sock).expect("sub monitor open failed");

    pub_sock.bind(&endpoint).expect("bind failed");
    sub_sock.connect(&endpoint).expect("connect failed");

    let (tx, rx) = mpsc::channel();
    sub_mon
        .on_event(move |event| {
            if event.is_connection_ready() {
                let _ = tx.send(());
            }
        })
        .expect("sub monitor callback failed");

    rx.recv_timeout(Duration::from_secs(5))
        .expect("monitor callback did not fire within 5s");
    drop(pub_mon);
    drop(sub_mon);

    let msg = Message::copy_from(b"101.25").unwrap();
    pub_sock.publish("prices", msg).expect("publish failed");

    let topic_msg = sub_sock.subscribe().expect("subscribe failed");
    let topic = topic_msg.topic().to_string();
    let payload = topic_msg.parts()[0].as_str().unwrap_or("?").to_string();
    let _ = rx;
    assert_eq!(topic, "prices");
    assert_eq!(payload, "101.25");
    println!("[pubsub/callback] publish: \"{topic}/101.25\" → subscribe: \"{topic}/{payload}\"");
}
