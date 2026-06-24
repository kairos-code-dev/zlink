// 자립형 가이드 예제: SPOT → 채널(ROUTER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   cargo run --example spot_channel_example

use std::sync::mpsc;
use std::thread;
use std::time::Duration;

use zlink::{Context, Message, Received, RecvFlags, RoutingId, SendFlags, SpotNode};

#[path = "sample_support.rs"]
mod sample_support;

fn main() {
    // --8<-- [start:doc]
    let ctx = Context::new().unwrap();
    let room_node = SpotNode::new(&ctx).unwrap();
    let room = room_node.create_spot().unwrap();
    let room_router = ctx.router_socket().unwrap();
    let api_router = ctx.router_socket().unwrap();
    let mut bridge = room_node.create_route_bridge().unwrap();

    let channel = "api";
    let endpoint = sample_support::tcp_endpoint();
    let room_router_rid = RoutingId::from(b"room-channel-client");
    let api_router_rid = RoutingId::from(b"room-channel-server");
    room_router.set_routing_id(&room_router_rid).unwrap();
    api_router.set_routing_id(&api_router_rid).unwrap();
    api_router.bind(&endpoint).unwrap();
    room_router.connect(&endpoint).unwrap();
    // "api" 채널 호출을 이 ROUTER로 내보내도록 bridge에 등록한다.
    bridge.attach_router_channel(channel, &room_router).unwrap();

    // API 서버(ROUTER)는 별도 스레드에서 요청을 받아 응답한다.
    let server = thread::spawn(move || {
        let mut received = Received::empty();
        api_router.recv(&mut received, RecvFlags::NONE).unwrap();
        received
            .reply()
            .message(Message::try_from(b"profile:level-7").unwrap())
            .submit()
            .unwrap();
    });

    // 게임룸이 API 채널로 outgame 요청을 보낸다.
    let (tx, rx) = mpsc::channel();
    let mut request_parts = vec![Message::try_from(b"get-profile").unwrap()];
    bridge
        .request(
            channel,
            &api_router_rid,
            &room.routing_id().unwrap(),
            &mut request_parts,
            SendFlags::NONE,
            Duration::from_secs(5),
            move |result| {
                if let Ok(parts) = result {
                    if !parts.is_empty() {
                        let _ = tx.send(parts[0].as_str().unwrap_or("").to_owned());
                    }
                }
            },
        )
        .unwrap();

    let reply = rx.recv_timeout(Duration::from_secs(6)).expect("no reply");
    server.join().unwrap();
    println!("[spot/channel] request \"get-profile\" -> reply \"{reply}\"");
    // --8<-- [end:doc]
}
