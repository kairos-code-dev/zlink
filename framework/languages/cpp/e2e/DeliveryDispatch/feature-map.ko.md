# C++ DeliveryDispatch E2E feature map

기준 구현: `framework/languages/dotnet/samples/DeliveryDispatch`

이 파일은 `.NET DeliveryDispatch` 샘플을 기준으로 C++ E2E가 검증하는 역할, 메시지 흐름, 남은 차이를
기록한다. 이 config는 공통 `config-*.ko.md`에 별도 scenario ID가 없으므로, `.NET` 샘플 README와
client scenario의 성공 marker를 검증 기준으로 삼는다.

| 항목 | 상태 | 근거 |
|------|------|------|
| `DD-A1` registry readiness | 구현 | `Probe` 실행 파일이 registry discovery로 Tracking route readiness를 확인한다. 최신 통과: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`, 출력: `delivery-dispatch e2e result=passed`. |
| `DD-A2` successful delivery | 부분 구현 | client가 `delivery-success`를 생성하고 `Assigned`, `Accepted`, `PickedUp`, `Delivered` push를 customer stream connector로 기다린다. 최신 통과: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`. 단, 최신 .NET 샘플처럼 courier stream session에서 offer push를 받고 `CourierDecision`을 보내는 경로는 아직 없다. |
| `DD-A3` delivery reassignment | 부분 구현 | `courier-a`를 `timeout-reassign` mode로 실행하고, `courier-b` 재배정 뒤 `deliverydispatch-reassignment=completed` marker를 확인한다. 최신 통과: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`. 단, courier-a/courier-b가 각자의 stream session과 actor node를 통해 offer를 받는 경로는 아직 없다. |
| `DD-A4` server evidence self-check | 구현 | `/self-check/assert`가 두 delivery의 상태 순서를 evidence log에서 확인하고 `deliverydispatch-server-evidence=completed` marker를 출력한다. 최신 통과: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`. |
| `DD-A5` message-flow evidence | 구현 | runner가 각 role의 flow log에서 `message flow` 기록을 확인한다. 최신 통과: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`. |
| role split parity | 부분 구현 | Registry, DispatchApi, DispatchCenter, Courier, Tracking, Session, Probe, Client를 별도 executable로 실행한다. 최신 .NET 샘플의 CustomerGateway, CourierSession, CourierGateway, CourierActorNode 역할은 아직 같은 의미로 분리되지 않았다. |
| shared contract parity | 부분 구현 | C++ `Shared/Contracts/messages.hpp`가 배송 생성, tracking, customer subscription DTO와 typed JSON stream payload hook을 제공한다. 최신 .NET 샘플의 courier session/actor-node DTO와 role 흐름은 아직 완전 대응하지 않는다. |
| tracking file split parity | 구현 | CustomerActor, DeliverySpotDirectory, DeliveryTrackingSpot, CustomerEntrySpot, Tracking handlers를 별도 header로 나누고 role wiring만 `Tracking/main.cpp`에 둔다. |

## C++ 표면 차이

- `.NET`은 project 파일 단위로 역할을 나누고, C++은 CMake target 단위로 역할을 나눈다.
- `.NET`의 `SampleFlowLog.cs` 책임은 C++에서 `sample_log_dir.hpp`와 framework trace 옵션으로 대응한다.
- `.NET`의 Tracking spot 세부 책임은 C++에서 별도 header로 분리한다. C++ E2E는 별도 framework
  SPOT mesh node를 추가하지 않고, Tracking role 내부 상태로 customer join과 delivery status history를
  기록한 뒤 customer subscription, delivery status fanout, server evidence로 public 동작을 검증한다.

## 남은 gap

최신 `.NET DeliveryDispatch` 샘플은 customer stream 하나와 courier stream 둘을 열고, 각 courier를
`CourierSession`에 bind한 뒤 `CourierGateway`와 `CourierActorNode`가 courier id별 actor node와 session
route를 해석한다. C++ E2E는 아직 이 courier stream/actor-node 경로를 구현하지 않고, `Server/Courier`
role이 offer accept/timeout을 직접 처리한다.

이 gap은 샘플 public 흐름의 차이이므로 완료로 보지 않는다. 후속 작업에서는 `CourierSession`,
`CourierGateway`, `CourierActorNode` 역할과 `BindCourierSession`, `OfferDelivery`, `CourierDecision`
stream 경로를 C++ public framework API로 포팅해야 한다.
