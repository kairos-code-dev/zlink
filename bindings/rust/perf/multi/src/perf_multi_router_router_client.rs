#[path = "perf_common.rs"]
mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_client_context();
    let server_rid = RoutingId::from_bytes(b"perf-rr-server");
    let mut sockets: Vec<RouterSocket> = Vec::with_capacity(settings.clients);
    let poller = Poller::new().expect("poller");
    let mut payloads: Vec<Vec<u8>> = Vec::with_capacity(settings.clients);
    let mut waiting_reply = vec![false; settings.clients];
    let mut send_pending = vec![false; settings.clients];
    let mut seqs = vec![1u64; settings.clients];
    let mut socket_keys = Vec::with_capacity(settings.clients);
    let mut monitors: Vec<SocketMonitor> = Vec::with_capacity(settings.clients);

    for index in 0..settings.clients {
        let sock = ctx.router_socket().expect("router");
        sock.common_options()
            .set_send_hwm(settings.send_hwm)
            .expect("sndhwm");
        sock.common_options()
            .set_recv_hwm(settings.recv_hwm)
            .expect("rcvhwm");
        sock.common_options()
            .set_recv_timeout(Duration::from_millis(1))
            .expect("recv timeout");
        let rid = RoutingId::from_bytes(format!("CLIENT-{index}").as_bytes());
        sock.set_routing_id(&rid).expect("set rid");
        sock.router_options()
            .set_connect_routing_id(&server_rid)
            .expect("connect rid");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sock, &tls).expect("client tls");
        }
        let mon = SocketMonitor::open(&sock).expect("monitor");
        sock.connect(&args.endpoint).expect("connect");
        poller.add_socket(&sock, POLLIN).expect("poller add");
        payloads.push(vec![0u8; args.msg_size.max(common::HEADER_SIZE)]);
        socket_keys.push(common::raw_socket_handle_router(&sock) as usize);
        sockets.push(sock);
        monitors.push(mon);
    }

    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    for mon in &mut monitors {
        common::wait_monitor_ready(mon, ready_timeout, "multi router-router client");
    }

    let mut latency = common::LatencyStats::new();
    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    while Instant::now() < deadline {
        let mut progressed = false;
        for index in 0..sockets.len() {
            if waiting_reply[index] || send_pending[index] {
                continue;
            }
            common::encode_header(
                &mut payloads[index],
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                seqs[index],
            );
            match sockets[index].try_send(
                &server_rid,
                vec![Message::copy_from(&payloads[index]).expect("msg")],
            ) {
                Ok(true) => {
                    waiting_reply[index] = true;
                    seqs[index] += 1;
                    progressed = true;
                }
                Ok(false) => {
                    send_pending[index] = true;
                    poller
                        .modify_socket(&sockets[index], POLLIN | POLLOUT)
                        .expect("poller modify");
                }
                Err(err) => panic!("send failed: {err}"),
            }
        }
        if progressed {
            continue;
        }

        let events = common::wait_native_poll_events(&poller, sockets.len(), 25)
            .expect("poller wait all");
        for event in events {
            let Some(index) = socket_keys
                .iter()
                .position(|socket_key| *socket_key == event.socket as usize)
            else {
                continue;
            };

            if event.events & POLLOUT != 0 {
                send_pending[index] = false;
                poller
                    .modify_socket(&sockets[index], POLLIN)
                    .expect("poller modify");
            }

            if event.events & POLLIN != 0 {
                loop {
                    match sockets[index].recv_with_flags(RecvFlags::DONT_WAIT) {
                        Ok(received) => {
                            let data = common::message_payload(received.parts());
                            if !common::is_valid_active_message(data, args.msg_size) {
                                continue;
                            }
                            let sent_ts_ns = common::decode_sent_ts_ns(data);
                            let latency_ns =
                                common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64
                                    / 2.0;
                            latency.record_ns(latency_ns);
                            waiting_reply[index] = false;
                        }
                        Err(err) if err.code() == RecvResult::NoData => break,
                        Err(err) => panic!("recv failed: {err}"),
                    }
                }
                if !send_pending[index] {
                    poller
                        .modify_socket(&sockets[index], POLLIN)
                        .expect("poller modify");
                }
            }
        }
    }

    common::print_result(
        "MULTI_ROUTER_ROUTER",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &latency.finish(),
    );
}
