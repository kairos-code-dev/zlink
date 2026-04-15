//! MULTI_SPOT client: N Spot subscribers, callback-triggered drain loop.

mod common;

use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
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

fn attach_service_pair(
    ctx: &Context,
    node: &SpotNode,
    service_name: &str,
) -> (PubSocket, SubSocket) {
    let pub_sock = ctx.pub_socket().expect("pub socket");
    let sub_sock = ctx.sub_socket().expect("sub socket");
    let endpoint = format!("tcp://127.0.0.1:{}", reserve_tcp_port());
    pub_sock.bind(&endpoint).expect("pub bind");
    sub_sock.connect(&endpoint).expect("sub connect");
    node.attach_pubsub(service_name, &pub_sock, &sub_sock)
        .expect("attach_pubsub");
    (pub_sock, sub_sock)
}

struct ClientState {
    _node: SpotNode,
    _pub_sock: PubSocket,
    _sub_sock: SubSocket,
    spot: Spot,
    dispatch_pending: Arc<AtomicBool>,
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let mut clients: Vec<ClientState> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let node = SpotNode::new(&ctx).expect("spot node");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            node.set_tls_client(&tls.ca, "localhost", false)
                .expect("node tls");
        }

        let (pub_sock, sub_sock) = attach_service_pair(&ctx, &node, SERVICE_NAME);
        let mut spot = node.create_spot().expect("spot");
        spot.set_subscription(TOPIC).expect("subscribe");

        let dispatch_pending = Arc::new(AtomicBool::new(false));
        let dispatch_flag = dispatch_pending.clone();
        spot
            .on_dispatch_event(move |event| {
                if event == SpotDispatchEvent::SubscribeReadable {
                    dispatch_flag.store(true, Ordering::Release);
                }
            })
            .expect("on_dispatch_event");

        node.connect_peer(&args.endpoint).expect("connect_peer");

        clients.push(ClientState {
            _node: node,
            _pub_sock: pub_sock,
            _sub_sock: sub_sock,
            spot,
            dispatch_pending,
        });
    }

    thread::sleep(Duration::from_millis(500));

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds + 20);

    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;
    let mut stop_seen = false;

    while !stop_seen && Instant::now() < deadline {
        let mut progress = false;

        for client in &mut clients {
            if !client.dispatch_pending.swap(false, Ordering::AcqRel) {
                continue;
            }

            loop {
                match client.spot.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(topic_msg) => {
                        let data = common::message_payload(topic_msg.parts());
                        if common::is_stop_token(data) {
                            stop_seen = true;
                            break;
                        }
                        progress = true;
                        let phase = common::decode_phase(data);
                        if phase == common::PHASE_ACTIVE {
                            let sent_ts_ns = common::decode_sent_ts_ns(data);
                            latency_stats.record_ns(
                                common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64,
                            );
                            active_count += 1;
                        }
                    }
                    Err(_) => break,
                }
            }

            if stop_seen {
                break;
            }
        }

        if !progress {
            thread::yield_now();
        }
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult {
        count: active_count,
        ..stats
    };
    common::print_result(
        "MULTI_SPOT",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &final_stats,
    );
}
