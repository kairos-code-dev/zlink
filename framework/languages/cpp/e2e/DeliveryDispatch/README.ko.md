# DeliveryDispatch C++ E2E

DeliveryDispatch E2E는 배달 생성, courier 배정, 픽업, 완료까지의 상태 전이를 C++ framework로 검증한다.
구조와 호출 순서는 `.NET` DeliveryDispatch 샘플을 기준으로 맞춘다. Client는 HTTP API로 배달을
생성하고 stream connector로 고객 세션과 배송원 세션을 연 뒤 상태 알림과 배송 제안을 기다린다.

## 실행

```bash
./run_e2e.sh
```

## Topology

- `Client`는 배달 dispatch 흐름을 시나리오처럼 검증한다.
- `Server/Registry`는 discovery registry를 실행한다.
- `Server/DispatchApi`는 `/deliveries`와 `/self-check/assert` HTTP API를 제공한다.
- `Server/DispatchCenter`는 courier 제안과 tracking 상태 갱신을 조율한다.
- `Server/Tracking`은 상태 증거를 기록하고 fanout으로 고객 세션에 상태 알림을 발행한다.
- `Server/Session`은 고객의 `SubscribeDelivery` 요청과 배송원의 `BindCourierSession` 요청을 받고,
  상태 알림과 배송 제안을 client stream으로 보낸다.
- `Probe`는 tracking route가 registry/discovery를 통해 준비됐는지 확인한다.
- `Shared`는 배달 상태 계약을 정의한다.

## Success Condition

runner가 `delivery-dispatch e2e result=passed`를 출력하면 registry readiness, delivery reassignment,
server evidence, message-flow evidence가 함께 검증된 것이다.

## 회귀 테스트

`run_e2e.sh`는 CMake E2E target을 먼저 빌드한 뒤 역할별 process를 실행한다.
