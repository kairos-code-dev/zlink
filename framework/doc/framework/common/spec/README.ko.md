# ZLink Framework 스펙

이 디렉토리는 framework의 공통 의미 계약과 언어별 공개 계약을 함께 관리한다.
공통 스펙은 모든 언어가 제공해야 하는 동작을 정의하고, 언어별 스펙은 그 동작이
각 언어에서 보이는 정확한 public API를 정의한다.

## 공통 의미 계약

| 문서 | 범위 |
|------|------|
| [공개 계약 관리](public-contract-governance.ko.md) | 정식 계약, draft, 변경 통제와 검증 규칙 |
| [언어별 구현 차이](implementation-gap.ko.md) | 정식 spec과 현재 `.NET`, Java/Kotlin, Node.js, C++ 구현의 차이 |
| [비동기 실행과 coroutine 정책](async-execution-policy.ko.md) | 비동기 완료, 취소, coroutine 투영 규칙 |
| [framework API](framework-api.ko.md) | framework 역할과 등록 표면의 공통 기준 |
| [Actor 모델](actor-model.ko.md) | actor lifecycle과 호출 의미 |
| [Channel 메시징](channel-messaging.ko.md) | channel runtime 수명, dispatch 실패 정책, startup validation, 종료 중 호출 |
| [SPOT 메시징](spot-messaging.ko.md) | SPOT의 개념 위치, outbound 세 축, publish·subscribe, dispatch 실패 정책, route ingress |
| [Spot Actor Join / Transfer](spot-actor.ko.md) | actor 이동과 callback 순서 |
| [SpotNode](spot-node.ko.md) | SpotNode 등록, Entry Spot과 bind 순서, SpotManager 생성·조회·종료 의미 |
| [Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md) | SPOT 위에 상위 실행 모델을 얹는 계약 — 실행 문맥 보장, timer, 책임 경계 |
| [STREAM 서버 세션](stream-session.ko.md) | 서버 쪽 session 표면, dispatch 모델, 등록 규칙, 오류 경계 |
| [Session Actor Dispatch](session-actor-dispatch.ko.md) | session에서 actor로 전달하는 공통 계약 |
| [메시지 흐름 추적](message-flow-tracing.ko.md) | success-path 메시지 흐름 추적과 dispatch 관측 |
| [메시지 흐름 상관관계](flow-correlation.ko.md) | flow_id 상위 키로 spot/actor 경계·fleet 관통 추적 |
| [런타임 모니터링](runtime-monitoring.ko.md) | runtime 변화 관측 — source 분리 근거, event 종류, polling 규칙 |
| [런타임 메트릭 계기](runtime-metrics.ko.md) | 밖에서 못 재는 신호(CCU·SPOT 큐·actor 이동 등)의 계기 카탈로그 |
| [Graceful Drain & Handoff](graceful-drain-handoff.ko.md) | stateful 노드 우아한 종료·핸드오프 수명주기 계약 |
| [Stream Connector](stream-connector.ko.md) | client connector의 대상 실행 환경(엔진 × 빌드 타깃), transport, wire 계약, 연결 생명주기, 배포 산출물 |

나머지 기능별 공통 스펙도 이 디렉토리에 있으며, 특정 언어의 타입이나 문법을
공통 계약으로 강제하지 않는다. "(제안)" 표시 문서는 아직
[공개 계약 관리](public-contract-governance.ko.md)의 승격 절차를 거치지 않은 제안 스펙이다.

공통 문서에 이해를 돕는 언어별 예시가 있더라도 그 예시는 비규범 설명이다. 정확한
public 타입과 시그니처는 `languages/<lang>/` 문서만 소유한다.

## 언어별 공개 계약

| 언어 | 정식 스펙 |
|------|-----------|
| `.NET` | [languages/dotnet](languages/dotnet/README.ko.md) |
| Java | [languages/java](languages/java/README.ko.md) |
| Kotlin | [languages/kotlin](languages/kotlin/README.ko.md) |
| Node.js / TypeScript | [languages/node](languages/node/README.ko.md) |
| C++ | [languages/cpp](languages/cpp/README.ko.md) |

언어별 디렉토리의 정식 스펙은 각 언어가 제공해야 하는 목표 public contract를
고정한다. 현재 구현이 정식 스펙과 다르면 정식 스펙을 현재 코드에 맞춰 축소하지
않고 [언어별 구현 차이](implementation-gap.ko.md)에 기록한 뒤 구현이 스펙을
따르도록 한다.
