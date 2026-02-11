[English](07-0-services.md) | [한국어](07-0-services.ko.md)

# Service Layer Overview

## 1. What is the Service Layer

The zlink service layer is a set of **high-level distributed service features** built on top of the 7 socket types (PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM). It enables service registration, discovery, and location-transparent communication without directly managing socket-level connections and routing.

## 2. Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Application                           │
│         Gateway (req/rep)  ·  SPOT (pub/sub)             │
├─────────────────────────────────────────────────────────┤
│                  Discovery (service discovery)            │
│            subscribe · get · service_available            │
├─────────────────────────────────────────────────────────┤
│                  Registry (service registry)              │
│        register · heartbeat · broadcast SERVICE_LIST      │
├─────────────────────────────────────────────────────────┤
│              zlink Core (7 socket types + 6 transports)   │
└─────────────────────────────────────────────────────────┘
```

- **Registry** manages service entries and periodically broadcasts the SERVICE_LIST.
- **Discovery** subscribes to the Registry and maintains a local cache of the service list.
- **Gateway** and **SPOT** automatically discover and connect to targets through Discovery.

## Service Terminology

| Service | Name Origin | One-Line Description |
|---------|-------------|---------------------|
| **Registry** | Service registry | Central store that registers and manages service entries |
| **Discovery** | Service discovery | Subscribes to the Registry and maintains a local cache of the service list |
| **Gateway** | Service gateway | Entry point to services + client-side load balancer. Unlike API Gateways (authentication, rate limiting, etc.), this is a different concept |
| **Receiver** | Service receiver | Backend that receives and processes requests from the Gateway |
| **SPOT** | Location (spot) transparent pub/sub | Object-level, location-transparent, topic-based publish/subscribe mesh |

## 3. Service Components

### 3.1 Service Discovery -- Foundation Infrastructure

A service registration/discovery system based on a Registry cluster. When a Receiver registers with the Registry, Discovery subscribes to it and manages the service list.

- Registry cluster HA (flooding synchronization)
- Heartbeat-based liveness checking
- Client-side service list caching

See the [Service Discovery Guide](07-1-discovery.md) for details.

### 3.2 Gateway -- Location-Transparent Request/Reply

Automatically discovers service Receivers based on Discovery and handles load-balanced message delivery. Designed as send-only, it is **thread-safe** and allows concurrent sends from multiple threads.

- **Thread-safe** -- send-only design minimizes contention, enabling concurrent sends from multiple threads
- Round Robin / Weighted load balancing
- Automatic connect/disconnect (based on Discovery events)

See the [Gateway Guide](07-2-gateway.md) for details.

### 3.3 SPOT -- Location-Transparent Topic PUB/SUB

Automatically constructs a PUB/SUB Mesh based on Discovery to publish/subscribe topic messages across the entire cluster.

- Topic-based publish/subscribe
- Pattern (wildcard) subscriptions
- Discovery-based automatic Mesh construction

See the [SPOT Guide](07-3-spot.md) for details.

## 4. Relationships Between Services

```
                    ┌──────────┐
                    │ Registry │
                    │ (PUB+    │
                    │  ROUTER) │
                    └────┬─────┘
                         │ SERVICE_LIST broadcast
            ┌────────────┼────────────┐
            │            │            │
            v            v            v
      ┌──────────┐ ┌──────────┐ ┌──────────┐
      │Discovery │ │Discovery │ │Discovery │
      │(Gateway) │ │ (SPOT)   │ │ (direct) │
      └────┬─────┘ └────┬─────┘ └──────────┘
           │             │
           v             v
      ┌──────────┐ ┌──────────┐
      │ Gateway  │ │   SPOT   │
      │ (ROUTER) │ │(PUB+SUB) │
      └──────────┘ └──────────┘
```

- **Discovery is the foundation infrastructure**: Both Gateway and SPOT discover targets through Discovery.
- **Gateway** handles request/reply using the DEALER/ROUTER pattern.
- **SPOT** propagates topic messages using the PUB/SUB pattern.
- Gateway and SPOT operate independently and can share the same Registry cluster.

---
[← Monitoring](06-monitoring.md) | [Discovery →](07-1-discovery.md)
