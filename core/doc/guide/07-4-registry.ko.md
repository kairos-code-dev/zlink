[English](07-4-registry.md) | [한국어](07-4-registry.ko.md)

<!-- zlink-nav:start -->
[← SPOT Actor](07-4-actor.ko.md) | [Routing ID →](08-routing-id.ko.md)
<!-- zlink-nav:end -->

# Registry

> **제거된 공개 API.**
> 예전 C registry service API는 core 8.4.3에서 공개 계약에서 제거되었다.
> 이 문서는 오래된 링크가 제거 안내로 이어지도록 남겨 둔다.

애플리케이션은 공개 registry handle을 만들거나 core C API로 registry topology를
조회해서는 안 된다. 현재 공개 C 계약은 `core/include/zlink.h`가 기준이다.

framework 수준 위치 조회, topology, status 대체 작업은
`framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`에서
추적한다.
