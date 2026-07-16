# C++ DeliveryDispatch Sample feature map

기준 구현: `framework/languages/dotnet/samples/DeliveryDispatch`

이 파일은 `.NET DeliveryDispatch` 샘플을 기준으로 C++ 샘플이 검증하는 역할, 메시지 흐름, 언어별 구현 배치를
기록한다. 이 config는 공통 `config-*.ko.md`에 별도 scenario ID가 없으므로, `.NET` 샘플 README와
client scenario의 성공 marker를 검증 기준으로 삼는다.

| 항목 | 상태 | 근거 |
|------|------|------|
| `DD-A1` location readiness | 구현 | `Probe` 실행 파일이 Redis location store 기반 topology로 Tracking route readiness를 확인한다. 최신 통과: `timeout 420s framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh`, 출력: `deliverydispatch sample result=passed`. |
| `DD-A2` successful delivery | 구현 | client가 `delivery-success`를 생성하고 `Assigned`, `Accepted`, `PickedUp`, `Delivered` push를 customer stream connector로 기다린다. CustomerGateway는 customer actor를 stream에 bind하고 bound session으로 status를 push한다. courier-a stream session은 `OfferDeliveryNotify`를 받은 뒤 `CourierDecisionMsg`로 수락한다. 최신 통과: `timeout 420s framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh`, 출력: `deliverydispatch sample result=passed`. |
| `DD-A3` delivery reassignment | 구현 | courier-a stream session이 첫 `OfferDeliveryNotify`를 받은 뒤 응답하지 않고, dispatch timeout 뒤 courier-b stream session이 두 번째 offer를 받아 `CourierDecisionMsg`로 수락한다. client는 `deliverydispatch-reassignment=completed` marker를 확인한다. 최신 통과: `timeout 420s framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh`, 출력: `deliverydispatch sample result=passed`. |
| `DD-A4` server evidence self-check | 구현 | `/self-check/assert`가 두 delivery의 상태 순서를 evidence log에서 확인하고 `deliverydispatch-server-evidence=completed` marker를 출력한다. 최신 통과: `timeout 420s framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh`. |
| `DD-A5` message-flow evidence | 구현 | runner가 각 role의 flow log에서 `message flow` 기록을 확인하고, `flow-customer-gateway.log`, `flow-courier-session.log`, `flow-courier-gateway.log`, `flow-delivery-courier-node-1.log`, `flow-delivery-courier-node-2.log`까지 별도로 확인한다. 최신 통과: `timeout 420s framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh`. |
| role split parity | 구현 | DispatchApi, DispatchCenter, Tracking, CustomerGateway, CourierSession, CourierGateway, CourierActorNode, Probe, Client를 별도 executable로 실행한다. Customer stream과 courier stream endpoint도 분리했다. CustomerGateway와 CourierSession은 actor ref를 stream에 bind하고, 각 actor의 bound session으로 customer status와 courier offer를 push한다. |
| shared contract parity | 구현 | C++ `Shared/Contracts/messages.hpp`가 배송 생성, tracking, customer subscription, `BindCourierReq`, `EnsureCourierActorReq`, courier session bind, offer notify, courier decision DTO를 제공하고, client는 public JSON stream connector codec 경로를 사용한다. CustomerGateway와 Courier 쪽 actor-bound session 경로도 public framework API로 대응한다. |
| tracking file split parity | 구현 | CustomerActor, DeliverySpotDirectory, DeliveryTrackingSpot, CustomerEntrySpot, Tracking handlers를 별도 header로 나누고 role wiring만 `Tracking/main.cpp`에 둔다. |

## C++ 표면 차이

- `.NET`은 project 파일 단위로 역할을 나누고, C++은 CMake target 단위로 역할을 나눈다.
- `.NET`의 `SampleFlowLog.cs` 책임은 C++에서 `sample_log_dir.hpp`와 framework trace 옵션으로 대응한다.
- `.NET`의 Tracking spot 세부 책임은 C++에서 별도 header로 분리한다. Tracking role은 상태 history와
  fanout publish를 맡고, CustomerGateway role은 customer actor spot mesh와 bound session push를 맡는다.

## 구현 배치 차이

최신 `.NET DeliveryDispatch` 샘플은 customer stream 하나와 courier stream 둘을 열고, 각 courier를
`CourierSession`에 bind한 뒤 `CourierGateway`와 `CourierActorNode`가 courier id별 actor node와 session
route를 해석한다. C++ 샘플도 별도 `CustomerGateway`, `CourierSession`, `CourierGateway`,
`CourierActorNode` 실행 파일을 두고 customer/courier stream endpoint와 courier route split을 검증한다.
CustomerGateway는 public `session_actor_manager_t`로 customer actor ref를 현재 session에 바인드하고,
framework가 stream 연결과 disconnect 정리를 관리한다. status fanout handler는 bound session으로
`DeliveryStatusNotify`를 push한다.
CourierSession도 public `session_actor_manager_t`로 actor ref를 현재 session에 바인드하고,
CourierActorNode entry spot handler는 actor context의 bound session으로 `OfferDeliveryNotify`를 push한다.

남는 차이는 파일 분류 수준이다. `.NET`은 CustomerGateway/CourierActorNode의 actor, directory, entry spot,
handler를 여러 C# 파일로 나누지만 C++ 샘플은 일부 책임을 role `main.cpp` 안에 둔다. public 동작과
runner 증거 기준의 actor-bound session gap은 남기지 않는다.
