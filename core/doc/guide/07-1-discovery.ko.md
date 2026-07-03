[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

<!-- zlink-nav:start -->
[← 서비스 개요](07-0-services.ko.md) | [SPOT →](07-3-spot.ko.md)
<!-- zlink-nav:end -->

# Service Discovery

> **제거된 공개 API.**
> 예전 C discovery service API는 core 8.4.3에서 공개 계약에서 제거되었다.
> 이 문서는 오래된 링크가 제거 안내로 이어지도록 남겨 둔다.

애플리케이션은 공개 discovery handle을 만들거나 socket/spot에 연결해서는 안 된다.
현재 공개 service API는 SPOT과 socket 가이드에 설명되어 있으며, 정확한 C 계약은
`core/include/zlink.h`가 기준이다.

framework 수준 자동 위치 조회와 라우팅은 location runtime/store 로 대체되었다.
정식 계약은 `framework/doc/framework/common/spec/location-runtime.ko.md`를 본다.
