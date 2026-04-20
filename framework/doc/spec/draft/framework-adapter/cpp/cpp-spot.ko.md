[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 `SPOT`을 어떤 표면으로 통합할지
> 정리한다.

## 1. 방향

`SPOT`은 `C++` runtime 안에서 중요한 축이므로, standalone host가 아래를 직접
제공하는 편이 맞다.

- `spot_node` bootstrap
- publish/subscribe registration
- spot-to-spot request/send
- attach된 channel client를 통한 다른 channel send/request

## 2. Public surface

- `spot_client_t`
- `spot_request_handler_t`
- `spot_subscription_handler_t`
- `spot_manager_t`

일반 channel messaging과 달리 `rid` 직접 지정은 `SPOT`에서만 남긴다.
