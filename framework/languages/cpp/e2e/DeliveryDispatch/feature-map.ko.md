# C++ DeliveryDispatch E2E feature map

기준 구현: `framework/languages/dotnet/samples/DeliveryDispatch`

이 파일은 `.NET DeliveryDispatch` 샘플을 기준으로 C++ E2E가 검증하는 역할, 메시지 흐름, 남은 차이를
기록한다. 이 config는 공통 `config-*.ko.md`에 별도 scenario ID가 없으므로, `.NET` 샘플 README와
client scenario의 성공 marker를 검증 기준으로 삼는다.

| 항목 | 상태 | 근거 |
|------|------|------|
| `DD-A1` registry readiness | 구현 | `Probe` 실행 파일이 registry discovery로 Tracking route readiness를 확인한다. |
| `DD-A2` successful delivery | 구현 | client가 `delivery-success`를 생성하고 `Assigned`, `Accepted`, `PickedUp`, `Delivered` push를 stream connector로 기다린다. |
| `DD-A3` delivery reassignment | 구현 | `courier-a`를 `timeout-reassign` mode로 실행하고, `courier-b` 재배정 뒤 `deliverydispatch-reassignment=completed` marker를 확인한다. |
| `DD-A4` server evidence self-check | 구현 | `/self-check/assert`가 두 delivery의 상태 순서를 evidence log에서 확인하고 `deliverydispatch-server-evidence=completed` marker를 출력한다. |
| `DD-A5` message-flow evidence | 구현 | runner가 각 role의 flow log에서 `message flow` 기록을 확인한다. |
| role split parity | 구현 | Registry, DispatchApi, DispatchCenter, Courier, Tracking, Session, Probe, Client를 별도 executable로 실행한다. |
| shared contract parity | 구현 | C++ `Shared/Contracts/messages.hpp`가 `.NET Shared/Contracts/Messages.cs`의 request, reply, notify DTO를 대응한다. |
| tracking file split parity | 구현 | CustomerActor, DeliverySpotDirectory, DeliveryTrackingSpot, CustomerEntrySpot, Tracking handlers를 별도 header로 나누고 role wiring만 `Tracking/main.cpp`에 둔다. |

## C++ 표면 차이

- `.NET`은 project 파일 단위로 역할을 나누고, C++은 CMake target 단위로 역할을 나눈다.
- `.NET`의 `SampleFlowLog.cs` 책임은 C++에서 `sample_log_dir.hpp`와 framework trace 옵션으로 대응한다.
- `.NET`의 Tracking spot 세부 책임은 C++에서 별도 header로 분리한다. C++ E2E는 별도 framework
  SPOT mesh node를 추가하지 않고, Tracking role 내부 상태로 customer join과 delivery status history를
  기록한 뒤 customer subscription, delivery status fanout, server evidence로 public 동작을 검증한다.
