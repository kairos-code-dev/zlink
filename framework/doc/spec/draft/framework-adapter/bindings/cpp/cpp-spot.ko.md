[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 `SPOT`을 어떤 표면으로 통합할지
> 정리한다.

## 1. 방향

`SPOT`은 `C++` runtime 안에서 중요한 축이므로, standalone host가 아래를 직접
제공하는 편이 맞다.

- `spot_node` bootstrap 과 discovery attach
- capability별 publish/subscribe registration
- attach된 channel client를 통한 다른 channel send/request
- local spot 인스턴스가 없는 외부 노드용 publisher client
- 필요할 때만 spot-to-spot request/send

현재 공통 정책 기준으로는 아래를 같이 지켜야 한다.

- active channel 범위는 node 생성이 아니라 attach된 discovery view가 정한다.
- capability는 `router`, `pub/sub`, attach된 channel client, attach된 spot
  publisher client로 나눠서 설명한다.
- spot factory는 `spot_name`과 함께 등록하고, 같은 이름 재등록은 덮어쓰지 않고
  예외로 본다.
- spot 생성은 `spot_name` 기준으로 설명하고, 운영 코드가
  `spot_rid -> spot_name` 매핑을 다시 볼 수 있어야 한다.
- timer는 공용 scheduler보다 spot lifecycle registration 표면으로 두는 편이
  자연스럽다.

## 2. Public surface

- `spot_client_t`
- `spot_publisher_client_t`
- `spot_request_handler_t`
- `spot_subscription_handler_t`
- `spot_manager_t`
- `timer_t`

일반 channel messaging과 달리 `rid` 직접 지정은 `SPOT`에서만 남긴다.
다만 이것도 current channel publish나 attach된 channel 호출보다 앞에 두지는 않는다.
또한 `spot_name` 기준 생성과 `spot_rid -> spot_name` 조회는 `spot_manager_t`가
같이 맡는 편이 맞다.
