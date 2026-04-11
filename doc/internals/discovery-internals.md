[English](discovery-internals.md) | [한국어](discovery-internals.ko.md)

# Discovery Service Internal Architecture

## 1. Component Overview

```mermaid
flowchart TB
    subgraph PublicAPI["Public API"]
        disc_new["zlink_discovery_new()"]
        disc_connect["zlink_discovery_connect_registry()"]
        disc_destroy["zlink_discovery_destroy()"]
    end

    subgraph DiscoveryCore["discovery_t"]
        bootstrap_rt["bootstrap_runtime<br/>DEALER → Registry ROUTER"]
        uplink_rt["uplink_runtime<br/>heartbeat, topology report"]
        sub_socket["SUB socket<br/>SERVICE_LIST reception"]
        service_state["service_state<br/>provider snapshots"]
        observers["observer list<br/>(attachments)"]
        registered["_registered_services<br/>(service_type, role, name, endpoint)"]
        control_task["control_task (periodic)"]
    end

    subgraph Attachments["Service Attachments"]
        spot_attach["SpotNode attachment"]
        socket_attach["socket_discovery_attachment_t<br/>(ROUTER/DEALER/PUB/SUB)"]
    end

    subgraph Registry["Registry"]
        reg_router["ROUTER socket"]
        reg_pub["XPUB socket"]
    end

    disc_new --> DiscoveryCore
    disc_connect --> bootstrap_rt
    bootstrap_rt -->|DEALER| reg_router
    uplink_rt -->|DEALER| reg_router
    reg_pub -->|SERVICE_LIST| sub_socket
    sub_socket --> service_state
    service_state --> observers
    observers --> spot_attach
    observers --> socket_attach
    control_task --> uplink_rt
```

## 2. Socket Types and Endpoints

| Socket | Type | Target | Purpose |
|--------|------|--------|---------|
| Bootstrap DEALER | DEALER | Registry ROUTER | Initial registration, bootstrap request |
| Topology Report DEALER | DEALER | Registry uplink | Topology state reports |
| Control DEALER | DEALER | Registry uplink | Heartbeat, attribute updates |
| SERVICE_LIST SUB | SUB | Registry XPUB | Receive service list broadcasts |

All DEALER sockets are created on demand and destroyed on shutdown.

## 3. Lifecycle State Machine

```mermaid
stateDiagram-v2
    [*] --> CREATED: discovery_new()
    CREATED --> BOOTSTRAPPING: connect_registry()
    BOOTSTRAPPING --> BOOTSTRAPPED: bootstrap_rep received
    BOOTSTRAPPED --> UPLINKED: uplink DEALERs created
    UPLINKED --> SUBSCRIBED: SUB connected to Registry PUB
    SUBSCRIBED --> RUNNING: SERVICE_LIST received

    RUNNING --> RUNNING: periodic tick<br/>(heartbeat, topology refresh)
    RUNNING --> SHUTDOWN: destroy()
    SHUTDOWN --> [*]

    BOOTSTRAPPING --> BOOTSTRAPPING: retry (timeout 2000ms)
```

## 4. Bootstrap Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Disc as Discovery
    participant DEALER as Bootstrap DEALER
    participant REG as Registry ROUTER

    App->>Disc: connect_registry("tcp://registry:5551")
    Disc->>Disc: add to pending_bootstrap_endpoints
    Note over Disc: control_task tick

    Disc->>DEALER: ensure_bootstrap_dealer()
    DEALER->>REG: BOOTSTRAP_REQ (0x0008)<br/>[routing_id]
    REG->>DEALER: BOOTSTRAP_REP (0x0009)<br/>[registry_id, heartbeat_interval,<br/>pub_endpoint, uplink_endpoint]

    Disc->>Disc: store registry config
    Disc->>Disc: create uplink DEALERs
    Disc->>Disc: create SUB socket
    Disc->>Disc: connect SUB to pub_endpoint
    Note over Disc: bootstrap complete
```

## 5. Service Registration Flow

```mermaid
sequenceDiagram
    participant Service as Service (SPOT/Socket)
    participant Disc as Discovery
    participant DEALER as Control DEALER
    participant REG as Registry ROUTER

    Service->>Disc: register_endpoint(type, endpoint, weight)
    Disc->>Disc: store in _registered_services
    Disc->>DEALER: REGISTER (0x0001)<br/>[service_type, service_name,<br/>service_role, endpoint, routing_id]
    REG->>DEALER: REGISTER_ACK (0x0002)<br/>[status, resolved_endpoint]

    loop Every heartbeat_interval
        Disc->>DEALER: HEARTBEAT (0x0004)<br/>[service_type, service_role,<br/>service_name, endpoint]
    end
```

## 6. SERVICE_LIST Update Flow

```mermaid
sequenceDiagram
    participant REG as Registry XPUB
    participant SUB as Discovery SUB
    participant State as service_state
    participant Observer as Attachment Observer

    REG->>SUB: SERVICE_LIST (0x0005)<br/>[registry_id, list_seq,<br/>service_count, entries...]
    SUB->>State: parse and filter by service_type/name
    State->>State: apply_provider_snapshot()
    State->>State: check if providers changed

    alt Providers changed
        State->>Observer: on_service_update(snapshot)
        Observer->>Observer: refresh_peers()
        Note over Observer: connect new peers,<br/>disconnect removed peers
    end
```

### SERVICE_LIST Frame Format

```text
Frame 0: msg_id = 0x0005
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)
Frame 3: service_count (uint32_t)
Frame 4~N: Service entries (repeated):
  - service_type (uint16_t)
  - service_name (string)
  - provider_count (uint32_t)
  - Per provider:
      service_role (uint16_t)
      endpoint (string)
      routing_id (variable)
      value (int64_t)
      metadata (variable)
```

## 7. Socket Discovery Attachment

`socket_discovery_attachment_t` integrates raw sockets with Discovery
for automatic peer management.

```mermaid
sequenceDiagram
    participant Socket as Raw Socket (ROUTER)
    participant Attach as socket_discovery_attachment_t
    participant Disc as Discovery
    participant REG as Registry

    Socket->>Socket: zlink_bind("tcp://*:5555")
    Socket->>Attach: attach(socket, discovery)
    Attach->>Attach: derive service_role from socket_type<br/>(ROUTER→3, DEALER→4, PUB→5, SUB→6)
    Attach->>Disc: register_endpoint(service_type_socket,<br/>endpoint, role)
    Disc->>REG: REGISTER

    Attach->>Disc: add_observer(self)
    Note over Attach: now receives SERVICE_LIST updates

    Disc->>Attach: on_service_update(providers)
    Attach->>Attach: filter by service_roles_match()
    Attach->>Socket: zlink_connect(new_peer_endpoint)
    Attach->>Socket: zlink_disconnect(removed_peer_endpoint)
```

### Role Matching Rules

| Socket Type | Service Role | Matches With |
|-------------|-------------|-------------|
| ROUTER | 3 | ROUTER (3), DEALER (4) |
| DEALER | 4 | ROUTER (3), DEALER (4) |
| PUB | 5 | SUB (6) |
| SUB | 6 | PUB (5) |
| SPOT | 2 | SPOT (2) |

### Attachment Constraints

- Only one bound endpoint allowed per socket
- Manual `connect`/`disconnect`/`unbind` blocked (Discovery-exclusive)
- Peer connections fully managed by Discovery
- Shutdown cascades from `discovery_destroy()` to all attachments

## 8. SPOT Node Attachment

SpotNode uses the same observer pattern but with:
- `service_type = service_type_spot_node (2)`
- `service_role = service_role_spot (2)` (fixed)
- Peer connections target other SpotNodes in the mesh

```mermaid
sequenceDiagram
    participant Node as SpotNode
    participant Disc as Discovery
    participant REG as Registry

    Node->>Node: zlink_spot_node_bind("tcp://*:9000")
    Node->>Disc: attach_discovery(discovery)
    Disc->>REG: REGISTER(type=spot_node, endpoint)

    Disc->>Node: on_service_update(spot_node providers)
    Node->>Node: connect_peer(new_spot_node_endpoint)
    Note over Node: mesh auto-constructed
```

## 9. Control Task Cycle

```mermaid
flowchart TD
    tick["control_task tick"] --> bootstrap["check pending<br/>bootstrap endpoints"]
    bootstrap --> sub["ensure SUB socket<br/>connected to PUB"]
    sub --> poll["poll SUB for<br/>SERVICE_LIST"]
    poll --> parse["parse and apply<br/>service updates"]
    parse --> heartbeat["refresh registered<br/>service heartbeats"]
    heartbeat --> topology["flush topology<br/>reports"]
    topology --> notify["notify observers<br/>if changed"]
```

## 10. Message Protocol

| msg_id | Name | Direction | Purpose |
|--------|------|-----------|---------|
| 0x0001 | REGISTER | DEALER→ROUTER | Register service |
| 0x0002 | REGISTER_ACK | ROUTER→DEALER | Registration confirmation |
| 0x0003 | UNREGISTER | DEALER→ROUTER | Remove service |
| 0x000D | UNREGISTER_ACK | ROUTER→DEALER | Removal confirmation |
| 0x0004 | HEARTBEAT | DEALER→ROUTER | Keep-alive |
| 0x0005 | SERVICE_LIST | PUB→SUB | Service broadcast |
| 0x0007 | UPDATE_ATTRIBUTES | DEALER→ROUTER | Update service metadata |
| 0x0008 | BOOTSTRAP_REQ | DEALER→ROUTER | Initial config request |
| 0x0009 | BOOTSTRAP_REP | ROUTER→DEALER | Config response |
| 0x000A | TOPOLOGY_REPORT | DEALER→ROUTER | Topology state report |
| 0x000B | TOPOLOGY_QUERY | DEALER→ROUTER | Query service topology |
| 0x000C | TOPOLOGY_REPLY | ROUTER→DEALER | Topology query response |
