<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Discovery·Registry HA](config-6-discovery-registry-ha.ko.md)
<!-- framework-adapter-nav:end -->

# Config 7 — Runtime Monitoring 배포

운영 중인 배포에서 socket·registry·spot 이벤트를 runtime monitoring으로 관찰하는 기능을 본다.
각 config가 쓰는 dispatch-error observer(evidence marker)와는 별개의, 독립된 monitoring 표면이다.

monitoring source는 관찰 대상을 **같은 앱(DI container)** 안에서 본다 — socket/spot source는 그
channel/spot을 호스팅하는 앱에, registry source는 `AddZLinkRegistry(...)`가 올라온 앱에
register한다(`ZLinkMonitoringSourceValidator`). 즉 별도 관찰자 프로세스가 다른 프로세스의 source를
직접 들여다보는 구조가 아니다. 각 host가 자기 source의 이벤트를 event handler로 받아 자기
evidence에 기록한다.

## 1. 목적과 범위

- 다룬다: socket/registry/spot source의 실제 event kind 관찰, event kind 필터, 등록 검증(startup), event dispatch 실패가 runtime을 막지 않는지.
- 여기서 다루지 않는 것: 기능 자체의 messaging 정확성(다른 config), 파일 로그(Config 5 RL-D3).

## 2. 서버 구성 (한 번 구동)

| 역할 | 수 | 구성 |
|------|----|------|
| registry 노드 | 1 | `AddZLinkRegistry(...)` + registry monitoring source를 colocate. registry 이벤트를 event handler로 받아 evidence에 기록. `/evidence`·`/health`. |
| service 노드 | 2 (`svc-a`, `svc-b`) | channel + spot 호스트. 자기 socket·spot monitoring source를 colocate해 이벤트를 evidence에 기록. |
| trigger client | 시나리오별 | 연결·해제, provider scale, spot subject/peer 변화를 유발해 이벤트를 만든다. |

각 host는 framework public monitoring API(`AddZLinkMonitoring(...)`)로 자기 source를 등록하고
event handler에서 관찰 이벤트를 evidence에 기록한다.

이벤트 kind는 고정 enum이다(이 config가 기대해도 되는 것만):

- socket: `Connected`, `ConnectionReady`, `Disconnected`, `HandshakeFailed`, `PeerAdmissionChanged`, `Closed`
- registry: `StatusChanged`, `TopologyChanged`, `ServiceSummaryChanged`
- spot: `StatusChanged`, `PeersChanged`, `SubjectsChanged`, `TimerHandlerFailed`, `TimerStoppedAfterUnhandledException`

actor join/leave, spot 생성·소멸 같은 lifecycle 이벤트는 monitoring kind로는 존재하지 않는다.
spot 쪽 관찰은 `SubjectsChanged`/`PeersChanged`로 본다.

## 3. 실행 모델

`run_e2e.sh`가 registry → service 노드를 monitoring colocated로 띄우고, trigger client가 이벤트를
유발한다. 각 host의 evidence를 조회해 관찰 결과를 확인한다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.
(monitoring 관측 evidence와 별개로, 흐름 추적 파일 로그도 함께 남긴다.)

## 4. 시나리오

### Track A — 이벤트 관찰

#### MON-A1 socket 이벤트 관찰

우선순위: `P0`

**한마디로:** client가 붙었다 끊을 때, socket source가 그 연결·해제 이벤트를 정해진 kind와 식별 정보까지 담아 관찰하는가.

- 절차: trigger client가 service의 channel에 연결했다가 끊는다. service host의 socket source가 관찰한다.
- 검증: socket 이벤트가 evidence에 기록되며 kind가 위 socket enum에 속한다(연결 시 `Connected`/`ConnectionReady`, 해제 시 `Disconnected`/`Closed`). 각 이벤트는 source name(`<channel>.<capability>` 형식)과 payload(`RemoteAddr`, 있으면 `RoutingId`)를 포함한다.
- 세부 동작: socket source 관찰(고정 kind·식별자).

#### MON-A2 registry 이벤트 관찰

우선순위: `P0`

**한마디로:** provider를 추가/종료해 topology를 흔들면, registry source가 `TopologyChanged`/`ServiceSummaryChanged`를 실제 변화 내용과 함께 관찰하는가.

- 절차: provider(`svc-a`/`svc-b`)를 추가/종료해 topology를 바꾼다. registry 노드의 registry source가 관찰한다.
- 검증: provider start/stop으로 `TopologyChanged`와 `ServiceSummaryChanged` 이벤트가 발행되고, 그 payload(topology snapshot/summary)가 실제 변화를 반영한다. (registry kind는 `StatusChanged`/`TopologyChanged`/`ServiceSummaryChanged` 3종 고정 — "등록/해제" 이벤트는 없다.)
- 세부 동작: registry source 관찰(snapshot diff 기반).

#### MON-A3 spot 이벤트 관찰

우선순위: `P0`

**한마디로:** spot의 subject/peer를 바꾸면 `SubjectsChanged`/`PeersChanged`가, timer가 예외로 죽으면 `TimerHandlerFailed`가 관찰되는가.

- 절차: spot의 subject(구독/멤버) 또는 peer 구성을 바꾸는 트리거를 발생시킨다. spot source가 관찰한다.
- 검증: `SubjectsChanged`/`PeersChanged` 이벤트가 evidence에 기록되어 실제 subject/peer 변화를 반영한다. spot timer가 예외로 실패하면 `TimerHandlerFailed`도 관찰된다.
- 세부 동작: spot source 관찰(subjects/peers/timer kind).

#### MON-A4 가용성 전이 관측 (failover / drain)

우선순위: `P1`

**한마디로:** provider 교체(failover)나 런타임 drain으로 가용성이 바뀔 때, 그 전이가 monitoring 이벤트(연결/해제·admission 변경)와 registry `TopologyChanged`로 잡히는가.

- 절차: (a) provider를 같은 rid 다른 endpoint로 교체(failover)하고, (b) 한 provider를 런타임 drain(`ConfigureServerSocket().Weight = 0`)했다가 restore한다. socket·registry source가 관찰한다.
- 검증: failover 시 연결 전이가 socket 이벤트(`Disconnected`/`Connected`/`ConnectionReady`)로, topology 변화가 registry `TopologyChanged`/`ServiceSummaryChanged`로 관측된다. drain된 peer의 가용성 변화는 연결된 peer 쪽 socket 이벤트(`PeerAdmissionChanged`)로 나타난다. (weight 값 자체의 세밀한 관측은 monitoring kind가 아니라 channel 옵션의 `Weight` read로 보완한다 — socket 이벤트 kind는 §2의 고정 enum 범위다.)
- 세부 동작: 가용성 전이(failover·drain) 관측(고정 enum + topology).

### Track B — 등록과 필터 검증

#### MON-B1 event kind 필터

우선순위: `P1`

**한마디로:** 특정 kind만 받도록 필터를 걸면 그 kind만 들어오고, 빈 필터면 전체가 들어오는가.

- 절차: socket source를 특정 kind만 받도록 필터를 걸어 등록하고(빈 kind 집합이면 전체), 여러 종류의 socket 이벤트를 유발한다.
- 검증: 필터에 포함한 kind만 evidence에 기록되고 나머지는 들어오지 않는다. 빈 kind 필터는 전체 kind를 받는다.
- 세부 동작: monitoring kind 필터.

#### MON-B2 monitoring 등록 검증

우선순위: `P1`

**한마디로:** 잘못된 monitoring 등록이 구성 시점/host 시작 시점 각각에서 정확히 명확한 오류로 걸러지는가.

- 절차: (a) **구성 단계** — `AddZLinkMonitoring(...)` 호출에서 잘못된 등록(중복 source, 비양수 polling interval)을 시도한다. (b) **host 시작 단계** — 미존재 socket/spot source를 가리키는 등록으로 host를 기동한다.
- 검증: (a)는 `AddZLinkMonitoring` 구성 시점에 동기 예외로 실패한다. (b)는 host 시작 시 실패하는데, 미존재 **spot** source는 `ZLinkMonitoringSourceValidator`의 preflight에서, 미존재 **socket** source는 그보다 뒤 `ZLinkMonitoringHostedService.AttachSocketMonitors`(→ `ZLinkChannelRuntimeManager.GetMonitoringSocket`)에서 실패한다. 정상 등록만 있으면 정상 기동. (polling interval은 API가 `TimeSpan` 필수 인자라 "누락"은 컴파일 타임 — 런타임 검증 대상은 비양수만.)
- 세부 동작: 구성 시점 vs host 시작 시점 검증 분리.

### Track C — 실패 격리

#### MON-C1 event dispatch 실패 격리

우선순위: `P1`

**한마디로:** event handler 하나가 예외를 던져도 framework runtime이나 messaging이 멈추지 않고(관찰은 best-effort), 그 실패가 error sink로 보고되는가.

- 절차: 한 event handler가 예외를 던지게 하고 이벤트를 계속 유발한다.
- 검증: event dispatch task의 실패가 framework runtime이나 messaging 경로를 종료시키지 않는다(관찰은 best-effort). 실패는 runtime error sink/debug event로 보고된다. (단, 같은 이벤트에 등록된 여러 handler를 순차 await하므로 "한 handler 실패가 같은 이벤트의 다음 handler 실행을 보장한다"고는 단언하지 않는다.)
- 세부 동작: 관측 dispatch 실패가 runtime을 막지 않음.

### Track D — 장애 중 관측 연속성

#### MON-D1 장애·복구 반복 중 이벤트 연속성

우선순위: `P1`

**한마디로:** provider가 죽었다 살기를 반복하는 동안에도 monitoring이 계속 이벤트를 잡고, 그 전이가 실제 장애·복구를 반영하며, 관측이 runtime을 끌어내리지 않는가.

- 절차: provider를 crash·재기동(필요 시 drain·restore 포함)으로 여러 번 흔드는 동안 socket·registry source가 계속 관찰하게 둔다(harness kill/restart 전제).
- 검증: 장애 구간에도 monitoring task가 죽지 않고 이벤트 기록이 이어지며, 관측된 전이(연결/해제, `TopologyChanged`)가 실제 down/up 순서를 반영한다. 관측은 best-effort 계약 안에서 동작하고, monitoring이 messaging·runtime 경로를 막거나 종료시키지 않는다.
- 세부 동작: 장애·복구 반복 중 관측 연속성(best-effort).

## 5. 완료 기준

- Track A의 `P0`(MON-A1·A2·A3)는 모두 통과한다.
- 관찰 이벤트의 kind는 위 고정 enum에 속해야 하고, payload는 실제 발생한 변화(연결·topology·subjects)를 반영해야 한다.
- public monitoring API만 직접 사용하고 `ensure`로 단언한다.
