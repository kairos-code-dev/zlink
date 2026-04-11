[English](registry-internals.md) | [한국어](registry-internals.ko.md)

# Registry Service Internal Architecture

## 1. Component Overview

```mermaid
flowchart TB
    subgraph PublicAPI["Public API"]
        reg_new["zlink_registry_new()"]
        reg_bind["zlink_registry_bind()"]
        reg_add_peer["zlink_registry_add_peer()"]
    end

    subgraph RegistryCore["registry_t"]
        router["ROUTER socket<br/>client requests"]
        xpub["XPUB socket<br/>SERVICE_LIST broadcast"]
        peer_sub["SUB socket<br/>peer Registry sync"]
        services["service_map_t<br/>provider storage"]
        peer_seq["peer_seq map<br/>registry_id → list_seq"]
        list_seq["list_seq counter"]
        control_task["control_task<br/>(periodic tick)"]
    end

    subgraph Clients["Discovery Clients"]
        disc1["Discovery 1<br/>DEALER + SUB"]
        disc2["Discovery 2<br/>DEALER + SUB"]
    end

    subgraph Peers["Peer Registries"]
        peer1["Registry 2<br/>XPUB"]
        peer2["Registry 3<br/>XPUB"]
    end

    reg_new --> RegistryCore
    disc1 -->|DEALER| router
    disc2 -->|DEALER| router
    xpub -->|SERVICE_LIST| disc1
    xpub -->|SERVICE_LIST| disc2
    peer1 -->|SERVICE_LIST| peer_sub
    peer2 -->|SERVICE_LIST| peer_sub
    peer_sub --> services
```

## 2. Socket Types

| Socket | Type | Endpoint | Purpose |
|--------|------|----------|---------|
| `_router_socket` | ROUTER | configured via `bind()` | Handle REGISTER, HEARTBEAT, BOOTSTRAP, TOPOLOGY requests |
| `_pub_socket` | XPUB | configured via `bind()` | Broadcast SERVICE_LIST to all Discovery SUBs |
| `_peer_sub_socket` | SUB | connects to peer PUBs | Receive SERVICE_LIST from peer Registries (cluster sync) |

XPUB is used (not PUB) to detect subscription events and trigger
immediate SERVICE_LIST broadcast to new subscribers.

## 3. Data Structures

```cpp
struct service_key_t {
    uint16_t service_type;       // spot_node(2), socket(3)
    std::string service_name;
};

struct provider_entry_t {
    uint16_t service_role;       // spot(2), router(3), dealer(4), pub(5), sub(6)
    std::string endpoint;
    zlink_routing_id_t routing_id;
    int64_t value;
    std::vector<uint8_t> metadata;
    uint64_t registered_at;
    uint64_t last_heartbeat;
    uint32_t source_registry;    // which registry provided this entry
};

// Storage: service_key → { provider_key → provider_entry }
```

## 4. Service Registration Sequence

```mermaid
sequenceDiagram
    participant Disc as Discovery DEALER
    participant Router as Registry ROUTER
    participant Store as service_map
    participant Pub as Registry XPUB
    participant Subs as Discovery SUBs

    Disc->>Router: REGISTER (0x0001)<br/>[service_type, name, role,<br/>endpoint, routing_id, value]
    Router->>Store: insert provider_entry
    Router->>Router: increment list_seq
    Router->>Disc: REGISTER_ACK (0x0002)<br/>[status=0, resolved_endpoint]
    Note over Pub: list_seq changed → broadcast
    Pub->>Subs: SERVICE_LIST (0x0005)<br/>[registry_id, list_seq, entries...]
```

## 5. Heartbeat Tracking

```mermaid
sequenceDiagram
    participant Disc as Discovery DEALER
    participant Router as Registry ROUTER
    participant Store as service_map

    loop Every heartbeat_interval (default 5000ms)
        Disc->>Router: HEARTBEAT (0x0004)<br/>[service_type, role, name, endpoint]
        Router->>Store: update last_heartbeat timestamp
    end

    Note over Router: control_task tick
    Router->>Store: remove_expired()
    Note over Store: remove entries where<br/>now - last_heartbeat > timeout (15000ms)
    Store->>Router: if removed → increment list_seq
    Router->>Router: trigger SERVICE_LIST broadcast
```

### Heartbeat Configuration

| Parameter | Default | Description |
|-----------|---------|-------------|
| `heartbeat_interval_ms` | 5000ms | Service heartbeat send interval |
| `heartbeat_timeout_ms` | 15000ms | Expiry threshold (3x interval) |
| `broadcast_interval_ms` | 30000ms | Periodic SERVICE_LIST broadcast |

## 6. Cluster Synchronization (Flooding)

```mermaid
sequenceDiagram
    participant R1 as Registry 1 (XPUB)
    participant R2 as Registry 2 (SUB + XPUB)
    participant R3 as Registry 3 (SUB)

    Note over R1: Service registered locally
    R1->>R2: SERVICE_LIST [registry_id=1, list_seq=5]
    R2->>R2: peer_seq[1] < 5 → accept
    R2->>R2: merge entries from Registry 1
    R2->>R2: increment own list_seq
    R2->>R3: SERVICE_LIST [registry_id=2, list_seq=12]<br/>(includes Registry 1 entries)
    R3->>R3: peer_seq[2] < 12 → accept

    Note over R1,R3: All registries converge
```

### Flooding Rules

| Rule | Description |
|------|-------------|
| Self-filter | `peer_registry_id == local_registry_id` → ignore |
| Sequence check | `list_seq <= peer_seq[id]` → ignore (already seen) |
| Merge | Clear old entries from `source_registry`, insert new |
| Rebroadcast | Increment own `list_seq`, broadcast to all subscribers |
| Loop prevention | Each entry carries `source_registry` to track origin |

### Cluster Topology

```mermaid
flowchart LR
    subgraph R1["Registry 1"]
        r1_pub["XPUB"]
        r1_sub["SUB"]
        r1_router["ROUTER"]
    end
    subgraph R2["Registry 2"]
        r2_pub["XPUB"]
        r2_sub["SUB"]
        r2_router["ROUTER"]
    end
    subgraph R3["Registry 3"]
        r3_pub["XPUB"]
        r3_sub["SUB"]
        r3_router["ROUTER"]
    end

    r1_pub -->|SERVICE_LIST| r2_sub
    r1_pub -->|SERVICE_LIST| r3_sub
    r2_pub -->|SERVICE_LIST| r1_sub
    r2_pub -->|SERVICE_LIST| r3_sub
    r3_pub -->|SERVICE_LIST| r1_sub
    r3_pub -->|SERVICE_LIST| r2_sub
```

Recommended cluster size: **3 nodes** (full mesh interconnection).

## 7. Topology Query

```mermaid
sequenceDiagram
    participant Client as Query Client
    participant Router as Registry ROUTER
    participant Store as service_map

    Client->>Router: TOPOLOGY_QUERY (0x000B)<br/>[filter: service_kind, role,<br/>state, name, routing_id]
    Router->>Store: filter and collect entries
    Store->>Router: matching topology_entry_t list
    Router->>Client: TOPOLOGY_REPLY (0x000C)<br/>[entry_count, entries...]
```

### Query Filters

| Filter | Type | Description |
|--------|------|-------------|
| service_kind | uint16_t | SPOT (2) or Socket (3) |
| service_role | uint16_t | router/dealer/pub/sub/spot |
| state | uint8_t | Service state filter |
| service_name | string | Exact name match |
| routing_id | bytes | Specific provider match |

Results are sorted by: `service_kind → service_name → service_role → endpoint → routing_id`

## 8. Control Task Cycle

```mermaid
flowchart TD
    tick["control_task tick"] --> ensure["ensure XPUB + ROUTER<br/>sockets bound"]
    ensure --> drain_router["drain ROUTER socket<br/>(all pending requests)"]
    drain_router --> drain_xpub["drain XPUB socket<br/>(subscription events)"]
    drain_xpub --> drain_peer["drain peer SUB socket<br/>(peer SERVICE_LISTs)"]
    drain_peer --> expire["remove_expired()<br/>(heartbeat timeout check)"]
    expire --> decide["broadcast needed?"]
    decide -->|"list_seq changed<br/>or periodic timer"| broadcast["send SERVICE_LIST<br/>via XPUB"]
    decide -->|"no change"| schedule["schedule next tick"]
    broadcast --> schedule
```

## 9. Bootstrap Mechanism

```mermaid
sequenceDiagram
    participant Disc as Discovery DEALER
    participant Router as Registry ROUTER

    Disc->>Router: BOOTSTRAP_REQ (0x0008)<br/>[routing_id]
    Router->>Router: lookup bootstrap config
    Router->>Disc: BOOTSTRAP_REP (0x0009)<br/>[registry_id,<br/>heartbeat_interval_ms,<br/>pub_endpoint,<br/>uplink_endpoint]

    Note over Disc: Now knows where to:<br/>- subscribe (pub_endpoint)<br/>- send heartbeats (uplink_endpoint)
```

The bootstrap reply provides the Discovery client with all the
information needed to establish ongoing communication:
- `registry_id`: unique identifier for duplicate detection
- `heartbeat_interval_ms`: how often to send heartbeats
- `pub_endpoint`: where to subscribe for SERVICE_LIST
- `uplink_endpoint`: where to send REGISTER/HEARTBEAT/TOPOLOGY
