# ZLink Framework 스펙

이 디렉토리는 framework의 **공통 의미 계약**과 **언어별 공개 계약**을 함께 관리한다.

- **공통 스펙**은 모든 언어가 제공해야 하는 **동작과 규칙**을 정의한다.
- **언어별 스펙**은 그 동작이 각 언어에서 갖는 **정확한 public API**를 정의한다.

**파일 이름의 숫자 prefix는 주제 그룹과 읽는 순서다.** 그룹 사이는 번호를 띄워 두어 새 문서를
끼워 넣을 수 있다.

| 번호대 | 주제 |
|---|---|
| `0x` | 기반 — 계약 관리, 개요, 메시지·상호작용 모델, 비동기 정책 |
| `1x` | Channel · HTTP client |
| `2x` | SPOT · Actor |
| `3x` | STREAM |
| `4x` | Location |
| `5x` | 관측 · 운영 |
| `9x` | 현재 상태 |

## 공통 의미 계약

### 0x 기반

| 문서 | 범위 |
|------|------|
| [00 공개 계약 관리](00-public-contract-governance.ko.md) | 정식 계약과 draft, 변경 통제, 검증 규칙. **먼저 읽는다** |
| [01 개요](01-overview.ko.md) | framework가 무엇을 해결하는가 |
| [02 상호작용 모델](02-interaction-model.ko.md) | request·send·publish의 공용 의미 |
| [03 메시지 모델](03-message-model.ko.md) | header + payload의 multipart wire 구성 |
| [04 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) | 비동기 완료, 취소, coroutine 투영 규칙 |
| [05 framework API](05-framework-api.ko.md) | framework 역할과 등록 표면의 공통 기준 |

### 1x Channel · HTTP client

| 문서 | 범위 |
|------|------|
| [10 channel topology](10-channel-topology.ko.md) | channel 종류, 역할, 자동·수동 연결 |
| [11 channel 메시징](11-channel-messaging.ko.md) | channel runtime 수명, dispatch 실패 정책, startup validation, 종료 중 호출 |
| [12 HTTP client](12-http-client.ko.md) | framework 동반 HTTP client — fluent builder, 실행 terminator, turn seam, 등록, codec, 오류 |

### 2x SPOT · Actor

| 문서 | 범위 |
|------|------|
| [20 SPOT 메시징](20-spot-messaging.ko.md) | SPOT의 개념 위치, outbound 세 축, publish·subscribe, dispatch 실패 정책, route ingress |
| [21 SpotNode](21-spot-node.ko.md) | SpotNode 등록, Entry Spot과 bind 순서, SpotManager 생성·조회·종료 |
| [22 Actor 모델](22-actor-model.ko.md) | actor lifecycle과 호출 의미 |
| [23 Spot Actor Join / Transfer](23-spot-actor.ko.md) | actor 이동과 callback 순서, transfer adapter |
| [24 Spot 주소 메시징](24-spot-address-messaging.ko.md) | spot handle, 조회 표면, stale 주소 실패 계약 |
| [25 Stage Wrapper On SPOT](25-stage-wrapper-on-spot.ko.md) | SPOT 위에 상위 실행 모델을 얹는 계약 — 실행 문맥 보장, timer, 책임 경계 |

### 3x STREAM

| 문서 | 범위 |
|------|------|
| [30 STREAM 서버 세션](30-stream-session.ko.md) | 서버 session 표면, dispatch 모델, 등록 규칙, 오류 경계 |
| [31 Session Actor Dispatch](31-session-actor-dispatch.ko.md) | session에서 actor로 전달하는 gateway 계약 |
| [32 Stream Connector](32-stream-connector.ko.md) | client connector — 대상 실행 환경(엔진 × 빌드 타깃), transport, wire 계약, 생명주기, 배포 산출물 |

### 4x Location

| 문서 | 범위 |
|------|------|
| [40 location runtime](40-location-runtime.ko.md) | location row, owner lease, resolver 계약, runtime query |
| [41 Redis location store](41-location-store-redis.ko.md) | Redis 구현이 지켜야 하는 store 계약 |

### 5x 관측 · 운영

| 문서 | 범위 |
|------|------|
| [50 런타임 모니터링](50-runtime-monitoring.ko.md) | runtime 변화 관측 — source 분리 근거, event 종류, polling 규칙 |
| [51 런타임 메트릭 계기](51-runtime-metrics.ko.md) | 밖에서 못 재는 신호(CCU·SPOT 큐·actor 이동 등)의 계기 카탈로그 |
| [52 메시지 흐름 추적](52-message-flow-tracing.ko.md) | success-path 메시지 흐름 추적과 dispatch 관측 |
| [53 메시지 흐름 상관관계](53-flow-correlation.ko.md) | flow_id 상위 키로 spot/actor 경계·fleet 관통 추적 |
| [54 Graceful Drain & Handoff](54-graceful-drain-handoff.ko.md) | stateful 노드 우아한 종료·핸드오프 수명주기 |

### 9x 현재 상태

| 문서 | 범위 |
|------|------|
| [90 언어별 구현 차이](90-implementation-gap.ko.md) | 정식 spec과 현재 `.NET`, Java/Kotlin, Node.js, C++ 구현의 차이 |

**공통 스펙은 특정 언어의 타입이나 문법을 계약으로 강제하지 않는다.** 이해를 돕는 언어별 예시가
있더라도 그 예시는 **비규범 설명**이다. 정확한 public 타입과 시그니처는 `languages/<lang>/`
문서만 소유한다.

"(제안)" 표시 문서는 아직 [공개 계약 관리](00-public-contract-governance.ko.md)의 승격 절차를
거치지 않은 제안 스펙이다.

## 언어별 공개 계약

**언어별 스펙은 아래 네 문서로 고정한다.** 그 밖의 기능별 문서를 따로 두지 않는다 — 기능의
의미는 위 공통 스펙이 소유한다. 언어에 따라 해당 없는 문서는 두지 않는다(표 참조).

| 번호 | 문서 | 범위 |
|---|------|------|
| `01` | 시스템 구조 | 패키지 구조·배포, host 등록, DI, lifecycle, startup validation |
| `02` | 인터페이스 | 전체 public interface·타입·시그니처 카탈로그 |
| `03` | Stream Connector | client connector의 public 표면 |
| `04` | HTTP client | framework 동반 HTTP client의 public 표면 **(작성 예정)** |

| 언어 | 정식 스펙 | 두는 문서 |
|------|-----------|-----------|
| `.NET` | [languages/dotnet](languages/dotnet/README.ko.md) | `01`, `02`, `03` (+ `04` 예정) |
| Java | [languages/java](languages/java/README.ko.md) | `01`, `02`, `03` (+ `04` 예정) |
| Kotlin | [languages/kotlin](languages/kotlin/README.ko.md) | `02`만 — Java 런타임을 공유하므로 Kotlin 고유 표면(`suspend`, `Flow`, DSL)만 고정한다 |
| Node.js | [languages/node](languages/node/README.ko.md) | `01`, `02` (+ `04` 예정) — connector는 아래 TypeScript가 소유한다 |
| TypeScript | [languages/typescript](languages/typescript/README.ko.md) | `03`만 — browser stream connector의 public 표면. Node.js framework 계약과 분리한다([00 §4](00-public-contract-governance.ko.md)) |
| **C++** | [languages/cpp](languages/cpp/README.ko.md) | **예외.** framework 자체를 구현하므로 기능별 스펙을 유지한다 |

**Node.js와 TypeScript를 나눈 이유:** stream connector는 브라우저에서 도는 client이고 Node.js
framework host와 배포 단위·실행 환경이 다르다. 두 계약을 한 디렉토리에 두면 어느 쪽이 어느
package의 표면인지 흐려진다. Node.js 문서가 connector를 참조할 때는 `languages/typescript/03`을
링크한다.

언어별 디렉토리의 정식 스펙은 각 언어가 제공해야 하는 **목표 public contract**를 고정한다.
**현재 구현이 정식 스펙과 다르면 정식 스펙을 코드에 맞춰 축소하지 않는다.**
[언어별 구현 차이](90-implementation-gap.ko.md)에 기록한 뒤 **구현이 스펙을 따르게 한다.**
