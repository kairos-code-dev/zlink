#[path = "perf_common.rs"]
mod common;

use std::collections::VecDeque;
use std::io::{self, BufRead};
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
use std::time::Duration;
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let ctx = common::perf_server_context();
    let router = ctx.router_socket().expect("router");
    let rid = RoutingId::from_bytes(b"perf-rr-server");
    router.set_routing_id(&rid).expect("set rid");
    router
        .common_options()
        .set_send_hwm(settings.send_hwm)
        .expect("sndhwm");
    router
        .common_options()
        .set_recv_hwm(settings.recv_hwm)
        .expect("rcvhwm");
    router
        .common_options()
        .set_recv_timeout(Duration::from_millis(1))
        .expect("recv timeout");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&router, &tls).expect("server tls");
    }
    let Some(bind_endpoint) =
        common::resolve_server_bind_endpoint("MULTI_ROUTER_ROUTER", &args.transport)
    else {
        return;
    };
    if let Err(err) = router.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("MULTI_ROUTER_ROUTER", &args.transport, "bind", err)
        {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = router.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);
    let poller = Poller::new().expect("poller");
    poller.add_socket(&router, POLLIN).expect("poller add");
    let mut pending = VecDeque::<(RoutingId, Vec<u8>)>::new();
    let stop = Arc::new(AtomicBool::new(false));
    let stop_reader = stop.clone();
    std::thread::spawn(move || {
        let stdin = io::stdin();
        for line in stdin.lock().lines() {
            let line = line.unwrap_or_default();
            if matches!(line.trim(), "STOP" | "QUIT") {
                stop_reader.store(true, Ordering::Release);
                break;
            }
        }
    });
    while !stop.load(Ordering::Acquire) {
        poller
            .modify_socket(
                &router,
                if pending.is_empty() {
                    POLLIN
                } else {
                    POLLIN | POLLOUT
                },
            )
            .expect("poller modify");
        match poller.wait(25) {
            Ok(Some(event)) => {
                if event.is_readable() {
                    loop {
                        match router.recv_with_flags(RecvFlags::DONT_WAIT) {
                            Ok(received) => {
                                let Some(rid) = received.routing_id().cloned() else {
                                    continue;
                                };
                                let reply_bytes =
                                    common::message_payload(received.parts()).to_vec();
                                if pending.is_empty() {
                                    match router.try_send(
                                        &rid,
                                        vec![Message::copy_from(&reply_bytes).expect("reply")],
                                    ) {
                                        Ok(true) => continue,
                                        Ok(false) => {}
                                        Err(err) => panic!("reply send failed: {err}"),
                                    }
                                }
                                pending.push_back((rid, reply_bytes));
                            }
                            Err(err) if err.code() == RecvResult::NoData => break,
                            Err(err) => panic!("router recv failed: {err}"),
                        }
                    }
                }
                if event.is_writable() {
                    while let Some((rid, reply_bytes)) = pending.pop_front() {
                        match router.try_send(
                            &rid,
                            vec![Message::copy_from(&reply_bytes).expect("pending reply")],
                        ) {
                            Ok(true) => {}
                            Ok(false) => {
                                pending.push_front((rid, reply_bytes));
                                break;
                            }
                            Err(err) => panic!("pending reply failed: {err}"),
                        }
                    }
                }
            }
            Ok(None) => {}
            Err(err) => panic!("poller wait failed: {err}"),
        }
    }
}
