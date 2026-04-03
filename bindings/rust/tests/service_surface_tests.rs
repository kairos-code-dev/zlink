//! Service surface tests - verify service/query/introspection APIs exist.

use zlink::*;

fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    port
}

#[test]
fn discovery_service_monitor_and_member_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, ServiceType::Spot, "svc").unwrap();
    let _ = discovery
        .monitor_open(SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP)
        .unwrap();
    let _ = discovery.member_peers().unwrap();
}

#[test]
fn spot_node_snapshot_and_monitor_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let endpoint = format!("tcp://127.0.0.1:{}", reserve_tcp_port());
    node.bind(&endpoint).unwrap();
    let _monitor_open = SpotNode::monitor_open;
    let _ = node.status_snapshot().unwrap();
    let _ = node.peers_snapshot().unwrap();
    let _ = node.subjects_snapshot().unwrap();
}

#[test]
fn spot_callback_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let mut spot = Spot::new(&node).unwrap();
    spot.on_subscribe(|_topic_message| {}).unwrap();
    spot.on_send_ready(|| {}).unwrap();
    let _ = spot.monitor_open(SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED);
}

#[test]
fn service_close_surfaces_exist() {
    let ctx = Context::new().unwrap();
    let mut discovery = Discovery::new(&ctx, ServiceType::Spot, "svc-close").unwrap();
    let mut node = SpotNode::new(&ctx).unwrap();
    let mut spot = Spot::new(&node).unwrap();
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
    let discovery = Discovery::new(&ctx, ServiceType::Socket, "svc-attach").unwrap();
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
    let mut discovery = Discovery::new(&ctx, ServiceType::Socket, "svc-attach-close").unwrap();
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
}

#[test]
fn registry_query_client_surface_exists() {
    let ctx = Context::new().unwrap();
    let _client = RegistryQueryClient::new(&ctx).unwrap();
}

#[test]
fn typed_poller_surface_exists() {
    let ctx = Context::new().unwrap();
    let socket = ctx.pair_socket().unwrap();
    socket.bind("inproc://typed-poller-surface").unwrap();
    let poller = Poller::new().unwrap();
    poller.add_socket(&socket, 0, POLLIN).unwrap();
    poller.modify_socket(&socket, POLLOUT).unwrap();
    poller.remove_socket(&socket).unwrap();
}
