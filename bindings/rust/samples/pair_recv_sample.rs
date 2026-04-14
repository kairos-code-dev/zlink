//! PAIR direct recv sample – demonstrates basic send/recv messaging.

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

    let server = ctx.pair_socket().expect("server socket failed");
    let client = ctx.pair_socket().expect("client socket failed");

    let server_mon = SocketMonitor::open(&server).expect("server monitor open failed");
    let client_mon = SocketMonitor::open(&client).expect("client monitor open failed");

    server.bind(&endpoint).expect("bind failed");
    client.connect(&endpoint).expect("connect failed");

    wait_connected(&[&server_mon, &client_mon]);
    drop(server_mon);
    drop(client_mon);

    let msg = Message::copy_from(b"hello-pair").expect("message creation failed");
    client.send(msg).expect("send failed");

    let received = server.recv().expect("recv failed");
    let payload = received.parts()[0].as_str().expect("utf8 error");
    assert_eq!(payload, "hello-pair");
    println!("[pair/recv] send: \"hello-pair\" → recv: \"{}\"", payload);
}
