use std::thread;
use std::time::Duration;

use zlink::*;

fn tcp_endpoint() -> String {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    format!("tcp://127.0.0.1:{}", port)
}

#[test]
fn spot_dispatch_callback_can_subscribe() {
    let ctx = Context::new().unwrap();
    let mut node = SpotNode::new(&ctx).unwrap();
    let mut spot = node.create_spot().unwrap();
    let mut pub_sock = ctx.pub_socket().unwrap();
    let mut sub_sock = ctx.sub_socket().unwrap();
    let endpoint = tcp_endpoint();
    pub_sock.bind(&endpoint).unwrap();
    sub_sock.connect(&endpoint).unwrap();

    let attach_deadline = std::time::Instant::now() + Duration::from_secs(5);
    loop {
        match node.attach_pubsub("sample", &pub_sock, &sub_sock) {
            Ok(()) => break,
            Err(_) if std::time::Instant::now() < attach_deadline => {
                thread::sleep(Duration::from_millis(10));
            }
            Err(err) => panic!("attach_pubsub failed: {err:?}"),
        }
    }

    let (tx, rx) = std::sync::mpsc::channel();
    spot.on_dispatch_event_with_context(move |event, dispatch| {
        if event != SpotDispatchEvent::SubscribeReadable {
            return;
        }
        let result = dispatch
            .subscribe_with_flags(RecvFlags::DONT_WAIT)
            .map(|message| {
                (
                    message.service_name.as_deref().unwrap_or("").to_string(),
                    message.topic().to_string(),
                    message.parts()[0].as_bytes().to_vec(),
                )
            });
        let _ = tx.send(result);
    })
    .unwrap();
    spot.set_subscription("room:lobby").unwrap();
    spot.publish(
        "sample",
        "room:lobby",
        Message::copy_from(b"hello-spot").unwrap(),
    )
    .unwrap();

    let delivered = rx
        .recv_timeout(Duration::from_secs(5))
        .expect("spot dispatch callback did not deliver")
        .expect("subscribe in dispatch callback failed");
    assert_eq!(delivered.0, "sample");
    assert_eq!(delivered.1, "room:lobby");
    assert_eq!(delivered.2, b"hello-spot");

    sub_sock.close().unwrap();
    pub_sock.close().unwrap();
    spot.close().unwrap();
    node.close().unwrap();
}
