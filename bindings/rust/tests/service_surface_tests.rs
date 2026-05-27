//! Service surface tests - verify channel/query/introspection APIs exist.

use std::future::Future;
use std::pin::pin;
use std::task::{Context as TaskContext, Poll, Waker};

use zlink::{
    AutoConnectType, Context, Discovery, Message, POLLIN, POLLOUT, PairSocket, PollEvent,
    PollSourceKind, Poller, Received, RecvFlags, Registry, RegistryQueryClient, RoutingId,
    SendFlags, SocketMonitor, Spot, SpotDispatchInfo, SpotNode, SubscriptionEvent, Timer,
};

fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    port
}

fn block_on_ready<F: Future>(future: F) -> F::Output {
    let waker = Waker::noop();
    let mut future = pin!(future);
    let mut cx = TaskContext::from_waker(waker);
    match future.as_mut().poll(&mut cx) {
        Poll::Ready(output) => output,
        Poll::Pending => panic!("future unexpectedly pending"),
    }
}

#[test]
fn discovery_member_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, AutoConnectType::ClientServer, "svc").unwrap();
    let _ = discovery.member_peers().unwrap();
}

#[test]
fn spot_node_snapshot_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let endpoint = format!("tcp://127.0.0.1:{}", reserve_tcp_port());
    node.set_pub_bind(&endpoint).unwrap();
    let _ = node.status_snapshot().unwrap();
    let _ = node.peers_snapshot().unwrap();
    let _ = node.subjects_snapshot(None).unwrap();
    let _ = node.set_tls_server("cert", "key", false);
    let _ = node.set_tls_client("ca", "localhost", true);
    node.set_routing_id(&RoutingId::from_bytes(b"node-surface"))
        .unwrap();
    let _ = node.routing_id().unwrap();
}

#[test]
fn spot_callback_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let spot = node.create_spot().unwrap();
    spot.set_routing_id(&RoutingId::from_bytes(b"spot-surface"))
        .unwrap();
    let _ = spot.routing_id().unwrap();
    let _ = spot
        .publish("topic")
        .message(Message::try_from(b"payload").unwrap())
        .submit();
    let _ = spot
        .publish("topic")
        .message(Message::try_from(b"payload").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit();
    let _ = spot.publish_part(
        "topic",
        Message::try_from(b"payload").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let _ = spot.subscribe_part(RecvFlags::DONT_WAIT);
    let _ = spot
        .send_channel("svc-surface")
        .message(Message::try_from(b"payload").unwrap())
        .submit();
    let _ = spot
        .send_channel("svc-surface")
        .message(Message::try_from(b"payload").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit();
    let rid = RoutingId::from_bytes(b"rid-surface");
    let spot_rid = RoutingId::from_bytes(b"spot-surface");
    let _ = spot
        .send_to_spot(rid.clone(), spot_rid.clone())
        .message(Message::try_from(b"payload").unwrap())
        .submit();
    let _ = spot
        .send_to_spot(rid, spot_rid)
        .message(Message::try_from(b"payload").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit();
    let _ = block_on_ready(
        spot.request_channel("svc-surface")
            .message(Message::try_from(b"payload").unwrap())
            .timeout(std::time::Duration::from_millis(1))
            .submit_async(),
    );
    let _ = spot
        .request_channel("svc-surface")
        .message(Message::try_from(b"payload").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit(|_result| {});
    let mut event = SubscriptionEvent::empty();
    let _ = spot.receive_subscription_event(&mut event, RecvFlags::DONT_WAIT);
    let _on_send_ready = Spot::on_send_ready::<fn()>;
    let _on_routed_receive = Spot::on_routed_receive::<fn(Received)>;
    let _on_dispatch_event = Spot::on_dispatch_event::<fn(SpotDispatchInfo<'_>)>;
    let dealer = ctx.dealer_socket().unwrap();
    let _ = dealer.set_channel_name("surface-channel");
    let _ = dealer.channel_name();
}

#[test]
fn service_close_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let mut discovery = Discovery::new(&ctx, AutoConnectType::SpotMesh, "svc-close").unwrap();
    let mut node = SpotNode::new(&ctx).unwrap();
    let mut spot = node.create_spot().unwrap();
    let mut registry = Registry::new(&ctx).unwrap();
    let mut query = RegistryQueryClient::new(&ctx).unwrap();

    spot.close().unwrap();
    node.close().unwrap();
    discovery.close().unwrap();
    registry.close().unwrap();
    query.close().unwrap();
}

#[test]
fn socket_attach_discovery_blocks_manual_connect_and_close() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, AutoConnectType::ClientServer, "svc-attach").unwrap();
    let mut dealer = ctx.dealer_socket().unwrap();

    dealer.attach_discovery(&discovery).unwrap();
    assert!(dealer.connect("tcp://127.0.0.1:1").is_err());
    assert!(dealer.disconnect("tcp://127.0.0.1:1").is_err());
    assert!(dealer.unbind("tcp://127.0.0.1:1").is_err());
    assert!(dealer.close().is_err());
}

#[test]
fn discovery_close_terminates_attached_socket_lifecycle() {
    let ctx = Context::new().unwrap();
    let mut discovery =
        Discovery::new(&ctx, AutoConnectType::ClientServer, "svc-attach-close").unwrap();
    let dealer = ctx.dealer_socket().unwrap();
    dealer.attach_discovery(&discovery).unwrap();
    discovery.close().unwrap();
    assert!(dealer.connect("tcp://127.0.0.1:1").is_err());
}

#[test]
fn registry_snapshot_and_query_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let registry = Registry::new(&ctx).unwrap();
    let _ = registry.status_snapshot().unwrap();
    let _ = registry.service_summary_snapshot().unwrap();
    let _ = registry.topology_snapshot().unwrap();
    let _ = registry.set_tls_server("cert", "key", false);
    let _ = registry.set_tls_client("ca", "localhost", true);
}

#[test]
fn registry_query_client_surface_exists() {
    let ctx = Context::new().unwrap();
    let client = RegistryQueryClient::new(&ctx).unwrap();
    let _ = client.snapshot(None);
}

#[test]
fn discovery_tls_client_surface_exists() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, AutoConnectType::SpotMesh, "svc-tls").unwrap();
    let _ = discovery.set_tls_client("ca", "localhost", true);
}

#[test]
fn discovery_resolve_spot_surface_exists() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, AutoConnectType::SpotMesh, "svc-resolve").unwrap();
    assert!(!discovery.spot_owner_sync_enabled().unwrap());
    discovery.set_spot_owner_sync_enabled(true).unwrap();
    assert!(discovery.spot_owner_sync_enabled().unwrap());
    assert!(!discovery.actor_route_sync_enabled().unwrap());
    discovery.set_actor_route_sync_enabled(true).unwrap();
    assert!(discovery.actor_route_sync_enabled().unwrap());
    let _ = discovery.resolve_spot(&RoutingId::from_bytes(b"spot-rid"));
    let _ = discovery.resolve_actor("actor-rid");
}

#[test]
fn actor_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    node.set_routing_id(&RoutingId::from_bytes(b"actor-node"))
        .unwrap();
    let spot = node.create_spot().unwrap();
    spot.set_routing_id(&RoutingId::from_bytes(b"actor-spot"))
        .unwrap();
    let stream = ctx.stream_socket().unwrap();
    let session_rid = RoutingId::from_bytes(b"actor-session");

    let mut actor = node.create_actor("actor-surface").unwrap();
    let actor_ref = actor.actor_ref().unwrap();
    assert!(!actor_ref.is_unchecked());
    assert_eq!(
        node.actor_lookup("actor-surface").unwrap().actor_id,
        "actor-surface"
    );
    assert_eq!(
        SpotNode::remote_actor_ref(&RoutingId::from_bytes(b"remote-node"), "remote-actor")
            .unwrap()
            .generation,
        0
    );
    let _ = node.spots_snapshot().unwrap();
    let _ = node.actors_snapshot().unwrap();
    let _ = spot.actors_snapshot().unwrap();
    let _ = actor.recv_part_with_flags(RecvFlags::DONT_WAIT);
    let _ = actor
        .send_bound_session_msg()
        .message(Message::try_from(b"payload").unwrap())
        .flags(SendFlags::DONT_WAIT);
    let _ = actor.close_bound_session(std::time::Duration::from_millis(1));
    let _ = actor
        .join(&spot)
        .message(Message::try_from(b"join").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(std::time::Duration::from_millis(1));
    let _ = spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT);
    let _ = stream
        .bind_actor(&session_rid, &actor_ref)
        .timeout(std::time::Duration::from_millis(1));
    let _ = stream
        .send_bound_actor(&session_rid, "actor-surface")
        .message(Message::try_from(b"payload").unwrap())
        .flags(SendFlags::DONT_WAIT);
    let _ = stream
        .unbind_actor(&session_rid, "actor-surface")
        .timeout(std::time::Duration::from_millis(1));
    let _ = actor.leave(&spot);
    let _ = actor.close();
    let remote =
        SpotNode::remote_actor_ref(&RoutingId::from_bytes(b"remote-node"), "remote-actor").unwrap();
    let _ = node
        .destroy_actor(&remote)
        .timeout(std::time::Duration::from_millis(1));
    let _ = node
        .join_actor(
            &remote,
            &RoutingId::from_bytes(b"actor-node"),
            &RoutingId::from_bytes(b"actor-spot"),
        )
        .message(Message::try_from(b"join").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(std::time::Duration::from_millis(1));
    let _ = node
        .join_actor_entry_spot(&remote, &RoutingId::from_bytes(b"actor-node"))
        .timeout(std::time::Duration::from_millis(1));
    let _ = node
        .leave_actor(&remote, &RoutingId::from_bytes(b"actor-spot"))
        .timeout(std::time::Duration::from_millis(1));
    fn _dispatch_handler(_info: SpotDispatchInfo<'_>) {}
    let _on_dispatch_event = Spot::on_dispatch_event::<fn(SpotDispatchInfo<'_>)>;
    let _ = _on_dispatch_event;
    let _ = _dispatch_handler as fn(SpotDispatchInfo<'_>);
}

#[test]
fn actor_entry_spot_join_returns_actor_ref() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let node_rid = RoutingId::from_bytes(b"entry-join-node");
    node.set_routing_id(&node_rid).unwrap();
    let actor = node.create_actor("entry-join-actor").unwrap();
    let actor_ref = actor.actor_ref().unwrap();
    let (tx, rx) = std::sync::mpsc::channel();

    node.join_actor_entry_spot(&actor_ref, &node_rid)
        .timeout(std::time::Duration::from_secs(1))
        .submit(move |result| tx.send(result).unwrap())
        .unwrap();

    let result = rx.recv_timeout(std::time::Duration::from_secs(2)).unwrap();
    assert_eq!(result.result, zlink::RequestResult::Ok);
    assert_eq!(result.actor.actor_id, "entry-join-actor");
    assert_eq!(result.target_node_rid, node_rid);
}

#[test]
fn typed_poller_surface_exists() {
    let ctx = Context::new().unwrap();
    let socket = ctx.pair_socket().unwrap();
    socket.bind("inproc://typed-poller-surface").unwrap();
    let poller = Poller::new().unwrap();
    poller.add_socket(&socket, POLLIN, 7).unwrap();
    let _ = poller.size();
    poller.modify_socket(&socket, POLLOUT).unwrap();
    poller.remove_socket(&socket).unwrap();
}

#[test]
fn typed_poller_wait_on_pair_socket_does_not_crash() {
    let ctx = Context::new().unwrap();
    let server = ctx.pair_socket().unwrap();
    let client = ctx.pair_socket().unwrap();
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    let endpoint = format!("tcp://127.0.0.1:{port}");

    let server_mon = SocketMonitor::open(&server).unwrap();
    let client_mon = SocketMonitor::open(&client).unwrap();
    server.bind(&endpoint).unwrap();
    client.connect(&endpoint).unwrap();
    server_mon.recv().unwrap();
    client_mon.recv().unwrap();

    let poller = Poller::new().unwrap();
    poller.add_socket(&server, POLLIN, 7).unwrap();
    client
        .send()
        .message(Message::try_from(b"poller").unwrap())
        .submit()
        .unwrap();

    let mut events = vec![PollEvent::default(); 4];
    let count = poller.wait(&mut events, 1000).unwrap();
    assert_eq!(count, 1);
    assert_eq!(events[0].source_kind, PollSourceKind::Socket);
    assert_eq!(events[0].slot, 7);
    assert!(
        events[0].is_readable(),
        "poller event must carry readiness bits"
    );
}

#[test]
fn typed_poller_rejects_empty_event_buffer() {
    let poller = Poller::new().unwrap();
    let mut events = Vec::<PollEvent>::new();
    assert!(poller.wait(&mut events, 0).is_err());
}

#[test]
fn typed_poller_capacity_leaves_remaining_ready_source() {
    let ctx = Context::new().unwrap();
    let sender1 = ctx.pair_socket().unwrap();
    let receiver1 = ctx.pair_socket().unwrap();
    let sender2 = ctx.pair_socket().unwrap();
    let receiver2 = ctx.pair_socket().unwrap();
    sender1.bind("inproc://typed-poller-capacity-a").unwrap();
    receiver1
        .connect("inproc://typed-poller-capacity-a")
        .unwrap();
    sender2.bind("inproc://typed-poller-capacity-b").unwrap();
    receiver2
        .connect("inproc://typed-poller-capacity-b")
        .unwrap();

    let poller = Poller::new().unwrap();
    poller.add_socket(&receiver1, POLLIN, 101).unwrap();
    poller.add_socket(&receiver2, POLLIN, 102).unwrap();
    sender1
        .send()
        .message(Message::try_from(b"a").unwrap())
        .submit()
        .unwrap();
    sender2
        .send()
        .message(Message::try_from(b"b").unwrap())
        .submit()
        .unwrap();

    let mut events = vec![PollEvent::default(); 1];
    let count = poller.wait(&mut events, 1000).unwrap();
    assert_eq!(count, 1);
    let first_slot = events[0].slot;
    assert!(first_slot == 101 || first_slot == 102);
    if first_slot == 101 {
        assert_eq!(recv_text(&receiver1), b"a");
    } else {
        assert_eq!(recv_text(&receiver2), b"b");
    }

    let count = poller.wait(&mut events, 1000).unwrap();
    assert_eq!(count, 1);
    assert_ne!(events[0].slot, first_slot);
    if events[0].slot == 101 {
        assert_eq!(recv_text(&receiver1), b"a");
    } else {
        assert_eq!(recv_text(&receiver2), b"b");
    }
}

#[test]
fn typed_poller_modify_remove_and_timeout_follow_core_semantics() {
    let ctx = Context::new().unwrap();
    let sender = ctx.pair_socket().unwrap();
    let receiver = ctx.pair_socket().unwrap();
    sender.bind("inproc://typed-poller-modify-remove").unwrap();
    receiver
        .connect("inproc://typed-poller-modify-remove")
        .unwrap();

    let poller = Poller::new().unwrap();
    poller.add_socket(&receiver, POLLIN, 31).unwrap();
    poller.modify_socket(&receiver, 0).unwrap();
    sender
        .send()
        .message(Message::try_from(b"hidden").unwrap())
        .submit()
        .unwrap();

    let mut events = vec![PollEvent::default(); 1];
    assert_eq!(poller.wait(&mut events, 20).unwrap(), 0);

    poller.modify_socket(&receiver, POLLIN).unwrap();
    assert_eq!(poller.wait(&mut events, 1000).unwrap(), 1);
    assert_eq!(events[0].slot, 31);
    assert_eq!(recv_text(&receiver), b"hidden");

    poller.remove_socket(&receiver).unwrap();
    sender
        .send()
        .message(Message::try_from(b"removed").unwrap())
        .submit()
        .unwrap();
    assert_eq!(poller.wait(&mut events, 0).unwrap(), 0);
}

#[test]
fn typed_poller_distinguishes_timer_and_socket_in_same_buffer() {
    let ctx = Context::new().unwrap();
    let sender = ctx.pair_socket().unwrap();
    let receiver = ctx.pair_socket().unwrap();
    sender.bind("inproc://typed-poller-timer-socket").unwrap();
    receiver
        .connect("inproc://typed-poller-timer-socket")
        .unwrap();

    let timer = Timer::new().unwrap();
    let poller = Poller::new().unwrap();
    poller.add_socket(&receiver, POLLIN, 41).unwrap();
    poller.add_timer(&timer, 42).unwrap();
    sender
        .send()
        .message(Message::try_from(b"socket").unwrap())
        .submit()
        .unwrap();
    timer.start(5_000_000, 1).unwrap();

    let mut events = vec![PollEvent::default(); 2];
    let mut saw_socket = false;
    let mut saw_timer = false;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
    while (!saw_socket || !saw_timer) && std::time::Instant::now() < deadline {
        let count = poller.wait(&mut events, 200).unwrap();
        for event in &events[..count] {
            match event.source_kind {
                PollSourceKind::Socket => {
                    assert_eq!(event.slot, 41);
                    assert_eq!(recv_text(&receiver), b"socket");
                    saw_socket = true;
                }
                PollSourceKind::Timer => {
                    assert_eq!(event.slot, 42);
                    assert_eq!(timer.recv().unwrap(), Some(1));
                    saw_timer = true;
                }
                PollSourceKind::Fd => {}
            }
        }
    }
    assert!(saw_socket);
    assert!(saw_timer);
}

fn recv_text(socket: &PairSocket) -> Vec<u8> {
    let mut received = Received::empty();
    assert!(socket.recv(&mut received, RecvFlags::NONE).unwrap());
    received.parts()[0].as_bytes().to_vec()
}
