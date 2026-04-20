[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework FastAPI SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `FastAPI`에서 `SPOT`을 어떤 표면으로 통합할지 정리한다.

## 1. 방향

`SPOT`은 app startup 안에서 등록하고, high-level 표면은 아래 세 축을 먼저
설명한다.

- current channel publish/subscribe
- attach된 channel client를 통한 다른 channel send/request
- 필요할 때만 spot-to-spot routed 호출

## 2. Public surface

- `ZLinkSpotClient`
- `@zlink_spot_request`
- `@zlink_spot_subscription(topic=...)`

일반 channel messaging과 달리 `rid` 직접 지정은 `SPOT`에서만 남긴다.
