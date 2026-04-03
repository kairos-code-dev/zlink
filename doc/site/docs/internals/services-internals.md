
# Service Layer Internal Design

## 1. Overview

The zlink service layer provides two high-level services: Discovery and SPOT. This document covers the internal implementation details.

For SPOT, transport-security ownership is intentionally narrow: the
`SpotNode` owns TLS/WSS wiring for mesh/control sockets, while unified
`Spot` remains a borrowed data-plane facade only. The facade never owns
node lifecycle and is not itself a TLS configuration surface.

## 2. Registry Internal Implementation

### 2.1 Data Structures

```cpp
struct service_entry_t {
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint16_t service_role;
    uint64_t registered_at;
    uint64_t last_heartbeat;
    uint32_t weight;
};

struct registry_state_t {
    uint32_t registry_id;
    uint64_t list_seq;
    std::map<std::string, std::vector<service_entry_t>> services;
};
```

### 2.2 Registry State Machine

```
[INIT] → start() → [RUNNING] → stop() → [STOPPED]
```

### 2.3 SERVICE_LIST Broadcast Triggers
| Trigger | Description |
|--------|------|
| Registration | After successful service REGISTER |
| Deregistration | UNREGISTER or Heartbeat timeout |
| Periodic | 30 seconds (default, configurable) |

### 2.4 Cluster Synchronization
- Each Registry subscribes to other Registries' PUB via SUB
- Immediate propagation via flooding
- Duplicates/reversals ignored using registry_id + list_seq

## 3. Discovery Internal Implementation

### 3.1 State Machine (Per Service)
```
[EMPTY] → SERVICE_LIST(count>0) → [AVAILABLE]
[AVAILABLE] → SERVICE_LIST(count==0) → [UNAVAILABLE]
```

### 3.2 Service Types and Roles

Discovery tracks providers with a (service_type, service_role) pair:

```cpp
// Service types
static const uint16_t service_type_spot_node = 2;
static const uint16_t service_type_socket = 3;

// Service roles
enum service_role_t {
    service_role_invalid = 0,
    service_role_spot    = 2,  // fixed for spot type
    service_role_router  = 3,  // socket family
    service_role_dealer  = 4,  // socket family
    service_role_pub     = 5,  // socket family
    service_role_sub     = 6   // socket family
};
```

SPOT has a fixed role derived from its service type. Socket family
services require an explicit role matching the socket type. Role
matching rules for peer discovery:
- PUB ↔ SUB
- ROUTER ↔ ROUTER, ROUTER ↔ DEALER, DEALER ↔ DEALER
- SPOT ↔ SPOT

### 3.3 Discovery-Owned Service Execution

Discovery acts as the lifecycle owner for attached services. Each service
type registers its endpoint(s) through the `discovery_owned_service`
convenience API:

```cpp
namespace discovery_owned_service {
    int register_endpoint(discovery_t *, uint16_t service_type,
                          const char *endpoint, uint32_t weight,
                          std::string *resolved_endpoint_out,
                          const zlink_routing_id_t *routing_id = NULL,
                          uint16_t service_role = 0);
    int update_weight(discovery_t *, uint16_t service_type,
                      const char *endpoint, uint32_t weight,
                      uint16_t service_role = 0);
    int unregister_endpoint(discovery_t *, uint16_t service_type,
                            const char *endpoint,
                            uint16_t service_role = 0);
}
```

Discovery internally maintains a `_registered_services` map keyed by
`(service_type, service_role, service_name, endpoint)` and periodically
refreshes heartbeats for all registered services via
`refresh_registered_service_heartbeats()`.

### 3.4 Socket Discovery Attachment

`socket_discovery_attachment_t` integrates raw socket lifecycle with
Discovery. When a socket is attached:

1. Validates the socket type is supported (ROUTER/DEALER/PUB/SUB)
2. Derives the service role from the socket type
3. Registers the socket's bound endpoint via `discovery_owned_service`
4. Observes service list updates and refreshes peer connections
5. Reports topology state changes back to Discovery
6. Blocks manual connect/disconnect/unbind/close operations

### 3.5 Subscription Behavior
- Subscribes to all Registry PUB (no network-level filtering)
- subscribe/unsubscribe operate as internal filters

### 3.6 Duplicate/Reversal Handling
- Applies only the latest snapshot based on (registry_id, list_seq)
- Ignores earlier list_seq from the same registry_id

## 4. Message Protocol

### 4.1 Frame Structure
```
Frame 0: msgId (uint16_t)
Frame 1~N: Payload (variable)
```

### 4.2 Message Types
| msgId | Name | Direction |
|-------|------|------|
| 0x0001 | REGISTER | Service → Registry |
| 0x0002 | REGISTER_ACK | Registry → Service |
| 0x0003 | UNREGISTER | Service → Registry |
| 0x0004 | HEARTBEAT | Service → Registry |
| 0x0005 | SERVICE_LIST | Registry → Discovery |
| 0x0006 | REGISTRY_SYNC | Registry → Registry |
| 0x0007 | UPDATE_ATTRIBUTES | Service → Registry |
| 0x0008 | BOOTSTRAP_REQ | Discovery → Registry |
| 0x0009 | BOOTSTRAP_REP | Registry → Discovery |
| 0x000A | TOPOLOGY_REPORT | Discovery → Registry |
| 0x000B | TOPOLOGY_QUERY | Client → Registry |
| 0x000C | TOPOLOGY_REPLY | Registry → Client |
| 0x000D | UNREGISTER_ACK | Registry → Service |

### 4.3 SERVICE_LIST Format
```
Frame 0: msgId = 0x0005
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)
Frame 3: service_count (uint32_t)
Frame 4~N: Service entries (repeated service_count times)
  - service_type (uint16_t)
  - service_name (string)
  - provider_count (uint32_t)
  - provider entries (repeated provider_count times):
      service_role (uint16_t), endpoint (string),
      routing_id, weight (uint32_t)
```

## 5. SPOT Internal Implementation

### 5.1 Structure
- `spot_node_t` -- Network control (owns PUB/SUB sockets, mesh management, worker thread)
- `spot_pub_t` -- Publish handle (delegates to spot_node_t's publish, tag-based validity check)
- `spot_sub_t` -- Subscribe/receive handle (internal queue, pattern matching, condition variable-based blocking recv)

### 5.2 Concurrency Model
- Publishing: Performed directly on the caller's thread, serialized by `_publish_sync` mutex (thread-safe)
- Receiving: Worker thread receives from SUB socket → distributes to spot_sub_t internal queues
- Lock ordering: `_sync` → `_publish_sync` (deadlock prevention)
- Direct publishing without async queue (no message buffering on the publish path)

### 5.3 Subscription Aggregation
- Refcount-based SUB filter management
- Duplicate subscriptions to the same topic increment the refcount
- Per-spot_sub_t subscription set management (separate for exact topics and patterns)

### 5.4 Delivery Policy
- Local publish (spot_pub) → local spot_sub distribution + PUB output (remote propagation)
- Remote receive (SUB) → local spot_sub distribution only (no re-publishing, loop prevention)

### 5.5 Raw Socket Policy
- `spot_pub_t`: Does not expose raw PUB socket (prevents thread-safety bypass)
- `spot_sub_t`: Does not expose raw SUB socket; consumption is via callback/recv API only

### 5.6 Discovery Type Segmentation
- Separates spot_node/socket_family via service_type field
  - `service_type_spot_node` (2), `service_type_socket` (3)
- Socket family services additionally carry a `service_role` field
  (ROUTER=3, DEALER=4, PUB=5, SUB=6) for role-based peer matching
- Role matching is enforced by `service_roles_match()` -- PUB pairs with SUB,
  ROUTER/DEALER pair with each other
