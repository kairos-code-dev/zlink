#[path = "perf_common.rs"]
mod common;

use std::sync::mpsc;
use zlink::*;

fn build_packet_frame(header: &[u8], body: &[u8]) -> Vec<u8> {
    let mut packet = Vec::with_capacity(6 + header.len() + body.len());
    packet.extend_from_slice(&(header.len() as u16).to_be_bytes());
    packet.extend_from_slice(&(body.len() as u32).to_be_bytes());
    packet.extend_from_slice(header);
    packet.extend_from_slice(body);
    packet
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let ctx = common::perf_server_context();
    let mut stream = ctx.stream_socket().expect("stream");
    stream
        .common_options()
        .set_send_hwm(settings.send_hwm)
        .expect("sndhwm");
    stream
        .common_options()
        .set_recv_hwm(settings.recv_hwm)
        .expect("rcvhwm");
    stream
        .common_options()
        .set_send_timeout(std::time::Duration::from_millis(settings.send_timeout_ms))
        .expect("sndtimeo");
    stream
        .common_options()
        .set_recv_timeout(std::time::Duration::from_millis(settings.recv_timeout_ms))
        .expect("rcvtimeo");
    stream
        .common_options()
        .set_tcp_nodelay(true)
        .expect("tcp_nodelay");
    let send_handle = stream.send_handle();
    stream
        .on_packet(move |routing_id, header, body| {
            let packet = build_packet_frame(header.as_bytes(), body.as_bytes());
            let msg = Message::copy_from(&packet).expect("packet");
            let _ = send_handle.send_to(&routing_id, msg);
        })
        .expect("on_packet");
    let Some(bind_endpoint) = common::resolve_server_bind_endpoint("MULTI_STREAM", &args.transport)
    else {
        return;
    };
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&stream, &tls).expect("stream tls");
    }
    if let Err(err) = stream.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("MULTI_STREAM", &args.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = stream.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);
    let (stop_tx, stop_rx) = mpsc::channel::<()>();
    std::thread::spawn(move || {
        common::wait_for_stop_stdin();
        let _ = stop_tx.send(());
    });
    let _ = stop_rx.recv();
}
