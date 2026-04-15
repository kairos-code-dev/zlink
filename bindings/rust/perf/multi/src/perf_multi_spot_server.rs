//! MULTI_SPOT server: service-aware Spot publisher.

mod common;

use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

const SERVICE_NAME: &str = "perf.spot";
const TOPIC: &str = "perf.spot.topic";

fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("reserve port");
    let port = listener.local_addr().expect("local_addr").port();
    drop(listener);
    port
}

fn settle_ms() -> u64 {
    std::env::var("PERF_SETTLE_MS")
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(500)
}

fn bind_endpoint(transport: &str) -> String {
    match transport {
        "ws" => "ws://0.0.0.0:0".to_string(),
        "wss" => "wss://0.0.0.0:0".to_string(),
        "tls" => "tls://0.0.0.0:0".to_string(),
        _ => "tcp://0.0.0.0:0".to_string(),
    }
}

fn attach_service_pair(ctx: &Context, node: &SpotNode, service_name: &str) -> (PubSocket, SubSocket) {
    let pub_sock = ctx.pub_socket().expect("pub socket");
    let sub_sock = ctx.sub_socket().expect("sub socket");
    let endpoint = format!("tcp://127.0.0.1:{}", reserve_tcp_port());
    pub_sock.bind(&endpoint).expect("pub bind");
    sub_sock.connect(&endpoint).expect("sub connect");
    node.attach_pubsub(service_name, &pub_sock, &sub_sock)
        .expect("attach_pubsub");
    (pub_sock, sub_sock)
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let node = SpotNode::new(&ctx).expect("spot node");

    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        node.set_tls_server(&tls.cert, &tls.key, false)
            .expect("node tls");
    }

    let _service_pair = attach_service_pair(&ctx, &node, SERVICE_NAME);

    let endpoint = bind_endpoint(&args.transport);
    node.bind(&endpoint).expect("bind");
    let endpoint = node.last_endpoint().expect("endpoint");
    let spot = node.create_spot().expect("spot");

    common::print_ready(&endpoint);
    thread::sleep(Duration::from_millis(settle_ms()));

    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut seq: u64 = 1;

    while Instant::now() < deadline {
        common::encode_header(&mut buf, common::PHASE_ACTIVE, msg_size as u32, seq);
        seq += 1;
        let msg = Message::copy_from(&buf).expect("msg");
        let _ = spot.publish(SERVICE_NAME, TOPIC, msg);
    }

    let stop_deadline = Instant::now() + Duration::from_secs(1);
    while Instant::now() < stop_deadline {
        let stop_msg = Message::copy_from(common::STOP_TOKEN).expect("stop");
        let _ = spot.publish(SERVICE_NAME, TOPIC, stop_msg);
    }

    common::wait_for_stop_stdin();
}
