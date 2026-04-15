[English](README.md) | [한국어](README.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md)

# Common Service Rules

This document explains the shared rules used by the service-related
specifications. SPOT, Registry, and Discovery expose separate public APIs, but
they share the same address interpretation rules. In particular, this document
defines what `node_rid` and `spot_rid` mean in SPOT direct routed messaging and
how a caller can start from only `spot_rid` and obtain the destination
`node_rid`.

## Document Roles

- [spot.md](spot.md): SPOT publish/subscribe and direct send/request/reply
  function contracts
- [registry.md](registry.md): the final rule for which `SpotNode` currently owns
  a `spot_rid`, plus register/unregister, overwrite, and expiration rules
- [discovery.md](discovery.md): how cached lookup works close to the caller,
  how it refreshes, and what scale assumptions it must support

This split keeps the SPOT document focused on message send/receive contracts,
while address-management details live in Registry and Discovery.

## Common Address Rules

In these service documents, `routing_id` does not mean a network endpoint such
as an IP address, port, or endpoint string. In the SPOT direct routed path,
both `node_rid` and `spot_rid` are **logical addresses**.

- The `SpotNode` routing identity identifies the logical node owner.
- The `Spot` routing identity identifies the logical spot owner.
- Both may be assigned through `zlink_set_routing_id()`.
- Neither directly encodes an IP address, port, or machine location.

`service_name` describes discovery membership and mesh relationships, and it is
also the **service scope** used by managed auto-connect and logical lookup.

- Discovery-managed auto-connect only operates inside one `service_name`.
- `zlink_discovery_resolve_spot()` resolves `spot_rid` only inside the current
  Discovery service view.
- In a managed deployment, a logical SPOT address is therefore interpreted as
  the pair `(service_name, spot_rid)`.
- The same `spot_rid` may exist at the same time in different `service_name`
  scopes.

## Two Addressing Modes

SPOT direct routed messaging is easiest to understand as two modes.

- direct pair mode:
  the caller already knows both `dest_node_rid` and `dest_spot_rid` and passes
  them directly to the SPOT send/request functions
- logical `spot_rid` mode:
  the caller starts from only `spot_rid`, asks the current Discovery service
  view which `SpotNode` currently owns it inside that `service_name`, then
  passes that result to the same SPOT send/request functions

The second mode is not a separate wire path. The final submit step always uses
the `dest_node_rid + dest_spot_rid` pair. Internally, the flow is:
resolve `spot_rid -> owner_node_rid`, then call the routed function.

This spec does not require extra `send/request/reply` overloads for that flow.
Instead, the caller first resolves the destination node with the following API
and then calls the SPOT routed submit functions.

```c
zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

If this call succeeds, the caller pairs `owner_node_rid_out` with the original
`spot_rid` and uses `zlink_spot_send_spot()`,
`zlink_spot_request_spot()`, or the equivalent router-side functions. That
resolved result is valid only inside the current Discovery `service_name`
scope. Replies are different: they must use the concrete source address
delivered with the incoming request and must not perform a fresh lookup.

## Manual and Managed Configurations

- manual deployment: uses only manual peer connections without
  Discovery/Registry ownership information. Only the base routed contract can
  be used directly.
- managed deployment: Registry manages authoritative ownership for
  `(service_name, spot_rid)` and Discovery provides resolve cache for that
  current service view. This deployment can support the usage pattern that
  starts from logical `spot_rid`.

## Common Auto-Connect Rules

Any service attached to Discovery uses the current Discovery `service_name` as
its auto-connect boundary. Managed auto-connect never crosses that service
boundary.

- Only peers from the same `service_name` are auto-discovered.
- Manual connect/disconnect and Discovery-managed auto-connect must not be mixed.
- SPOT Node mesh and raw socket family auto-connect both share this service
  boundary.
- The role-specific raw socket auto-connect rules, including DEALER policy, are
  defined in [discovery.md](discovery.md).

## Reading Order

1. For the SPOT API contract, read [spot.md](spot.md)
2. For `spot_rid -> owner_node_rid` ownership rules, read
   [registry.md](registry.md)
3. For ownership cache and resolve flow, read [discovery.md](discovery.md)
