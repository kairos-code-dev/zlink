[English](services-internals.md) | [한국어](services-internals.ko.md)

# Service Layer Internal Design

## 1. Overview

The zlink service layer provides three high-level services: Discovery, Gateway, and SPOT. This document covers the internal implementation details.

## 2. Registry Internal Implementation

### 2.1 Data Structures

```cpp
struct service_entry_t {
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
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
| Registration | After successful Receiver REGISTER |
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

### 3.2 Subscription Behavior
- Subscribes to all Registry PUB (no network-level filtering)
- subscribe/unsubscribe operate as internal filters
- Only restricts Gateway notification/query targets

### 3.3 Duplicate/Reversal Handling
- Applies only the latest snapshot based on (registry_id, list_seq)
- Ignores earlier list_seq from the same registry_id

## 4. Gateway Internal Implementation

### 4.1 State Machine (Per Service)
```
[NO_POOL] → RECEIVER_ADDED → [POOL_READY]
[POOL_READY] → last RECEIVER_REMOVED → [NO_POOL]
```

### 4.2 Service Pool Structure
- One ROUTER socket per service
- Connects to all Receiver endpoints
- Target designation via routing_id

### 4.3 Request-Response Mapping
- request_id (uint64_t) auto-generated
- Stored in pending_requests map
- Mapped by request_id upon response receipt

## 5. Receiver Internal Implementation

> **Note**: The public C API is named `zlink_receiver_*`, but the internal C++ implementation class
> is maintained as `provider_t` (`core/src/services/gateway/receiver.hpp`).

### 5.1 State Machine
```
[INIT] → bind() → [BOUND] → connect_registry() → [CONNECTED]
→ register() → [REGISTERED] → heartbeat → [REGISTERED]
→ unregister()/timeout → [UNREGISTERED]
```

### 5.2 Receiver Identification
- Primary key: service_name + advertise_endpoint
- Re-registration with the same key updates routing_id/weight/heartbeat

### 5.3 Registry Failover
- Single active Registry + re-registration on failure
- Immediate retry, exponential backoff on consecutive failures (200ms~5s, +/-20% jitter)
- Round-robin Registry traversal

## 6. Message Protocol

### 6.1 Frame Structure
```
Frame 0: msgId (uint16_t)
Frame 1~N: Payload (variable)
```

### 6.2 Message Types
| msgId | Name | Direction |
|-------|------|------|
| 0x0001 | REGISTER | Receiver → Registry |
| 0x0002 | REGISTER_ACK | Registry → Receiver |
| 0x0003 | UNREGISTER | Receiver → Registry |
| 0x0004 | HEARTBEAT | Receiver → Registry |
| 0x0005 | SERVICE_LIST | Registry → Discovery |
| 0x0006 | REGISTRY_SYNC | Registry → Registry |
| 0x0007 | UPDATE_WEIGHT | Receiver → Registry |

### 6.3 SERVICE_LIST Format
```
Frame 0: msgId = 0x0005
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)
Frame 3: service_count (uint32_t)
Frame 4~N: Service entries
  - service_name (string)
  - receiver_count (uint32_t)
  - receiver entries: endpoint, routing_id, weight
```

### 6.4 Business Messages (Gateway <-> Receiver)
```
Frame 0: routing_id
Frame 1: request_id (uint64_t)
Frame 2: msgId (uint16_t)
Frame 3~N: Payload
```

## 7. SPOT Internal Implementation

### 7.1 Structure
- `spot_node_t` -- Network control (owns PUB/SUB sockets, mesh management, worker thread)
- `spot_pub_t` -- Publish handle (delegates to spot_node_t's publish, tag-based validity check)
- `spot_sub_t` -- Subscribe/receive handle (internal queue, pattern matching, condition variable-based blocking recv)

### 7.2 Concurrency Model
- Publishing: Performed directly on the caller's thread, serialized by `_pub_sync` mutex (thread-safe)
- Receiving: Worker thread receives from SUB socket → distributes to spot_sub_t internal queues
- Lock ordering: `_sync` → `_pub_sync` (deadlock prevention)
- Direct publishing without async queue (no message buffering on the publish path)

### 7.3 Subscription Aggregation
- Refcount-based SUB filter management
- Duplicate subscriptions to the same topic increment the refcount
- Per-spot_sub_t subscription set management (separate for exact topics and patterns)

### 7.4 Delivery Policy
- Local publish (spot_pub) → local spot_sub distribution + PUB output (remote propagation)
- Remote receive (SUB) → local spot_sub distribution only (no re-publishing, loop prevention)

### 7.5 Raw Socket Policy
- `spot_pub_t`: Does not expose raw PUB socket (prevents thread-safety bypass)
- `spot_sub_t`: Exposes raw SUB socket (`zlink_spot_sub_socket()`, for diagnostics/advanced use)

### 7.4 Discovery Type Segmentation
- Separates gateway_receiver/spot_node via service_type field
  - `ZLINK_SERVICE_TYPE_GATEWAY` (1), `ZLINK_SERVICE_TYPE_SPOT` (2)
- Details: [plan/type-segmentation.md](../plan/type-segmentation.md)
