//! STREAM direct recv sample – demonstrates STREAM socket with direct recv.
//! The STREAM socket binds as a server; a raw TCP client connects inward.

use std::io::Write;
use std::net::TcpStream;

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

    let stream = ctx.stream_socket().expect("stream socket failed");
    stream.bind("tcp://127.0.0.1:0").expect("bind failed");
    let endpoint = stream.last_endpoint().expect("last_endpoint failed");
    let stream_mon = SocketMonitor::open(&stream).expect("stream monitor open failed");

    let tcp_addr = endpoint.strip_prefix("tcp://").unwrap();
    let mut tcp_client = TcpStream::connect(tcp_addr).expect("tcp connect failed");
    tcp_client.set_nodelay(true).expect("set_nodelay failed");
    wait_stream_connected(&stream_mon);
    drop(stream_mon);

    tcp_client
        .write_all(b"hello-stream")
        .expect("tcp write failed");
    tcp_client.flush().expect("tcp flush failed");

    let received = stream.recv().expect("server recv failed");
    let peer_id = received
        .routing_id()
        .expect("missing routing id")
        .to_u32()
        .expect("stream routing id must be a u32");
    assert_eq!(received.parts()[0].as_bytes(), b"hello-stream");
    stream
        .send(
            received.routing_id().expect("missing routing id"),
            Message::copy_from(b"hello-stream").expect("reply failed"),
        )
        .expect("stream send failed");
    println!(
        "[stream/recv] peer: {} send: \"hello-stream\" → recv: \"{}\"",
        peer_id,
        received.parts()[0].as_str().unwrap()
    );
}
