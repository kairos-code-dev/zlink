[English](07-4-registry.md) | [한국어](07-4-registry.ko.md)

<!-- zlink-nav:start -->
[← SPOT Actor](07-4-actor.md) | [Routing ID →](08-routing-id.md)
<!-- zlink-nav:end -->

# Registry

> **Removed public API.**
> The former C registry service API was removed from the public contract in
> core 8.4.3. This page is kept only so older links resolve to the removal
> notice.

Applications should not create public registry handles or query registry
topology through the core C API. The current public C contract is
`core/include/zlink.h`.

Framework-level location lookup, topology, and status moved to the location
runtime query. The formal contract lives in
`framework/doc/framework/common/spec/location-runtime.ko.md`.
