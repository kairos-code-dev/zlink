//! STREAM callback sample – demonstrates STREAM socket with packet handler.
//! The STREAM socket binds as a server; a raw TCP client connects inward.

use std::io::Write;
use std::net::TcpStream;
use std::sync::mpsc;
use std::time::Duration;

use zlink::{Context, SocketMonitor};

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

    let mut stream = ctx.stream_socket().expect("stream socket failed");
    stream.bind("tcp://127.0.0.1:0").expect("bind failed");
    let endpoint = stream.last_endpoint().expect("last_endpoint failed");
    let stream_mon = SocketMonitor::open(&stream).expect("stream monitor open failed");

    let (tx, rx) = mpsc::channel();

    stream
        .on_packet(move |routing_id, header, body| {
            assert!(!routing_id.as_bytes().is_empty());
            assert!(header.as_bytes().is_empty());
            let _ = tx.send(body.as_bytes().to_vec());
        })
        .expect("on_packet failed");

    let tcp_addr = endpoint.strip_prefix("tcp://").unwrap();
    let mut tcp_client = TcpStream::connect(tcp_addr).expect("tcp connect failed");
    tcp_client.set_nodelay(true).expect("set_nodelay failed");
    wait_stream_connected(&stream_mon);
    drop(stream_mon);

    tcp_client
        .write_all(b"hello-stream")
        .expect("tcp write failed");
    tcp_client.flush().expect("tcp flush failed");
    let payload = rx
        .recv_timeout(Duration::from_secs(5))
        .expect("stream callback did not fire within 5s");
    assert_eq!(payload, b"hello-stream");
    let recv_str = std::str::from_utf8(&payload).unwrap();
    println!(
        "[stream/callback] send: \"hello-stream\" → recv: \"{}\"",
        recv_str
    );
}
