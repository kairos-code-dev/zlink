<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../../../../java/README.ko.md) | [SPOT](spring-boot-spot.ko.md) | [SPOT 가이드](../../../../java/guide/05-spot.ko.md)

# Java Stage Wrapper On SPOT

## 1. 기본 생각

playhouse `Stage` 같은 상위 객체는 `SPOT` 자체를 대체하는 것이 아니라, `SPOT` 위에 얹는
도메인 모델로 보는 편이 맞다. Java에서는 사용자가 만든 그 도메인 객체가
`ZLinkSpot<TActor>` 를 구현한다(별도 framework `Stage` 타입은 없다).

이 상위 도메인 객체(`Stage` wrapper)는 아래 역할을 가져야 한다.

- 현재 spot rid·node rid 노출(`context.spotRid()`/`context.nodeRid()`, 반환 `RoutingId`)
- packet handler registry
- timer 등록
- outbound channel client 접근

## 2. 분리 기준

- 상태와 도메인 메서드: `Stage`
- packet handler: 별도 bean
- 다른 channel 호출: `ZLinkSpotOutbound`

즉 `Stage` 안에 모든 packet 처리와 외부 호출을 몰아 넣지 않는 편이 맞다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
