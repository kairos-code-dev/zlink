//! PAIR callback sample – demonstrates receive via callback handler.

use std::sync::mpsc;
use std::time::Duration;

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

    let mut server = ctx.pair_socket().expect("server socket failed");
    let client = ctx.pair_socket().expect("client socket failed");

    let (tx, rx) = mpsc::channel();

    server
        .on_receive(move |received| {
            let payload = received.parts()[0]
                .as_str()
                .unwrap_or("(binary)")
                .to_string();
            let _ = tx.send(payload);
        })
        .expect("on_receive failed");

    let server_mon = SocketMonitor::open(&server, MONITOR_EVENT_CONNECTION_READY_CHANGED)
        .expect("server monitor open failed");
    let client_mon = SocketMonitor::open(&client, MONITOR_EVENT_CONNECTION_READY_CHANGED)
        .expect("client monitor open failed");

    server.bind(&endpoint).expect("bind failed");
    client.connect(&endpoint).expect("connect failed");

    wait_connected(&[&server_mon, &client_mon]);
    drop(server_mon);
    drop(client_mon);

    let msg = Message::from_bytes(b"hello-pair").unwrap();
    client.send(msg).expect("send failed");

    let payload = rx
        .recv_timeout(Duration::from_secs(5))
        .expect("pair callback did not fire within 5s");
    assert_eq!(payload, "hello-pair");
    println!(
        "[pair/callback] send: \"hello-pair\" → recv: \"{}\"",
        payload
    );
}
