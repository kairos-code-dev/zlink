mod common;

use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let mut sockets: Vec<DealerSocket> = Vec::with_capacity(settings.clients);
    let mut monitors: Vec<SocketMonitor> = Vec::with_capacity(settings.clients);

    for index in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        sock.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
        sock.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
        sock.common_options()
            .set_recv_timeout(Duration::from_millis(1))
            .expect("recv timeout");
        let rid = RoutingId::from_bytes(format!("CLIENT-{index}").as_bytes());
        sock.set_routing_id(&rid).expect("set rid");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sock, &tls).expect("client tls");
        }
        let mon = SocketMonitor::open(&sock).expect("monitor");
        sock.connect(&args.endpoint).expect("connect");
        sockets.push(sock);
        monitors.push(mon);
    }

    for mon in &monitors {
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            if Instant::now() >= deadline {
                panic!("multi dealer-router client connection-ready gate timed out");
            }
            match mon.recv() {
                Ok(ev) if ev.is_connection_ready() => break,
                Ok(_) => continue,
                Err(_) => thread::sleep(Duration::from_millis(1)),
            }
        }
    }

    let latency = Arc::new(Mutex::new(common::LatencyStats::new()));
    let payload = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    let mut threads = Vec::with_capacity(sockets.len());
    for sock in sockets {
        let msg_size = args.msg_size;
        let duration_seconds = settings.duration_seconds;
        let payload = payload.clone();
        let latency = Arc::clone(&latency);
        threads.push(thread::spawn(move || {
            let deadline = Instant::now() + Duration::from_secs(duration_seconds);
            let mut seq = 1u64;
            let mut buf = payload;
            while Instant::now() < deadline {
                common::encode_header(&mut buf, common::PHASE_ACTIVE, msg_size as u32, seq);
                let msg = Message::copy_from(&buf).expect("msg");
                if sock.send(msg).is_err() {
                    continue;
                }
                match sock.recv() {
                    Ok(received) => {
                        let data = common::message_payload(received.parts());
                        if !common::is_valid_active_message(data, msg_size) {
                            continue;
                        }
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        let latency_ns =
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64 / 2.0;
                        latency.lock().expect("latency lock").record_ns(latency_ns);
                        seq += 1;
                    }
                    Err(_) => {}
                }
            }
        }));
    }

    for handle in threads {
        handle.join().expect("join");
    }
    let total = latency.lock().expect("latency lock").finish();

    common::print_result(
        "MULTI_DEALER_ROUTER",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &total,
    );
}
