//! Service surface tests - verify channel/query/introspection APIs exist.

use std::future::Future;
use std::pin::pin;
use std::task::{Context as TaskContext, Poll, Waker};

use zlink::*;

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
    node.bind(&endpoint).unwrap();
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
    let _ = spot.publish(
        "svc-surface",
        "topic",
        Message::copy_from(b"payload").unwrap(),
    );
    let _ = spot.publish_with_flags(
        "svc-surface",
        "topic",
        Message::copy_from(b"payload").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let _ = spot.send_channel("svc-surface", Message::copy_from(b"payload").unwrap());
    let _ = spot.send_channel_with_flags(
        "svc-surface",
        Message::copy_from(b"payload").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let rid = RoutingId::from_bytes(b"rid-surface");
    let spot_rid = RoutingId::from_bytes(b"spot-surface");
    let _ = spot.send_to_spot(
        rid.clone(),
        spot_rid.clone(),
        Message::copy_from(b"payload").unwrap(),
    );
    let _ = spot.send_to_spot_with_flags(
        rid,
        spot_rid,
        Message::copy_from(b"payload").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let _ = block_on_ready(spot.request_channel(
        "svc-surface",
        Message::copy_from(b"payload").unwrap(),
        std::time::Duration::from_millis(1),
    ));
    let _ = spot.request_channel_callback(
        "svc-surface",
        Message::copy_from(b"payload").unwrap(),
        |_result| {},
        SendFlags::DONT_WAIT,
        std::time::Duration::from_millis(1),
    );
    let _ = spot.receive_subscription_event_with_flags(RecvFlags::DONT_WAIT);
    let _on_send_ready = Spot::on_send_ready::<fn()>;
    let _on_routed_receive = Spot::on_routed_receive::<fn(RoutingId, RoutingId, u64, Vec<Message>)>;
    let _on_dispatch_event = Spot::on_dispatch_event::<fn(SpotDispatchInfo)>;
    let _ = spot.drain_channel_reply_from(std::ptr::null_mut());
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
    let mut node = SpotNode::new(&ctx).unwrap();
    node.set_routing_id(&RoutingId::from_bytes(b"actor-node"))
        .unwrap();
    let spot = node.create_spot().unwrap();
    spot.set_routing_id(&RoutingId::from_bytes(b"actor-spot"))
        .unwrap();
    let stream = ctx.stream_socket().unwrap();
    let session_rid = RoutingId::from_bytes(b"actor-session");

    node.on_actor_admission(|_, _| ActorAdmissionResult::Accept)
        .unwrap();
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
    let _ = actor.recv_part_with_flags(RecvFlags::DONT_WAIT).unwrap();
    let _ = actor.send_bound_session_msg(
        Message::copy_from(b"payload").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let _ = actor.send_bound_session_packet(
        Message::copy_from(b"h").unwrap(),
        Message::copy_from(b"b").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let _ = actor.join_callback(
        &spot,
        Message::copy_from(b"join").unwrap(),
        |_| {},
        SendFlags::DONT_WAIT,
        std::time::Duration::from_millis(1),
    );
    let _ = spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT);
    let _ = stream.bind_actor(
        &node,
        &session_rid,
        &actor_ref,
        std::time::Duration::from_millis(1),
    );
    let _ = stream.send_bound_actor_part(
        &node,
        &session_rid,
        "actor-surface",
        Message::copy_from(b"payload").unwrap(),
        SendFlags::DONT_WAIT,
    );
    let _ = stream.unbind_actor(
        &node,
        &session_rid,
        "actor-surface",
        std::time::Duration::from_millis(1),
    );
    let _ = actor.leave(&spot);
    let _ = actor.close();
    let _ = node.create_remote_actor(
        &RoutingId::from_bytes(b"remote-node"),
        "remote-actor",
        Message::copy_from(b"create").unwrap(),
        std::time::Duration::from_millis(1),
    );
    let remote =
        SpotNode::remote_actor_ref(&RoutingId::from_bytes(b"remote-node"), "remote-actor").unwrap();
    let _ = node.destroy_remote_actor(&remote, std::time::Duration::from_millis(1));
    let _ = node.join_actor_callback(
        &remote,
        &RoutingId::from_bytes(b"actor-spot"),
        Message::copy_from(b"join").unwrap(),
        |_| {},
        SendFlags::DONT_WAIT,
        std::time::Duration::from_millis(1),
    );
    let _ = node.leave_actor(
        &remote,
        &RoutingId::from_bytes(b"actor-spot"),
        std::time::Duration::from_millis(1),
    );
    let _on_dispatch_event = Spot::on_dispatch_event::<fn(SpotDispatchInfo)>;
    let _ = _on_dispatch_event;
}

#[test]
fn typed_poller_surface_exists() {
    let ctx = Context::new().unwrap();
    let socket = ctx.pair_socket().unwrap();
    socket.bind("inproc://typed-poller-surface").unwrap();
    let poller = Poller::new().unwrap();
    poller.add_socket(&socket, POLLIN).unwrap();
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
    poller.add_socket(&server, POLLIN).unwrap();

    let wait_result = poller.wait(0).unwrap();
    if let Some(event) = wait_result {
        assert!(
            event.is_readable() || event.is_writable(),
            "poller event must carry readiness bits"
        );
    }
}
