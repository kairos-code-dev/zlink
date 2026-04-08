#![allow(dead_code)]

use std::time::{Duration, Instant};

use zlink::SocketMonitor;
use zlink::SpotNode;

pub fn tcp_endpoint() -> String {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    format!("tcp://127.0.0.1:{}", port)
}

pub fn wait_connected(monitors: &[&SocketMonitor]) {
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

pub fn wait_stream_connected(monitor: &SocketMonitor) {
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

pub fn wait_until<F>(mut predicate: F, timeout: Duration, description: &str)
where
    F: FnMut() -> bool,
{
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        if predicate() {
            return;
        }
        std::thread::yield_now();
    }
    panic!("timed out waiting for {description}");
}

pub fn wait_spot_peer_connected(node: &SpotNode, timeout: Duration) {
    wait_until(
        || {
            node.status_snapshot()
                .map(|status| status.connected_peer_count > 0)
                .unwrap_or(false)
        },
        timeout,
        "spot peer connection",
    );
}
