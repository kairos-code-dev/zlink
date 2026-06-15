<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Spring Boot STREAM](./spring-boot-stream.ko.md) | [다음: Draft -- Java STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md)

# Draft -- Java Stage Wrapper On SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, stage 같은 상위 모델을 `SPOT` 위에 감쌀 때 필요한
> 조건을 정리한다.

## 1. 기본 생각

`Stage` 같은 상위 객체는 `SPOT` 자체를 대체하는 것이 아니라, `SPOT` 위에 얹는
도메인 모델로 보는 편이 맞다.

`Stage` wrapper는 아래 역할을 가져야 한다.

- 현재 `SpotRid`, `NodeRid` 노출
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
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Spring Boot STREAM](./spring-boot-stream.ko.md) | [다음: Draft -- Java STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:bottom:end -->
