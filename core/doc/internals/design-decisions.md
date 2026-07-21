[English](design-decisions.md) | [한국어](design-decisions.ko.md)

# Design Decision Records

This document records the rationale and alternative analyses for key design decisions in zlink.

---

## 1. Routing ID Policy

### 1.1 Socket Own Routing ID: 16B UUID

**Decision**: Auto-generated own routing_id for every socket is a 16B UUID (binary).

**Rationale**:
- A 16B random UUID makes collisions across nodes/processes effectively negligible
- Provides sufficient entropy for socket identification in monitoring/debugging
- Avoids cross-process collisions that short (4B / 5B) identifiers would risk

### 1.2 STREAM Peer/Client Routing ID: 4B uint32

**Decision**: STREAM per-connection peer routing_id is 4B uint32.

**Rationale**:
- The routing_id field inside msg_t is already uint32_t
- uint32 range is sufficient for connection counts
- Clear separation of purpose between own routing_id (identification) and peer routing_id (routing)

### 1.3 String Alias Retention

**Decision**: `zlink_set_routing_id()` / `zlink_get_routing_id()` and `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (set via `zlink_set_router_option()`) retain variable-length byte routing ids/aliases (usable as strings).

**Rationale**:
- String alias-based debugging/logging patterns are widely used with ROUTER
- Per-connection alias designation capability is needed
- routing_id length is not fixed

### 1.4 Default Routing ID Generation Location

**Decision**: Generated in `core/src/runtime/sockets/common/socket_base.cpp`.

**Rationale**:
- Core already has socket_id-based auto-generation behavior
- A higher runtime may apply an explicit routing-ID override through the public socket API.
- Keeps the dependency direction from higher runtimes into Core.

---

## 2. Monitoring Design

### 2.1 Polling Approach Selection

**Decision**: Monitoring exposes a direct receive surface by default, with an
optional one-way handler callback.

**Rationale**:
- The default recv model is safely processed in the user thread and avoids the
  I/O-thread callback deadlock risk
- An application that prefers callback-driven delivery can attach a handler
  (a one-way transition to callback-only mode)
- Can combine multi-socket monitoring via the poller

### 2.2 CONNECTION_READY Event

**Decision**: The send/receive-ready point is signaled by `CONNECTION_READY`.

**Rationale**:
- CONNECTED/ACCEPTED are transport-level events that can confuse users
- Clearly communicates the "actual send/receive ready" point to users
- Unifies meaning: handshake complete = connection ready

### 2.3 DISCONNECTED Reason Code

**Decision**: Add reason codes to the DISCONNECTED event (`UNKNOWN=0`,
`HANDSHAKE_FAILED=3`, `TRANSPORT_ERROR=4`, `CTX_TERM=5`).

**Rationale**:
- Need to distinguish context termination (`CTX_TERM`) from transport errors (`TRANSPORT_ERROR`) and handshake failures (`HANDSHAKE_FAILED`)
- Identifying disconnect causes is essential for operational debugging

### 2.4 Single Event Format

**Decision**: Monitoring events use a single format (no format versioning).

**Rationale**:
- No backward compatibility requirement (no compatibility policy)
- Simplifies implementation/usage by eliminating format branching logic

---
---

## 3. SPOT Design

### 3.1 PUB/SUB-Based Mesh

**Decision**: SPOT cluster is composed of a PUB/SUB mesh.

**Rationale**:
- PUB/SUB is natural for topic-based fanout
- Subscription filtering is more efficient than ROUTER-based approaches
- Explicit peer connections keep mesh formation under application control

### 3.2 No Re-publishing Policy

**Decision**: Remotely received messages are distributed locally only, not re-published.

**Rationale**:
- Re-publishing causes message loops/duplicates
- Full-mesh connectivity guarantees 1-hop delivery
- Saves network bandwidth

---

## 5. Naming: zlink / ZLINK

**Decision**: Public identifiers use zlink / ZLINK. There is no ZMTP/ZeroMQ wire compatibility.

**Rationale**:
- Establish a clear, independent identity
- The ZMP wire protocol is not ZMTP-compatible, so API aliases would imply false portability
- A single canonical name removes ambiguity in documentation and telemetry
