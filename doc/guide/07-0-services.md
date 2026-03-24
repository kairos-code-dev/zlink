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
│  Public API Facade  (service_api · service_*_api)        │
│  validate + delegate → service-local access seam         │
├─────────────────────────────────────────────────────────┤
│  Service Access Layer                                    │
│  gateway_access · discovery_access · registry_access     │
│  spot_node_access · spot_subject_access                  │
│  service_public_api_guard (admission/close guard)        │
├─────────────────────────────────────────────────────────┤
│  Service Runtime                                         │
│  Gateway: facade·lifecycle·pool·socket·monitor·refresh   │
│  Discovery: bootstrap·state·update·uplink·registry_client│
│  SPOT: node·data_plane(forwarding·protocol)·pub·sub      │
├─────────────────────────────────────────────────────────┤
│  Discovery (service discovery) · Registry (service reg.) │
│  subscribe · heartbeat · broadcast SERVICE_LIST           │
├─────────────────────────────────────────────────────────┤
│              zlink Core (8 socket types + 6 transports)   │
└─────────────────────────────────────────────────────────┘
```

- **Public API Facade** is the C API entry point that validates handles and delegates to service-local access seams. It does not know concrete service details.
- **Service Access Layer** is the service-local seam provided by each service. `*_access.hpp` defines the contract between the API layer and service runtime.
- **Service Runtime** is the concrete implementation of each service. Gateway is modularized into facade/lifecycle/pool/socket/monitor/refresh; SPOT into node/data_plane(forwarding/protocol)/pub/sub.
- **Registry** manages service entries and periodically broadcasts the SERVICE_LIST.
- **Discovery** subscribes to the Registry and maintains a local cache of the service list.
- **Gateway** and **SPOT** automatically discover and connect to targets through Discovery.

## Service Terminology

| Service | Name Origin | One-Line Description |
|---------|-------------|---------------------|
| **Registry** | Service registry | Central store that registers and manages service entries |
| **Discovery** | Service discovery | Subscribes to the Registry and maintains a local cache of the service list |
| **Gateway** | Service gateway | Entry point to services + client-side load balancer. Unlike API Gateways (authentication, rate limiting, etc.), this is a different concept |
| **Gateway (Server)** | Service gateway | When bound, acts as the backend that receives and processes requests from remote Gateways |
| **SPOT** | Location (spot) transparent pub/sub | Object-level, location-transparent, topic-based publish/subscribe mesh |

## 3. Service Components

### 3.1 Service Discovery -- Foundation Infrastructure

A service registration/discovery system based on a Registry cluster. When a Gateway registers with the Registry, Discovery subscribes to it and manages the service list.

- Registry cluster HA (flooding synchronization)
- Heartbeat-based liveness checking
- Client-side service list caching
- Internal modules: `discovery_access` (API seam) · `discovery_bootstrap` · `discovery_state` · `discovery_update` · `discovery_uplink` · `discovery_registry_client`

See the [Service Discovery Guide](07-1-discovery.md) and the [Registry Guide](07-4-registry.md) for details.

### 3.2 Gateway -- Location-Transparent Request/Reply

Automatically discovers service peers based on Discovery and handles
load-balanced message delivery.

- **Thread-safe** -- a single Gateway handle allows concurrent `send` calls from multiple threads
- Round Robin / Weighted load balancing
- Automatic connect/disconnect (based on Discovery events)
- Internal modules: `gateway_access` (API seam) · `gateway_facade` · `gateway_lifecycle` · `gateway_pool` · `gateway_socket` · `gateway_monitor` · `gateway_refresh`

See the [Gateway Guide](07-2-gateway.md) for details.

### 3.3 SPOT -- Location-Transparent Topic PUB/SUB

Automatically constructs a PUB/SUB Mesh based on Discovery to publish/subscribe topic messages across the entire cluster.

- Topic-based publish/subscribe
- Pattern (wildcard) subscriptions
- Discovery-based automatic Mesh construction
- **Thread-safe** -- a single `spot` / `spot_node` handle allows concurrent operational API calls from multiple threads
- Internal modules: `spot_node_access` · `spot_subject_access` (API seam) · `spot_handle` · `spot_data_plane` (forwarding · protocol) · `spot_pub` · `spot_sub` (option · recv)

See the [SPOT Guide](07-3-spot.md) for details.

### 3.4 Registry -- Central Service Registry

Central store that registers and manages service entries. Handles Gateway/SPOT node registration, heartbeats, and topology broadcasts.

- Internal modules: `registry_access` (API seam) · `registry_query_access` (remote query seam)

See the [Registry Guide](07-4-registry.md) for details.

## 4. Service Access Layer Pattern

All services follow a common access layer pattern:

```
C API (zlink_gateway_send, etc.)
    → service_api.cpp (validate + delegate)
    → *_access.hpp (service-local seam)
    → service runtime (concrete implementation)
```

| Service | Access Seam | Role |
|---------|-------------|------|
| Gateway | `gateway_access_t` | lifecycle, bind/connect, send, option, monitor, TLS |
| Discovery | `discovery_access_t` | lifecycle, connect_registry, option, monitor |
| Registry | `registry_access_t` | lifecycle, bind, config, snapshot/query |
| Registry Query | `registry_query_access_t` | remote topology query |
| SPOT Node | `spot_node_access_t` | lifecycle, bind, peer connect, discovery attach |
| SPOT Subject | `spot_subject_access_t` | publish, subscribe, option, handler, monitor |

Each access seam integrates with `service_public_api_guard_t` to provide
callback mode tracking and lifecycle gates (destroy returning `EBUSY`/`ESHUTDOWN`).

This structure ensures the API layer does not know concrete service
implementations, and adding a new service only requires changes to
`api/service_*_api.cpp`, the corresponding `*_access` file, and the
service implementation files.

## 5. Relationships Between Services

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
