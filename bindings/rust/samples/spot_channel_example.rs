// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   cargo run --example spot_channel_example

use std::sync::mpsc;
use std::thread;
use std::time::Duration;

use zlink::{Context, Message, Received, RecvFlags, SpotNode};

#[path = "sample_support.rs"]
mod sample_support;

fn main() {
    // --8<-- [start:doc]
    let ctx = Context::new().unwrap();
    let room_node = SpotNode::new(&ctx).unwrap();
    let room = room_node.create_spot().unwrap();
    let room_dealer = ctx.dealer_socket().unwrap();
    let api_router = ctx.router_socket().unwrap();

    let channel = "api";
    let endpoint = sample_support::tcp_endpoint();
    api_router.bind(&endpoint).unwrap();
    room_dealer.connect(&endpoint).unwrap();
    // "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
    room_node
        .attach_channel_dealer_manual(channel, &room_dealer)
        .unwrap();

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
    room.request_to_channel(channel)
        .message(Message::try_from(b"get-profile").unwrap())
        .timeout(Duration::from_secs(5))
        .submit(move |result| {
            if let Ok(parts) = result {
                if !parts.is_empty() {
                    let _ = tx.send(parts[0].as_str().unwrap_or("").to_owned());
                }
            }
        })
        .unwrap();

    let reply = rx.recv_timeout(Duration::from_secs(6)).expect("no reply");
    server.join().unwrap();
    println!("[spot/channel] request \"get-profile\" -> reply \"{reply}\"");
    // --8<-- [end:doc]
}
