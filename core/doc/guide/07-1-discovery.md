[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

<!-- zlink-nav:start -->
[← Services](07-0-services.md) | [SPOT →](07-3-spot.md)
<!-- zlink-nav:end -->

# Service Discovery

> **Removed public API.**
> The former C discovery service API was removed from the public contract in
> core 8.4.3. This page is kept only so older links resolve to the removal
> notice.

Applications should not create or attach public discovery handles. Current
public service APIs are documented in the SPOT and socket guides, and the
exact C contract is defined by `core/include/zlink.h`.

Framework-level automatic location lookup and routing moved to the location
runtime/store. The formal contract lives in
`framework/doc/framework/common/spec/location-runtime.ko.md`.
