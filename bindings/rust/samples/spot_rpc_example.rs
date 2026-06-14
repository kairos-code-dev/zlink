// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
//   cargo run --example spot_rpc_example

use std::sync::mpsc;
use std::thread::sleep;
use std::time::{Duration, Instant};

use zlink::{Context, Message, Received, RecvFlags, RoutingId, SendFlags, SpotNode, SubmitResult};

#[path = "sample_support.rs"]
mod sample_support;

fn main() {
    // --8<-- [start:doc]
    let ctx = Context::new().unwrap();
    let server_node = SpotNode::new(&ctx).unwrap();
    let client_node = SpotNode::new(&ctx).unwrap();
    let server = server_node.create_spot().unwrap();
    let client = client_node.create_spot().unwrap();

    server_node
        .set_routing_id(&RoutingId::from(b"rpc-server-node"))
        .unwrap();
    client_node
        .set_routing_id(&RoutingId::from(b"rpc-client-node"))
        .unwrap();
    server
        .set_routing_id(&RoutingId::from(b"rpc-server-spot"))
        .unwrap();
    client
        .set_routing_id(&RoutingId::from(b"rpc-client-spot"))
        .unwrap();
    // 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
    server_node
        .set_router_bind(&sample_support::tcp_endpoint())
        .unwrap();
    client_node
        .set_router_bind(&sample_support::tcp_endpoint())
        .unwrap();
    let server_endpoint = sample_support::tcp_endpoint();
    let client_endpoint = sample_support::tcp_endpoint();
    server_node.set_pub_bind(&server_endpoint).unwrap();
    client_node.set_pub_bind(&client_endpoint).unwrap();
    server_node.connect_peer(&client_endpoint).unwrap();
    client_node.connect_peer(&server_endpoint).unwrap();

    sample_support::wait_spot_peer_connected(&server_node, Duration::from_secs(5));
    sample_support::wait_spot_peer_connected(&client_node, Duration::from_secs(5));

    // 클라이언트 Spot이 서버 Spot으로 요청한다.
    let (reply_tx, reply_rx) = mpsc::channel();
    let submit_deadline = Instant::now() + Duration::from_secs(5);
    loop {
        let reply_tx_for_callback = reply_tx.clone();
        match client
            .request_to_spot(
                RoutingId::from(b"rpc-server-node"),
                RoutingId::from(b"rpc-server-spot"),
            )
            .message(Message::try_from(b"ping").unwrap())
            .flags(SendFlags::DONT_WAIT)
            .timeout(Duration::from_secs(3))
            .submit(move |result| {
                if let Ok(parts) = result {
                    if !parts.is_empty() {
                        let _ =
                            reply_tx_for_callback.send(parts[0].as_str().unwrap_or("").to_owned());
                    }
                }
            }) {
            Ok(()) => break,
            Err(err)
                if err.code() == SubmitResult::NotConnected && Instant::now() < submit_deadline =>
            {
                sleep(Duration::from_millis(10));
            }
            Err(err) => panic!("spot rpc request submit failed: {err}"),
        }
    }

    // 서버 Spot의 라우티드 수신을 폴링해 응답하고, 응답이 올 때까지 기다린다.
    let mut received = Received::empty();
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Ok(true) = server.recv_routed(&mut received, RecvFlags::DONT_WAIT) {
            received
                .reply()
                .message(Message::try_from(b"pong").unwrap())
                .submit()
                .unwrap();
        }
        if let Ok(reply) = reply_rx.try_recv() {
            println!("[spot/rpc] request \"ping\" -> reply \"{reply}\"");
            return;
        }
        if Instant::now() > deadline {
            break;
        }
        sleep(Duration::from_millis(10));
    }
    panic!("spot rpc: no reply");
    // --8<-- [end:doc]
}
