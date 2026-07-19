# Framework 언어별 구현 차이

[스펙 목차](README.ko.md) | [이전: Graceful Drain & Handoff 수명주기 계약](server/54-graceful-drain-handoff.ko.md)

이 문서는 정식 public contract가 아니다. 공통 스펙과 언어별 스펙을 기준으로 현재
구현에서 확인된 차이를 기록한다. 차이를 해결할 때 정식 스펙을 현재 코드에 맞춰
축소하지 않고, 구현과 contract test를 정식 스펙에 맞춘다.

검토 기준일은 2026-07-17이며 대상은 `.NET`, Java/Kotlin, Node.js와 C++ framework다.

## 구현 차이의 소유권

이 문서는 `.NET`, Java/Kotlin, Node.js와 C++ framework의 현재 구현 차이를 한곳에서 관리한다.
공통·package별 spec은 목표 동작을 소유하고, 언어별 exact spec은 정확한 public interface와 해당
언어에서 관찰한 차이를 기록한다. 언어별 exact spec의 구현 차이 표는 이 문서를 참조한다.

구현 stage의 상태, 담당자와 실행 증거는 이 문서에 기록하지 않는다. 이 문서는 목표 계약과 현재
public surface 사이의 차이만 설명한다.

## 1. 판정 기준

다음은 구현 차이다.

- 공통 스펙이 요구하는 기능이나 관찰 가능한 결과가 특정 언어에 없다.
- 언어별 스펙의 public 타입, 메서드, 반환형 또는 오류 의미가 실제 public surface와 다르다.
- 내부 구현 타입이 package root나 public contract 영역을 통해 외부에 노출된다.
- 비동기 완료를 기다려야 하는 callback이 blocking wait로 연결된다.

다음은 구현 차이가 아니다.

- 같은 기능을 `ValueTask`, `CompletionStage`, `suspend`, `Promise`, coroutine task처럼
  언어별 비동기 관례로 표현하는 차이
- interface, decorator, function object, template처럼 등록 문법이 다른 경우
- 명시적인 취소 인자가 없는 언어. 취소는 언어별 스펙이 제공하기로 한 작업에서만
  계약이며, 모든 언어의 필수 parity 항목이 아니다.

## 2. 스펙별 확인표

**공통 스펙 문서 하나가 한 행이다.** 각 칸은 그 언어 구현이 그 스펙을 충족하는지 나타낸다.

| 기호 | 뜻 |
|---|---|
| **O** | 충족 — contract/unit test와 E2E로 검증했다 |
| **△** | **부분 충족** — 일부 항목이 미해결이다. 해당 gap 절을 링크한다 |
| **X** | **미충족** — 계약을 어긴다. 해당 gap 절을 링크한다 |
| **?** | **미검증** — 이 문서가 확인하지 않았다. 충족한다는 뜻이 아니다 |
| **—** | 구현 대상이 아니다(규약·서술 문서) |

**Kotlin은 Java 런타임을 공유한다.** 표의 Kotlin 칸은 **Kotlin 고유 표면**(`suspend`, `Flow`,
DSL)이 그 스펙을 만족하는지를 뜻한다. Kotlin contract/unit/integration test와 Kotlin E2E가
Java runtime을 Kotlin 표면으로 사용해 같은 결과를 내는지 별도로 검증했다.

### 2.1 기반 (0x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 00 | [공개 계약 관리](00-public-contract-governance.ko.md) | — | — | — | — | — |
| 01 | [개요](01-overview.ko.md) | — | — | — | — | — |
| 02 | [상호작용 모델](02-interaction-model.ko.md) | O | O | O | O | O |
| 03 | [메시지 모델](03-message-model.ko.md) | O | O | O | O | O |
| 04 | [비동기 실행 정책](04-async-execution-policy.ko.md) | O | **X** §12.23 | **X** §12.23 | O | **X** §12.23 |
| 05 | [framework API](05-framework-api.ko.md) | **X** §12.32 §12.33 | **X** §12.26 §12.32 §12.33 | **X** §12.26 §12.32 §12.33 | **X** §12.32 §12.33 | **X** §12.32 §12.33 |

### 2.2 Channel (1x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 10 | [channel topology](server/10-channel-topology.ko.md) | **X** §12.33 | **X** §12.33 | **X** §12.33 | **X** §12.33 | **X** §12.33 |
| 12 | [HTTP client](http-client/12-http-client.ko.md) | O | O | O | O | **X** §12.22 |
| 11 | [channel 메시징](server/11-channel-messaging.ko.md) | O | O | O | O | O |

### 2.3 SPOT · Actor (2x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 20 | [SPOT 메시징](server/20-spot-messaging.ko.md) | O | O | O | O | O |
| 21 | [MeshNode](server/21-mesh-node.ko.md) | **X** §12.33 | **X** §12.33 | **X** §12.33 | **X** §12.33 | **X** §12.33 |
| 22 | [Actor 모델](server/22-actor-model.ko.md) | **X** §12.34 | **X** §12.2 | **X** §12.2 | O | **X** §12.2 §12.34 |
| 23 | [Spot Actor Join/Transfer](server/23-spot-actor.ko.md) | **X** §12.24 §12.29 | **X** §12.2 §12.24 §12.29 | **X** §12.2 §12.24 §12.29 | **X** §12.24 §12.29 | **X** §12.2 §12.24 §12.29 |
| 24 | [Spot 주소 메시징](server/24-spot-address-messaging.ko.md) | O | **X** §2.7 | **X** §2.7 | O | **X** §2.7 |
| 25 | [Stage Wrapper](server/25-stage-wrapper-on-spot.ko.md) | O | O | O | O | O |

### 2.4 STREAM (3x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 30 | [STREAM 서버 세션](server/30-stream-session.ko.md) | O | O | O | O | **X** §12.30 |
| 31 | [Session Actor Dispatch](server/31-session-actor-dispatch.ko.md) | **X** §12.28 | **X** §12.28 | **X** §12.28 | **X** §12.28 | **X** §12.28 |
| 32 | [Stream Connector](stream-connector/32-stream-connector.ko.md) | **X** §12.25 | **X** §12.1 §12.15 §12.25 | **X** Java 표면 상속 + §12.25 | O | **X** §12.25 |

### 2.5 Location (4x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 40 | [location runtime](server/40-location-runtime.ko.md) | **X** §12.27 | **X** §12.27 | **X** §12.27 | **X** §12.27 | **X** §12.27 |
| 41 | [Redis location store](server/41-location-store-redis.ko.md) | **X** §12.27 §12.29 | **X** §12.27 §12.29 | **X** §12.27 §12.29 | **X** §12.27 §12.29 | **X** §12.27 §12.29 |

### 2.6 관측 · 운영 (5x)

| # | 스펙 | `.NET` | Java | Kotlin | Node | C++ |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 50 | [런타임 모니터링](server/50-runtime-monitoring.ko.md) | O | **△** §12.8 | **△** §12.8 | O | O |
| 51 | [런타임 메트릭](server/51-runtime-metrics.ko.md) | **X** §12.31 | **X** §12.31 | **X** §12.31 | **X** §12.31 | **X** §12.31 |
| 52 | [메시지 흐름 추적](server/52-message-flow-tracing.ko.md) | O | O | O | O | O |
| 53 | [흐름 상관관계](server/53-flow-correlation.ko.md) | O | O | O | O | O |
| 54 | [Graceful Drain](server/54-graceful-drain-handoff.ko.md) | O | O | O | O | O |

### 2.7 열려 있는 gap 요약

**모든 X·△를 여기 모은다.** 이 목록이 비면 구현이 정본을 전부 따른 것이다.

| gap | 언어 | 내용 |
|---|---|---|
| §12.1 | **Java** | **독립 unread-history가 없다.** overflow가 가장 오래된 메시지를 버리고(기준선은 새 메시지), 기본 상한이 무제한이며, drop 오류가 없고, handler 없는 메시지가 폐기되며, `waitFor`가 기존 메시지를 못 받고, `AUTO`에서 한도가 적용되지 않는다 |
| §12.2 | **Java, C++** | `onActorJoin` admission이 **선택 사항**이라, 구현을 빠뜨리면 컴파일은 통과하고 **모든 join이 조용히 거절**된다 |
| §12.8 | **Java** | runtime event 모델이 sealed 계층이 아니라 flat record + kind enum이고, `ZLinkMonitoringOptions`에 location 계열 source 등록 4개가 없으며, event handler가 `void`를 반환한다 |
| §12.9 | **Java, Kotlin, C++** | Java/Kotlin은 `sendToSpot`/`requestToSpot`이 spot handle과 channel 이름을 함께 받는다. C++은 spot context outbound가 node RID와 spot RID를 함께 받는다. 모든 언어에서 handle이 전송 mesh를 소유해야 한다 |
| §12.15 | **Java** | 비동기 실패를 오류 코드를 담은 공통 예외로 정규화하지 않는다 |
| §12.22 | **C++** | HTTP client에 `yield`·`submit`이 없고 DI 서버 표면도 없다 |
| §12.23 | **C++, Java/Kotlin** | 두 worker와 terminator는 구현했지만 callback에 cancellation 신호를 전달하지 않는다 |
| §12.24 | **전 언어** | accepted join의 location CAS보다 source leave를 먼저 실행하거나, target membership·`OnJoinedActor` 뒤에 location을 기록한다. CAS 실패에서 source membership을 보존하는 정식 commit 순서를 충족하지 않는다 |
| §12.25 | **`.NET`, Java/Kotlin, C++** | `.NET`·Java/Kotlin은 계약에 없는 receive-count API가 남아 있다. `.NET`은 handler-bound send가 공통 수신 큐 admission을 우회한다. C++는 operation별 codec 선택과 connector codec registry가 남아 있고 connector-level typed codec option이 없다 |
| §12.26 | **Java, Kotlin** | exact interface의 `ZLinkRouteMeshRuntimeOptions`가 없고 E2E도 기존 ChannelName 전용 runtime options를 사용한다 |
| §12.27 | **전 언어** | `ActorLocation`이 현재 Spot lifecycle generation을 보존하지 않아 같은 Spot RID 재사용 뒤 stale membership을 구분할 수 없다 |
| §12.28 | **전 언어** | Actor dispatch를 사용하는 STREAM node가 target MeshName 하나를 명시하는 public 설정과 startup 검증을 제공하지 않는다 |
| §12.29 | **전 언어** | durable Actor transfer store와 Redis participant·active-index·recovery-lease 원자 전이가 없어 process 장애 뒤 transfer를 복구할 수 없다 |
| §12.30 | **C++** | STREAM TLS server 설정이 client 인증서 요구 여부를 받지 않아 공통 mTLS admission을 구성할 수 없다 |
| §12.31 | **전 언어** | Actor transfer counter와 duration이 `mesh_name`, 닫힌 `outcome` label과 실패 terminal을 기록하지 않는다 |
| §12.32 | **전 언어** | 수신 envelope의 알 수 없는 non-JSON content-type을 거부하지 않고 JSON·기본 serializer로 해석하거나 raw payload로 전달한다 |
| §12.33 | **전 언어** | exact interface의 MeshName 중심 `AddRouteMesh`·`addRouteMesh`·`add_route_mesh`와 MeshNode builder가 source·package·sample·E2E에 적용되지 않았고 기존 분리 builder와 production in-memory location helper가 남아 있다 |
| §12.34 | **`.NET`, C++** | 공통 ActorRef는 `NodeRid`·`ActorId`·`Generation` 세 값만 보존하지만 `.NET` binding에는 계약 밖 `IsUnchecked`가 있고 C++은 `actor_type`을 추가로 보존해 snapshot 복원 호출자에게 전달을 요구한다 |
| §13 | **Java, Kotlin** | TicTacToe가 **수동 등록** 대신 package 스캔을 쓴다. 규약상 TicTacToe만 수동 연결 + 수동 등록이다(Node가 참조 구현) |

connector wire의 frame·header·metadata 계약은 세 native 구현에서 해소했다(§10). 수신 message queue
admission은 Java와 이를 공유하는 Kotlin이 §12.1을 아직 충족하지 않는다.

## 3~6. 언어별 구현 차이

각 언어의 정확한 public interface와 그 interface에서 확인한 구현 차이는
`server/languages/<lang>/`의 exact spec에 기록한다. 여러 언어에 공통인 원인과 전체 구현 상태는
이 문서의 §2.7과 §12가 소유한다.

## 7. 문서 및 계약 검증 차이

`.NET` 문서 회귀 검사는 `spec/`(기반)·`spec/server/`와 `spec/server/languages/dotnet/`의 정식 문서를
직접 읽는다. 문서, active unit test, 실제 E2E scenario 또는 script 참조를 찾지 못하면 실패하며,
현재 G0에서는 전체 15개 검사가 성공한다. 이 검사에는 모든 공통 E2E scenario ID가 active
`.NET` fixture source와 all runner 항목에 연결되는지 확인하는 inventory 검증도 포함한다.

Java, Kotlin, Node.js와 C++는 각 언어 G0에서 같은 조건을 검증한다. 이전
`framework/<lang>/spec/` 경로를 읽거나 파일을 열지 못해도 통과하는 검사가 남아 있으면 해당
언어의 구현을 시작하기 전에 정식 경로와 fail-closed 검사로 바꾼다. 이 검증은 public interface
차이가 아니라 정식 계약을 실제로 검사하는지 확인하는 gate다.

## 8. POSD public contract 변경 gap

| 변경 | `.NET` | Java/Kotlin | Node.js | C++ |
|------|--------|-------------|---------|-----|
| Spot messaging handle | `SpotRef` resolver/outbound를 `SpotHandle`로 교체 | ref resolver와 Kotlin extension을 handle 기반으로 교체 | public structural ref를 branded handle로 교체 | `spot_ref_t` 전송 인자를 opaque `spot_handle_t`로 교체 |
| 실행 줄 관리 | request/join/worker yield 타입과 worker callback submit 제거 | blocking `await`, yield와 callback submit 제거 | yield call과 worker callback 제거 | yield call과 worker callback 제거 |
| dispatch 최적화 은닉 | `ZLinkDispatchMode`와 두 mode property 제거 | 같은 mode enum/property 제거 | 현재 `mode` option 제거 | `dispatch_mode_t`와 두 mode property 제거 |
| packet identity | typed call의 `PacketName` 제거 | typed call과 annotation override 제거 | call/payload instance override 제거 | typed call의 `packet_name` 제거 |
| actor Spot 접근 | `GetSpot` overload 제거 | Java getter 제거, Kotlin은 Spot handler 인자 사용 | `getSpot` overload 제거 | 목표 contract가 이미 getter를 요구하지 않음 |
| actor membership | `IsJoined`를 제거하고 nullable `SpotRid`만 사용 | `isJoined`를 제거하고 `Optional<RoutingId>`만 사용 | `isJoined`를 제거하고 optional `spotRid`만 사용 | `is_joined()`를 nullable `spot_rid()`로 교체 |
| actor join 결과 | boolean과 nullable actor를 승인/거절 sealed record로 교체 | result code와 nullable actor를 sealed interface로 교체 | 독립 필드를 discriminated union으로 교체 | result code 구조체를 승인/거절 `variant`로 교체 |
| monitoring event 상태 | kind와 nullable payload 독립 필드를 sealed event로 교체 | location/Spot event를 sealed interface로 교체 | event kind별 discriminated union으로 교체 | event payload도 유효 상태만 표현하는 `variant`인지 검증 |
| manual connection | capability별 `IZLinkEndpointConnections` runtime handle 추가 | 동일한 6개 nominal interface 대신 `ZLinkEndpointConnections` 재사용 | 기존 단일 interface를 builder capability accessor에 연결하고 runtime handle 의미로 정렬 | 역할 builder가 동일 connection 계약을 재사용하도록 검증 |

이 표의 변경은 public contract 변경이므로 compatibility alias를 자동으로 추가하지 않는다.
alias가 같은 복잡성을 계속 노출하면 POSD 목표를 달성하지 못한다. release 정책상 전환 기간이
필요하면 deprecated adapter를 별도 compatibility package에 두고 정식 package root에서는
새 계약만 노출한다.

## 9. 관측·운영 계약 구현 차이

2026-07-11에 확정한 runtime metrics, flow correlation, graceful drain 계약은 현재 plan의 각 언어
G0에서 실제 symbol과 source 위치를 조사한다. 기존 monitoring 또는 shutdown 기능이 일부 있어도 아래
항목 전체가 contract test로 증명되기 전에는 충족으로 판정하지 않는다.

| 영역 | 모든 언어에서 확인하고 구현할 차이 | plan 연결 |
|------|--------------------------------------|-----------|
| flow correlation | UUIDv7 id 자동 생성, 네 origin, 모든 홉과 비동기 문맥 전파·정리, `0xF2` marker codec 일괄 교체 | DN-017~018과 각 언어 G0~G3 |
| runtime metrics | 고정 catalog, server/connector 계기 소유권, 닫힌 label, fanout drop capability, 비활성 최소 비용 | DN-019와 각 언어 G0~G3 |
| graceful drain | typed `Draining` field, readiness/admission 차단, lease 유지, actor handoff와 두 SPOT 정책, 공유 terminal result | DN-020~021과 각 언어 G0~G3 |
| session closing | versioned control, 닫힌 close reason, disconnect event 순서와 bounded 전송 | DN-022와 각 언어 G0~G3 |
| 사용 예제와 배포 검증 | Bingo §17 공개 사용 예제와 Config 11 OBS-A1~C5 전체 | 각 언어 G5~G6 |

Java runtime을 공유하는 Kotlin도 별도 완료 판정을 받는다. Kotlin coroutine 문맥에서 flow가 누출되지
않는지는 `KotlinFlowContextBridgeTest`, drain waiter 취소가 shared drain을 취소하지 않는지는
`KotlinCompletionStageAwaitIntegrationTest`가 검증한다. C++는
framework가 signal handler를 설치하지 않으며 애플리케이션이 소유한 종료 실행 문맥에서 drain을
호출하는 예제를 제공해야 한다.

## 10. Stream Connector wire·검증 계약 차이

[Stream Connector 공통 스펙](stream-connector/32-stream-connector.ko.md)을 정본으로 두고 3개 connector 구현을
대조해 확인한 차이는 2026-07-13에 모두 해소했다. 브라우저 실행 환경 차이는 §4.10이
따로 소유한다.

| # | 항목 | 해소 결과 | 검증 항목 |
|---|------|-----------|-----------|
| 10.1 | **Response/Error packet name 검증 — 폐기(2026-07-14)** | 이름 대조 규칙 자체를 스펙에서 걷어냈다. pending request 매칭은 `request_seq`가 정본이고 reply의 packet name은 참고 값이다 — 이름이 달라도 응답을 버리지 않는다. C++·Node·.NET 커넥터에서 대조를 제거했다 | 이름이 다른 reply도 같은 `request_seq`의 pending request를 정상 완료한다 |
| 10.2 | **Error payload 포맷** | C++도 압축 해제 뒤 UTF-8 JSON object를 읽고 `code`와 `message`가 문자열인지 검증한다 | 올바른 Error object를 읽고 문자열이나 필수 필드가 잘못된 payload를 거부한다 |
| 10.3 | **metadata 1024바이트 한도** | C++ public option을 제거하고 송수신 양쪽에 고정된 1024바이트 한도를 적용한다 | 경계값은 허용하고 한도를 넘은 송수신 metadata는 거부한다 |
| 10.4 | **예약 packet name 범위** | C++는 `$zlink.` prefix만 거부하며 `$application.event` 같은 application 이름은 허용한다 | application 이름은 허용하고 framework 예약 prefix만 거부한다 |
| 10.5 | **수신 메시지 큐 overflow** | `.NET`은 기존 미수신 메시지를 유지하고 새 메시지를 버린 뒤 `ReceivedMessageDropped`를 보고한다 | 큐가 가득 차면 기존 항목을 유지하고 새 항목의 drop을 관찰할 수 있다 |
| 10.6 | **연결 상태 `Created`** | Java에 `CREATED`를 추가하고 첫 연결 시도 전 초기 상태로 사용한다. 연결 시도에 실패한 뒤에는 `DISCONNECTED`로 전환한다 | 최초 연결 전 상태와 연결 실패 뒤 상태를 구분한다 |
| 10.7 | **dispatch error observer의 `FailCaller` 결과** | `.NET`과 Node.js에 `FailCaller` action을 추가하고 reply frame이 만들어지지 않은 local dispatch가 caller를 실패시키며 같은 action을 보고한다 | local request에 reply가 없을 때 호출 실패 action과 원인을 함께 관찰한다 |

10.1과 10.2의 wire 호환성, 10.5와 10.7의 언어별 관찰 결과 차이는 위 구현과
회귀 검사로 같은 계약에 맞췄다.

### 10.7b `FailCaller` action (C++) — 해소

**해소.** C++ `dispatch_error_action_t`는 `reply_error`, `drop`, **`fail_caller`** 세 값을 모두
제공한다. `.NET`과 Node.js는 §10.7과 §4.11에서 해소했다. 세 언어 모두
[Message Flow Tracing §4](server/52-message-flow-tracing.ko.md#4-event-fields)의 action 집합을
충족한다.

### 10.8 dispatch 실패의 로그 수준

**해소(`.NET`).** [channel 메시징 §3.1](server/11-channel-messaging.ko.md)은 **handler 예외를 one-way
경로에서도 Error로 기록**하고, handler 없음·decode 실패·invalid frame은 send는 Warning, publish는
Debug로 구분하도록 규정한다.

`.NET` message flow tracer는 handler 예외를 `Error`, handler 없음·decode 실패·invalid frame을
send에서 `Warning`, publish에서 `Debug`로 기록한다. 별도 로그 파일과 shared logger는 같은
`ResolveLogLevel` 결정을 사용하며, `MessageFlowTracerTests`가 이 구분을 회귀 검사한다.

### 10.9 handler filter의 적용 범위

**정보(설계 결정).** [framework API §8.1](05-framework-api.ko.md#81-handler-filter)이 규정하듯 filter는 **channel
dispatch 경로에만** 적용한다. `.NET`에서 SPOT handler·STREAM session handler·route-mesh handler는
filter 파이프라인을 거치지 않고 handler invocation engine을 직접 호출한다.

**이는 구현 결함이 아니라 현재 계약의 범위다.** SPOT과 session은 각자의 실행 문맥이 소유하는 별도
dispatch이기 때문이다. **filter를 이 경로까지 넓히려면 공개 계약을 먼저 확장해야 한다.**

## 11. 완료 조건

각 항목은 다음 조건을 모두 만족해야 닫을 수 있다.

1. 언어별 public declaration이 정식 interface spec과 일치한다.
2. package 또는 assembly의 실제 export 목록에서 내부 구현 타입이 제거된다.
3. contract test가 전체 타입과 시그니처를 검증한다.
4. 공통 E2E가 같은 기능과 관찰 가능한 결과를 검증한다.
5. 이 문서에서 해당 차이를 제거한다.

## 12. 2026-07-14 기준선 대조에서 확인한 차이

`.NET` framework 구현을 기준선으로 각 언어의 public 표면과 동작을 대조해 확인한 차이다.

**두 종류를 구분한다.** 고치는 방법이 다르기 때문이다.

| 종류 | 뜻 | 고치는 법 |
|------|-----|-----------|
| **미구현** | 계약이 요구하는 표면·동작이 **없다** | 만든다 |
| **결함** | 표면은 **있는데 계약과 다르게 동작한다** | 동작을 바꾼다. 표면 이름·시그니처가 함께 틀린 경우 그것도 바꾼다 |

**결함이 더 위험하다.** 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **그대로 통과한
채 부하가 걸릴 때만 드물게 깨진다.** 아래 표가 결함으로 분류된 항목이다.

| 갭 | 종류 | 무엇이 다른가 |
|---|---|---|
| §12.20 | **해소** | 모든 언어가 reply packet name을 제거하고 sequence로만 correlation한다 |
| §12.21 | **해소** | 모든 언어에서 기본 `async`는 turn을 유지하고 명시적 `yield`만 turn을 반납한다 |
| §12.22 | **C++ 결함 + 미구현** | terminator 이름이 계약과 다르고 blocking 표면이 public이다(결함). turn seam·DI 서버 표면이 없다(미구현) |
| §12.23 | **C++·Java/Kotlin 미구현** | CPU/I/O worker와 terminator는 구현됐지만 worker callback cancellation 인자가 없다 |
| §12.24 | **전 언어 결함** | accepted join에서 location CAS보다 source leave를 먼저 실행하거나 target membership을 먼저 공개한다 |
| §12.27 | **전 언어 미구현** | Actor location record와 Redis codec에 Spot generation이 없다 |
| §12.32 | **전 언어 결함** | 수신 wire content-type과 등록 codec의 일치를 확인하지 않아 알 수 없는 non-JSON payload가 handler까지 도달할 수 있다 |
| §12.33 | **전 언어 미구현** | 10.0.0 exact RouteMesh·MeshNode 등록 표면과 기존 builder 제거가 source·package·sample·E2E에 적용되지 않았다 |
| §12.34 | **`.NET`·C++ 결함** | ActorRef의 공통 세 필드 밖 공개 상태가 남아 있고 C++ snapshot 복원이 Actor type 인자를 요구한다 |

나머지 §12.1~§12.19는 언어별 표면 차이이며, 각 항목이 미구현인지 결함인지를 본문에 적었다.

### 12.1~12.19 언어별 표면 차이

각 언어의 exact spec이 public interface와 언어별 차이를 함께 기록한다. §2.7은 현재 열려 있는
차이를 언어별로 모아 보여 주며, 아래 범위는 그 차이의 분류 기준이다.

| 언어 | 항목 |
|------|------|
| `.NET` | §12.7 |
| Java | §12.1~12.4 · §12.8~12.10 · §12.12·12.13 · §12.15~12.19 |
| Kotlin | §12.3 · §12.14 · §12.19 |
| Node | §12.5 · §12.6 · §12.11 |
| C++ | §12.2 |

§12.20과 §12.21은 모든 언어에서 해소됐다. §12.23은 C++·Java/Kotlin, §12.24는 모든 언어에 남아
있다. 이후 공통 갭은 §2.7과 아래 상세 절을 기준으로 판단한다.

### 12.20 응답 packet name — 해소

`.NET`, Java/Kotlin, Node와 C++ 모두 `Response`·`Error` header에서 packet name을 제거했다. Pending
request는 request sequence로만 맞추고 진단 이름은 원 요청에서 가져온다. C++의
`stream_write_call_t::packet_name(...)`도 공개 표면에서 제거했다. 각 언어 gap 문서의 §12.20 회귀
근거가 이 상태를 고정한다.

### 12.21 C++ yield terminator 부재

**해소.** [04 §1.1](04-async-execution-policy.ko.md)은 request·actor join·worker에 세 terminator를 요구한다.
다섯 언어 모두 기본 async의 turn 유지와 명시적 yield의 turn 반납을 구현했다.

| terminator | 실행 줄 |
|---|---|
| `submit` | 그대로 진행(one-way) |
| **`async`**(기본) | **turn을 유지한다.** 대기 중 같은 Spot의 다른 callback은 시작하지 않는다 |
| **`yield`**(opt-in) | turn을 반납한다. 완료된 continuation은 큐에 다시 들어가 순서대로 재개된다 |

C++도 `call.hpp`와 `worker.hpp`에서 `async()`를 turn 유지로, `yield()`를 turn 반납으로 시작하고
`task.hpp`의 serial turn scheduler가 두 의미를 구분한다. C++ HTTP client 차이는 §12.22, worker
cancellation 차이는 §12.23이 각각 소유한다.

### 12.22 C++ HTTP client가 framework 계약 밖에 있다

**C++ 미충족.** [12 HTTP client](http-client/12-http-client.ko.md)는 HTTP client를 framework 동반
client로 규정하고 terminator·turn seam·서버 등록 표면을 고정한다. `.NET`, Java/Kotlin과 Node는 이
통합을 구현하고 각 언어 gap의 §12.22를 닫았다.

| 항목 | 계약 | 현재 |
|------|------|------|
| terminator | `submit` / `async` / `yield` / callback | C++는 완료 방식 전체를 제공하지 않는다 |
| Spot turn 인지 | `yield`가 turn을 반납한다 | C++는 framework 실행 turn과 연결되지 않는다 |
| 서버 표면 | DI 주입 client(`submit`/`async`/`yield`/callback) | C++에는 서버 등록 표면이 없다 |
| blocking 표면 | 두지 않는다 | C++ `fetch<T>()`가 남아 있다 |

그 결과 **spot handler에서 외부 API를 호출하면 실행 줄이 그대로 막힌다.** actor 입·퇴장 시 외부
데이터를 가져오는 흐름이 room 전체와 timer를 멈춘다 — 이 client가 존재해야 하는 이유가 바로
그 경로인데 표면이 없다.

**고쳐야 할 것:**

- 세 terminator(`submit`/`async`/`yield`)와 callback 완료 경로를 제공한다.
- **turn seam**(execution scheduler 주입점)을 공개 계약으로 둔다. framework가 DI 등록 시 spot
  turn을 아는 scheduler를 꽂는다. C++ HTTP client에 **같은 형태의 API 표면이 이미 있다**
  (`framework_resume_scheduler_t`) — 다만 framework 런타임이 아직 그것을 주입하지 않으므로 표면만
  있고 통합은 검증되지 않았다.
- **서버용 DI 표면**을 신설한다. application이 명명 등록하고 handler가 주입받는다. 정적 팩토리는
  client-side 전용으로 남긴다.
- blocking 언래핑 terminator를 public 표면에서 제거한다.
- **바이너리 의존은 framework → HTTP client 한 방향을 유지한다.**

### 12.23 C++·Java/Kotlin worker cancellation 부재

**C++와 Java/Kotlin 미충족.** [04 §1.2](04-async-execution-policy.ko.md)는 worker를 CPU worker와
I/O worker로 나누고, 둘 다 `async`·`yield` terminator와 cancellation 신호를 갖도록 규정한다.
두 구현 모두 worker 분리와 terminator는 제공한다. C++ callback은 `std::stop_token`을 받지 않고,
Java/Kotlin의 `ZLinkWorkerTask.run()`과 `ZLinkIoWorkerTask.run()`도 cancellation 인자를 받지 않아 timeout,
caller cancellation과 shutdown을 실행 중인 작업에 전달할 수 없다. `.NET`과 Node만 세 요구를 모두
충족한다.

**고쳐야 할 것:**

- 각 언어의 worker callback에 표준 cancellation 표현을 전달하고, 늦은 완료가 이미 확정된 terminal
  결과를 바꾸지 않는 contract test를 둔다.

E2E는 이미 정본을 따른다 — [config-2 SM-A8](../common/e2e/config-2-spot-service.ko.md)과
[config-8 TD-C3~C5](../common/e2e/config-8-execution-turn.ko.md)가 `RunCpuWorker`/`RunIoWorker`를 쓴다.

### 12.24 전 언어 actor join commit 순서

**전 언어 미충족.** [23 §3](server/23-spot-actor.ko.md#3-join-commit)은 admission accept 뒤 location
authority가 expected Actor generation과 membership epoch를 비교해 CAS를 먼저 commit하도록 고정한다.
CAS가 성공한 뒤에만 source `OnLeaveActor`, target membership 공개와 `OnJoinedActor`를 차례로 실행한다.
CAS 실패에서는 source membership을 그대로 유지해야 한다.

현재 구현은 모두 이 commit point를 다른 순서로 둔다.

- `.NET`은 `ZLinkFrameworkActorFacade.cs:56-71`에서 admission accept 뒤 source
  `NotifyActorLeftAfterManagedJoinSpotAsync`를 먼저 완료하고 target commit을 호출한다.
- Java/Kotlin은 `ZLinkActorSpotAdmission.java:245-257`에서 source leave, target membership,
  `OnJoinedActor`, durable location commit 순으로 실행한다.
- Node는 `local-first-actor-join-coordinator.ts:69-116`에서 target admission·membership 처리와 source
  leave를 마친 뒤 `notifyActorJoinedSpot`으로 location을 기록한다.
- C++는 `spot_runtime.hpp:805-867`에서 `commit_actor_left`를 먼저 실행하고 target callback과 route를
  공개한다. location authority CAS를 이 순서의 commit point로 사용하지 않는다.

**고쳐야 할 것 — location authority가 commit 순서를 소유한다:**

1. caller turn에서 target admission을 요청하고 expected Actor generation·membership epoch를 보존한다.
2. accepted reply를 받은 location authority가 target owner와 Spot membership을 CAS commit한다.
3. CAS가 성공한 뒤 source `OnLeaveActor`를 실행한다. CAS가 실패하면 source callback과 membership을
   바꾸지 않는다.
4. target membership을 공개하고 `OnJoinedActor`를 실행한다. callback 실패는 commit 이후 복구 절차로
   처리하며 source로 rollback하지 않는다.
5. 서로 다른 Spot 쌍은 노드 전역 세마포어 없이 병행할 수 있어야 한다.

**E2E:** [config-8 TD-E2](../common/e2e/config-8-execution-turn.ko.md)(user→user join)의 commit marker
순서와 TD-E3(반대 방향 동시 join)이 이 갭의 검증 축이다.

### 12.25 Stream Connector의 근거 없는 count·operation codec 표면

**미충족(`.NET`, Java/Kotlin, C++).** [Stream Connector §5.4](stream-connector/32-stream-connector.ko.md)는
typed payload codec 하나를 connector 생성 option으로 받고 모든 typed operation이 함께 사용하도록
고정한다. [§10.2](stream-connector/32-stream-connector.ko.md)는 push 관측 표면을 `waitFor`,
`expectNone`, `waitForSequence`로 한정한다.

- `.NET`의 `IZlinkStreamConnector.ReceivedCount(string)`와 Java의
  `ZLinkStreamConnector.receivedCount(String)`, Kotlin wrapper의 `receivedCount(String)`는 target
  exact interface에 없는 공개 member다. 부재 검증은 count snapshot이 아니라 `ExpectNone` 계열의
  명시적인 관찰 구간으로 수행해야 한다.
- `.NET` `ZlinkStreamReceiveDispatcher.cs:74-96`은 handler가 하나라도 있으면
  `ZlinkStreamReceivedMessages.Record`를 호출하지 않고 callback을 바로 실행한다. handler-bound send도
  §10.1의 bounded queue admission을 먼저 거쳐야 하며, 인수 뒤 unread 기록에 남기지 않아야 한다.
- C++의 `connector_t::codecs()`와 send/request builder의 `codec(codec_t)`는 codec 결정을 connector
  밖의 operation과 registry 호출로 분산한다. 이 세 member와 `codec_registry_t`를 제거하고
  `connector_options_t::typed_codec` 하나로 정렬해야 한다.

구현 제거와 package API snapshot 갱신은 S8·S9에서 각 언어 source, contract test와 실제 package를
함께 바꾼다. 정식 exact interface는 제거 뒤 목표 표면만 기술한다.

### 12.26 Java/Kotlin route-mesh runtime options 미구현

Java 10.0.0 exact interface는 MeshName과 ChannelName을 함께 받는
`ZLinkRouteMeshRuntimeOptions.channel(meshName, channelName)`과 MeshNode·ChannelName runtime options를
공개 계약으로 고정한다. 현재 Java runtime은 ChannelName만 받는
`ZLinkChannelRuntimeOptions.clientServerChannel(channelName)`과
`ZLinkClientServerChannelRuntimeOptions`를 제공한다. Kotlin은 Java runtime을 그대로 사용하므로 같은
차이가 적용된다.

Java와 Kotlin의 Config 5 RL-B4는 기존 표면으로 weight 0/100의 부하 제외 의미를 검증한다. exact
interface를 구현한 뒤 두 E2E를 새 표면으로 전환하고 MeshName이 다른 같은 ChannelName을 독립적으로
설정하는 contract test를 추가해야 한다.

### 12.27 전 언어 Actor location의 Spot generation 미구현

[Location Runtime §3](server/40-location-runtime.ko.md)과 다섯 언어 exact interface는 Actor location에
현재 Spot의 lifecycle generation을 보존한다. 같은 Spot RID가 종료 뒤 다시 사용되면 이 값으로 이전
membership과 새 membership을 구분한다.

현재 `.NET`, C++, Java/Kotlin과 Node의 public `ActorLocation` record에는 이 필드가 없다. 공식 Redis
codec도 값을 저장하거나 복원할 수 없으므로 같은 RID의 재생성 뒤 stale actor row가 현재 위치처럼
해석될 수 있다.

각 언어는 record, in-memory·Redis codec, location lifecycle과 stale 판정을 함께 갱신하고 다음을
contract test로 고정해야 한다.

- Actor가 Spot에 join할 때 현재 Spot generation을 location row에 기록한다.
- resolve와 transfer admission은 row의 Spot generation과 현재 Spot owner를 함께 검증한다.
- Redis round-trip이 unsigned 64-bit generation을 손실 없이 보존한다.
- 같은 Spot RID를 더 큰 generation으로 다시 만든 뒤 이전 actor row를 stale로 거부한다.

### 12.28 전 언어 STREAM Actor dispatch MeshName 설정 미구현

[Session Actor Dispatch §2·§9](server/31-session-actor-dispatch.ko.md)는 Actor dispatch를 사용하는 STREAM
node가 target MeshName 하나를 등록 시점에 명시하도록 고정한다. endpoint, 첫 번째 MeshNode나 ActorRef에서
이 값을 추론하지 않는다. 같은 process에 MeshNode가 여러 개 있어도 session resolve·bind·dispatch가
선택한 mesh 밖으로 넘어가지 않게 하는 public 경계다.

현재 다섯 언어 모두 이 계약을 충족하지 않는다.

- `.NET`의 `IZLinkStreamNodeBuilder`와 `ZLinkStreamNodeBuilder`에는 `EnableActorDispatch`가 없고
  `ZLinkStreamRuntimeManager.cs:14-35`도 STREAM registration에 MeshName을 전달하지 않는다.
- Java의 `ZLinkStreamNodeBuilder`에는 `enableActorDispatch`가 없으며
  `ZLinkStreamRuntime.java:187-204`는 등록된 Spot node 가운데 첫 항목을 session relay로 선택한다.
- Kotlin은 Java builder와 runtime을 재사용하므로 같은 차이가 적용된다.
- Node의 `ZLinkStreamNodeBuilder`와 `DefaultStreamNodeBuilder`에는 `enableActorDispatch`가 없고 stream
  registration도 MeshName을 보존하지 않는다.
- C++의 `stream_node_options_builder_t`에는 `enable_actor_dispatch`가 없고 stream registration이 relay용
  MeshName을 저장하지 않는다.

각 언어는 exact interface에 고정한 이름으로 설정을 추가하고 다음을 contract test로 검증해야 한다.

- Actor dispatch를 사용하지 않는 STREAM-only host는 MeshNode 없이 시작한다.
- Actor dispatch를 사용하는 STREAM node는 비어 있지 않은 MeshName 하나를 요구한다.
- 같은 이름의 local MeshNode가 없거나 같은 builder에서 두 번 설정하면 startup 설정 오류다.
- 두 STREAM node가 서로 다른 MeshName을 선택하면 resolve·bind·dispatch state를 공유하지 않는다.
- 다른 MeshName의 ActorRef는 bind 또는 dispatch 전에 target 오류로 거부한다.

### 12.29 전 언어 durable Actor transfer store 미구현

[Spot Actor §6](server/23-spot-actor.ko.md#6-failure와-recovery)과
[Redis Location Store §3.1](server/41-location-store-redis.ko.md#31-actor-transfer-authority)는 transfer
participant, source·target identity, expected Actor generation·membership epoch, state와 recovery lease를
durable store에 기록하도록 고정한다. Actor마다 active transfer 하나만 허용하고 prepare·commit·abort·takeover를
Actor row와 원자적으로 전이해야 process 종료 뒤 successor가 복구할 수 있다.

다섯 언어 exact interface는 각각 `IZLinkActorTransferStore`, `actor_transfer_store_t`,
`ZLinkActorTransferStore`를 목표 계약으로 선언한다. 그러나 현재 source에는 이 interface나 구현이 없고,
공식 Redis extension도 location row와 lease만 저장한다. runtime의 pending transfer map과 in-memory handoff
상태는 process가 종료되면 사라지므로 recovery lease takeover를 수행할 수 없다.

각 언어는 store-neutral interface와 공식 Redis 구현을 함께 제공하고 다음을 contract test로 고정해야 한다.

- actor row key는 MeshName과 Actor ID를 UTF-8 byte 길이로 encode하며 delimiter 충돌이 없다.
- `prepare`는 expected generation·epoch와 active transfer 부재를 한 번에 검사한다.
- `commit`은 recovery lease owner가 일치할 때 actor row와 transfer state를 한 transaction에서 바꾼다.
- `abort`는 commit 전 상태만 source로 되돌리고 active index를 조건부 제거한다.
- `takeover`는 만료된 recovery lease와 participant set을 확인해 successor를 한 번만 정한다.
- 같은 actor의 동시 transfer, 늦은 source cleanup과 commit 이후 callback 실패가 committed target을 지우지 않는다.

### 12.30 C++ STREAM TLS client 인증서 요구 설정 미구현

[STREAM Server §7.1](server/30-stream-session.ko.md#71-tls)은 server certificate와 key를 설정할 때 client
인증서 요구 여부도 선택하도록 고정한다. 기본값은 `false`다. C++ exact interface는
`set_tls_server(certificate_path, key_path, require_client_certificate = false)`를 목표 시그니처로 둔다.

현재 C++ `stream_node_options_builder_t::set_tls_server`는 certificate와 key 두 인자만 받고 Core의
`ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT`를 설정하지 않는다. low-level `stream_builder_t`에도 계약 밖
`set_tls_server(certificate, key)`가 남아 있다. Exact interface에서 low-level builder는 `bind`와
`register_session`만 제공한다. `.NET`, Java와 Node는 같은 client 인증서 요구 선택을 이미 제공한다.

C++ node options builder, registration snapshot과 stream runtime에 bool을 보존하고 low-level builder의
TLS 공개 메서드는 제거한다. false에서 일반 TLS client가 연결되며 true에서 인증서 없는 client가 session
생성 전에 거부되는 실제 TLS contract test를 추가해야 한다.

### 12.31 전 언어 Actor transfer metric outcome 미구현

[Runtime Metrics §4](server/51-runtime-metrics.ko.md#4-object와-stream-계기)는
`zlink.actor.transfers`와 `zlink.actor.transfer.duration`에 `mesh_name`과 닫힌
`outcome=activated|aborted|timed_out|shutdown`을 기록하도록 고정한다. Duration은 transfer 시작부터
activation 또는 실패 terminal까지의 시간이며 location commit만으로 성공을 기록하지 않는다.

현재 구현은 성공 경로의 label 없는 값만 기록한다.

- `.NET` `ZLinkRuntimeMetrics.cs:157-167`은 pending count를 시작할 때 기록하고 완료 시 counter와
  histogram에 tag를 전달하지 않는다.
- Java `ZLinkActorRuntime.java:629-633`은 두 계기를 `Map.of()` 빈 label로 기록한다.
- Kotlin은 Java runtime을 공유한다.
- Node `actor-transfer-runtime.ts:146-157`은 commit callback에서 label 없이 count와 duration을 기록한다.
- C++ `spot_runtime.cpp:3126-3128`은 label 없이 counter와 histogram을 기록한다.

각 언어는 transfer operation이 MeshName, 시작 시각과 terminal outcome을 한 context로 소유하게 하고,
activation·abort·timeout·shutdown의 각 terminal에서 정확히 한 번 기록해야 한다. 실패 뒤 성공으로 다시
세거나 local join을 transfer로 세지 않는 contract test와 Config 11 OBS-B2를 label까지 검증하도록 갱신한다.

### 12.32 전 언어 수신 content-type 검증 결함

[Framework API §9](05-framework-api.ko.md#9-codec)은 송신 업무 타입에 맞는 extension이 없으면
JSON을 선택하지만, 수신 envelope가 명시한 non-JSON content-type과 일치하는 codec이 없으면 JSON으로
다시 해석하지 않고 `PayloadDecodeFailed`로 완료하도록 고정한다. 송신 기본값은 수신 wire 선언을
무시하는 허가가 아니다.

현재 구현은 모두 이 경계를 지키지 않는다.

- `.NET` `ZLinkEnvelopeCodec.cs:254-278`은 content-type에 맞는 serializer가 없으면 그대로
  `JsonSerializer.Deserialize`를 호출한다.
- Java/Kotlin `ZLinkChannelMessageDispatcher.java:304-307`은 envelope content-type을 읽지 않고
  packet name과 payload만 분리하며, `ZLinkChannelHandlerInvoker.java:137-203`은 handler 타입으로 고른
  serializer를 사용한다.
- Node `channel-envelope.ts:221-231`은 등록 serializer와 JSON·binary가 아닌 content-type의 payload를
  오류로 끝내지 않고 `Buffer`로 반환한다.
- C++ `envelope_codec.cpp:187-195`은 body를 raw message로 반환하고 handler registry가 content-type과
  무관하게 handler 타입 serializer를 선택한다.

각 언어는 envelope decode 경계에서 content-type을 codec registry와 먼저 대조해야 한다. JSON 또는
등록된 codec만 허용하고, 알 수 없는 non-JSON 값은 handler를 호출하지 않은 채 `PayloadDecodeFailed`로
종료해야 한다. Config 4 RC-B5는 정확한 error kind, 정상 JSON 트래픽의 지속과 handler 미호출을 함께
검증해야 한다.

### 12.33 전 언어 RouteMesh·MeshNode 통합 표면 적용 미구현

[Framework API §3](05-framework-api.ko.md#3-routemesh-등록)과 다섯 언어 exact interface는 물리
MeshName 하나를 `AddRouteMesh`·`addRouteMesh`·`add_route_mesh`로 등록하고 반환된 MeshNode builder가
ChannelName, handler group, node client, manual peer, Spot과 Actor 구성을 소유하도록 고정한다. Production
구성은 공식 location store를 사용하며 in-memory store 선택 helper를 공개 표면에 두지 않는다.

현재 source는 10.0.0 목표 표면으로 전환되지 않았다.

- `.NET` `IZLinkFrameworkOptions`는 `AddClientServerChannel`, `AddRouteMeshChannel`,
  `UseInMemoryLocationStores`, `AddSpotMesh`를 별도 builder로 제공하고 `AddRouteMesh`가 없다.
- Java/Kotlin `ZLinkFrameworkOptions`는 `addClientServerChannel`, `addRouteMeshChannel`, `addSpotMesh`,
  `useInMemoryLocationStores`를 유지하고 `addRouteMesh`가 없다.
- Node `ZLinkFrameworkOptions`는 `addSpotMesh`, `addClientServerChannel`, `addRouteMeshChannel`을 유지하고
  `addRouteMesh`가 없다.
- C++ `framework_options_t::add_route_mesh(channel_name)`은 `route_mesh_channel_builder_t`를 반환하며
  target의 `mesh_node_builder_t add_route_mesh(mesh_name)`과 다르다. 기존 client-server channel과 SpotMesh
  등록도 분리돼 있다.

S8은 다섯 언어 source와 package public declaration을 exact interface로 바꾸고 old builder와 production
in-memory location helper를 제거한다. S9는 guide·sample·E2E를 통합 MeshNode 표면으로 옮긴다. 실행 상태와
gate 증거는 execution ledger만 소유하며, 이 절과 언어별 문서는 현재 구현 차이와 적용 범위만 추적한다.

### 12.34 `.NET`·C++ ActorRef 필드 집합 불일치

[상호작용 모델 §7](02-interaction-model.ko.md#7-spot과-actor)과
[Actor 모델 §2](server/22-actor-model.ko.md#2-identity와-상태-축)는 `ActorRef`를 owner node의 `NodeRid`,
논리 `ActorId`, 현재 `Generation` 세 값으로 고정한다. endpoint, 내부 frame, location row와 Actor type은
참조에 포함하지 않는다. `ActorRefSnapshot`도 같은 세 값만 보존하고 별도 인자 없이 참조로 복원한다.

Java/Kotlin과 Node source는 이 필드 집합을 따른다. `.NET` binding의 값 타입도 세 값을 보존하지만
계약 밖 `IsUnchecked` 공개 property를 추가로 노출한다. C++ `actor_ref_t`는 `actor_type` 필드와 accessor를
추가로 노출하고, `actor_ref_snapshot_t::to_actor_ref(actor_type)` 호출자가 snapshot에 없는 type을 다시
주입해야 한다.

S7은 `.NET`의 `IsUnchecked`를 공개 표면에서 제거한다. C++은 `actor_ref_t`의 `actor_type`과 accessor를
제거하고 snapshot 복원을 인자 없는 `to_actor_ref()`로 바꾼다. Actor handler 선택에 필요한 Actor type은
Actor manager와 runtime registry가 소유하며 application의 참조 복원 호출자에게 전달하지 않는다.

### 12.35 C++ Actor lifetime 중 generation 변경

**C++ 미충족.** [Spot과 Actor membership §1](server/23-spot-actor.ko.md#1-identity와-authority)은 Actor
generation을 생성 성공 시 해당 Actor lifetime의 값으로 확정하며 destroy까지 변경하지 않도록 고정한다.
같은 MeshNode의 Spot 이동과 다른 MeshNode로의 transfer는 source와 target에서 같은 Actor generation을
사용하고, 성공한 location commit에서 membership epoch만 정확히 1 증가해야 한다.

C++ runtime은 같은 MeshNode의 이동과 remote transfer target `ActorRef`를 만들 때 기존 generation에 1을
더한다(`spot_runtime.cpp:1227-1229`, `2662-2665`, `3206-3209`, `3406-3409`). C++ ST-F2와 contract
gate도 이 값을 요구한다. Java testkit의 fake backend와 `.NET` 단위 테스트의 remote join mock에도 같은
증가 방식이 남아 있어 contract test가 잘못된 값을 정상으로 받아들일 수 있다.

**고쳐야 할 것:**

- C++에서 생성 시 확정한 Actor generation이 destroy까지 변경되지 않게 하고, join·transfer target
  `ActorRef`가 source Actor ID와 generation을 그대로 사용하게 한다.
- C++ E2E와 contract test, Java testkit과 `.NET` test double에서 이동 전후 generation 동일성을 검증한다.
- 성공한 이동에서는 owner Node RID와 membership epoch만 바뀌고, destroy 뒤 새 Actor 생성에서만 다음
  Actor generation을 할당하는지 언어별 contract test로 확인한다.

### 12.36 .NET call builder의 TrySubmit·node-direct metadata

**.NET 부분 충족.** [.NET handler interfaces §2](server/languages/dotnet/02-handler-interfaces.ko.md#2-공통-metadata와-call)와
[.NET RouteMesh §6](server/languages/dotnet/05-route-mesh.ko.md#6-메시징-metadata)은 모든 call builder에
`IZLinkMetadataCall<TSelf>` metadata overload와 `TrySubmit()`(비대기 admission 판정)을 요구한다.

현재 .NET 구현은 call 표면을 exact 형상으로 정렬했고 Spot direct send/request와 Logical Multicast
publish는 metadata를 canonical frame으로 Core까지 전달한다. 다음 두 조각은 미충족으로 남아
`NotSupportedException`으로 명시 실패한다.

- **mesh 경로 TrySubmit**: mesh submit은 node별 비동기 submit queue를 거치므로 동기 admission 판정이
  없다. queue에 들어간 message를 `Backpressured`로 보고하면 재시도 시 중복 전송이 생기므로, 동기
  seam 직행 경로가 생길 때까지 `TrySubmit()`은 명시적으로 거부한다. classic dealer plane의
  `TrySubmit()`은 구현되어 있다.
- **node-direct metadata**: `IZLinkRouteClient.SendToNode/RequestToNode`는
  router seam(`IZLinkBackendRouterSocket`) 경유라 metadata 인자가 아직 관통되지 않았다. spot
  direct·publish와 동일한 seam 확장이 필요하다. mesh ChannelName select-one 계열
  (`IZLinkRouteClient.SendToChannel/RequestToChannel`)은 entry spot seam으로 metadata를
  관통하고 send `TrySubmit()`을 one-shot DONTWAIT로 구현했다(2026-07-19).

**고쳐야 할 것:**

- mesh submit 경로에 DONTWAIT 동기 admission 판정을 제공하는 seam 직행 `TrySubmit` 경로를 추가한다.
- router seam send/request에 metadata 인자를 관통시키고 node-direct call의
  `Metadata(...)`를 실제 전송으로 연결한다.
- 두 조각이 착지하면 `NotSupportedException` fail-fast를 제거하고 exact interface 회귀 테스트로
  판정한다.

### 12.37 .NET IZLinkRouteMeshRuntime snapshot의 Core 미노출 필드

**.NET 부분 충족.** [.NET RouteMesh §8](server/languages/dotnet/05-route-mesh.ko.md#8-runtime-snapshot-event와-drain)과
[Runtime monitoring](server/50-runtime-monitoring.ko.md)은 MeshNode snapshot에 peer별 ChannelName
set, channel별 ready member 수, Logical Multicast admission 누계(backpressure·remote/local
snapshot·admitted·dropped), drain seal 상태와 pending transfer·STREAM barrier
수를 요구한다.

현재 .NET `IZLinkRouteMeshRuntime` 구현은 exact interface와 event stream(polling 파생)을
제공하지만 Core `zlink_mesh_node_status_t`/`zlink_mesh_peer_entry_t`가 위 값을 노출하지 않아
해당 field는 빈 값(빈 목록·0·false)으로 채운다. channel ready member 수는 admitted peer 수
기반 근사값이다. per-mesh drain은 host 공유 drain에 위임한다(단일 mesh host에서는 동일 의미).

**고쳐야 할 것:**

- Core status/peer 표면이 위 값을 노출하면 binding·seam을 거쳐 실측값으로 교체한다.
- 다중 mesh host의 선택적 drain이 필요해지면 mesh 단위 drain seam을 추가한다.

## 13. 샘플 연결·등록 축 준수 현황

[샘플 규약](../common/sample/README.ko.md)은 두 축을 고정한다.

- **TicTacToe만** 수동 endpoint 연결 + **수동 handler 등록**을 사용한다. handler 자체는 다른
  샘플과 같이 attribute·annotation·decorator로 **선언**하되, **assembly·module 스캔에 의한 자동
  등록을 쓰지 않고** 구성 코드에서 그 handler를 직접 등록한다. 이 대조를 보여 주는 것이
  TicTacToe의 목적이다.
- **나머지 정본 샘플**은 전부 location store 자동 연결 + **자동 등록**(스캔)을 사용한다.
- **C++ 샘플은 예외**다. runtime reflection scanner가 없으므로 모든 샘플이 compile-time 명시
  등록을 쓴다([C++ exact interface §8](server/languages/cpp/02-framework-interfaces.ko.md#8-handler-registry)).
  C++은 이 표의 위반 대상이 아니다.

**판정 기준은 "스캔을 쓰는가"다.** handler에 annotation을 붙였는지가 아니라, 등록이 스캔으로
일어나는지 명시 호출로 일어나는지가 기준이다.

이 축은 framework 구현이 아니라 **샘플이 지켜야 하는 규약**이다. `.NET` 구현이 기준선인 다른
항목과 달리, 여기서는 규약이 정본이고 샘플 코드가 따라와야 한다.

### 13.1 등록 축 현황 (2026-07-15 재검증)

| 샘플 | 규약 | `.NET` | Java/Kotlin | Node | C++ |
|------|------|:---:|:---:|:---:|:---:|
| **TicTacToe** | **수동 등록** | O | **X** 스캔 | **O** (참조 구현) | O (언어 예외) |
| Bingo | 자동 등록 | O | O | O | O (언어 예외) |
| SupportChat | 자동 등록 | O | O | O | O (언어 예외) |
| DeliveryDispatch | 자동 등록 | O | O | O | O (언어 예외) |
| ShoppingMall | 자동 등록 | O | O | O | O (언어 예외) |
| GameQuest | 자동 등록 | O | O | O | O (언어 예외) |

**Node TicTacToe가 이 축의 참조 구현이다.** handler를 decorator로 선언하고, spot의 `configure()`에서
`context.handlers.addActorPacket(...)` / `addSubscribe(..., topic)`으로 직접 등록한다. 스캔 호출이
하나도 없다. 다른 언어는 이 형태로 맞춘다.

**미충족 내용:**

- **Java/Kotlin TicTacToe** — `addHandlersFromPackageOf(...)`로 package를 스캔한다. annotation
  선언은 그대로 두되 스캔 호출을 빼고 handler를 직접 등록해야 한다.

### 13.3 샘플 계약 갭 — 발견 기록과 현재 상태

C++ 샘플 감사에서 시작해 `.NET` 기준선에서도 확인한 항목이다. 아래 현황은 발견 당시 설명을
현재 구현 상태에 맞춰 갱신한다. `.NET`에서 해소한 항목은 다른 언어의 완료를 뜻하지 않는다.

| ID | 계약 | 현황 |
|----|------|------|
| **SMP-X1** (미구현) | [bingo README:452-507,833-847](../common/sample/bingo/README.ko.md): room Spot의 actor join이 Api 서버에서 **`GetPlayerRecordReq/Res`**로 전적을 조회하고, leave가 **`ReportBingoResultReq/Res`**로 기록한다. 둘 다 **`yield`** 터미네이터를 쓴다([공통 샘플 §Spot 실행 turn](../common/sample/README.ko.md)). `BingoPlayerState`에 **`Wins`·`Losses`** 필드가 있다 | `.NET`은 Api store·조회/보고 handler와 room lifecycle의 `Yield` 호출, client 전적 검증을 구현해 해소했다. C++ 등 다른 언어는 각 gap 문서에서 추적한다. |
| **SMP-X2** (결함) | [공통 샘플 §Client self-check:353](../common/sample/README.ko.md): **"인증 요청에 사용한 token 또는 actor id가 인증 응답의 actor id와 일치한다"**는 모든 샘플 client의 **첫 번째 필수 검증**이다. [gamequest:596](../common/sample/event/gamequest.ko.md)도 join의 "bind 검증"을 요구한다 | `JoinSessionRes`에 `PlayerId`를 추가하고 `.NET` client가 Alice와 Bob의 요청값을 각각 대조한다. actor routing 정보는 client에 노출하지 않는다. C++ 등 다른 언어는 같은 계약 반영이 필요하다. |
| **SMP-X3** (결함) | [deliverydispatch:681-685](../common/sample/deliverydispatch/README.ko.md): 배송 상태가 `Assigned → Accepted → PickedUp → Delivered` **순서대로 도착**한다 | `.NET` client는 connector의 `WaitForSequence<DeliveryStatusNotify>()`로 성공·재배정 순서를 직접 검증한다. 서버가 계산한 bool에 순서 판정을 위임하지 않는다. C++은 별도 추적한다. |
| **SMP-X4** (결함) | [deliverydispatch §메시지 계약:306-320](../common/sample/deliverydispatch/README.ko.md)이 wire 메시지를 고정한다. | courier client→session→actor의 `CourierDecisionMsg`와 actor→dispatch의 `OfferDeliveryResultMsg`를 서로 다른 책임의 one-way message로 문서화했다. `.NET`의 계약 밖 `ServerAssertionReq/Res` wire는 제거됐다. |
| **SMP-X5** (결함) | [gamequest §11.2:503-509](../common/sample/event/gamequest.ko.md): entry→owner hop은 **flat one-way `GameplayMsg`** = `{EventId, PlayerId, Type, Payload: bytes, OccurredAtUnixMs}` | `.NET`은 flat `GameplayMsg`를 channel one-way send로 전달하고 versioned projection을 유지해 해소했다. C++ 등 다른 언어는 각 gap 문서에서 추적한다. |

**작업 순서:** 각 언어는 §12.21의 `yield` terminator를 먼저
구현한 뒤 SMP-X1을 채운다. `.NET`은 이 순서로 완료했다. 순서를 뒤집으면 `yield` 없이 `async`로
흉내 내게 되어 **샘플이 보여 주려던 대비 자체가 사라진다.**

### 13.4 계약 결정 (2026-07-15 확정) — 각 언어는 이 결정대로 구현한다

**아래 네 결정은 확정됐다. 각 언어 에이전트는 재론하지 말고 그대로 구현한다.**
근거는 POSD(호출자 복잡성·정보 은닉·거짓 계약 금지)와 공통 spec이다. **"다른 언어에 있으니
그게 맞다"는 근거가 아니다** — 아래 여러 항목에서 기준선인 `.NET`이 틀린 쪽이었다.

| 결정 | 확정 내용 | 각 언어가 할 일 |
|------|-----------|-----------------|
| **D1. DeliveryStatus 표현** (SMP-X4 일부) | **wire에 이름 있는 문자열**(`"Assigned"` 등). 정수 ordinal 금지 — 값 추가 시 순서가 밀려 교차 언어를 조용히 깬다 | `.NET`: C# enum에 문자열 컨버터 등록. **나머지 언어는 이미 맞음** |
| **D2. `BindCourierSessionRes.Actor`** (SMP-X4 일부) | **framework의 `ActorRefSnapshot` 전체**(`nodeRid`+`actorId`+`generation`). 샘플이 자기 축약 타입을 정의하지 않는다 | `.NET`: `{nodeRid}`만 담은 `CourierActorBindingSnapshot`을 제거하고 framework 타입 사용. 나머지 확인 |
| **D3. `DeliveryStatusChangedReq.CustomerId`** (SMP-X4 일부) | **계약에 `CustomerId`를 추가**한다(문서 수정 완료, deliverydispatch:326). 이 필드가 없어서 C++이 `"customer-1"`을 하드코딩한 버그(SMP-CP-55)가 났다 | 모든 언어: `DeliveryStatusChangedReq`에 `CustomerId` 추가, Tracking이 그걸로 고객 actor를 찾게 한다. 하드코딩 제거 |
| **D4. GameQuest wire** (SMP-X5) | **전부 문서(gamequest.ko.md)대로.** `QuestProgress.Version: int64` **유지**, 필드 이름 **`LastSourceEventId`**, `GameplayMsg`는 **one-way** | `.NET`: `Version` 추가, `LastEventId`→`LastSourceEventId`, `ApplyGameplayEventReq`(request)→one-way `GameplayMsg`. C++: envelope wrapper를 문서의 flat `GameplayMsg`로. 계약에 없는 request 7종·event 이름 drift 정리 |
| **D5. 원격 actor 생성·join** (E2E-DN-03 / Config 10) | **새 public API를 만들지 않는다.** 원격 노드에서 actor를 생성·join하는 기능은 **의도적으로 제거됐다** — 모호한 소유권 상황을 막으려고 **actor 생성·join 책임을 그 actor를 소유하는 노드에 고정**했다. transfer는 기존 drain/handoff 계약([54](server/54-graceful-drain-handoff.ko.md))으로만 표현한다 | 각 언어: Config 10에서 "분리된 transfer controller가 원격 actor를 spawn"하는 형태를 **만들지 않는다.** e2e 기대치를 "노드가 자기 actor를 소유·생성한다"에 맞춘다. private API·raw frame 우회 금지 |

**D1~D4는 wire 계약이라 C++ gap 문서 §0.8의 1단계였지만,
이제 결정이 내려졌으므로 각 언어의 2단계(구현) 작업이다.** 각 언어 gap 문서의 해당 행에서 이 결정을
참조해 닫는다.

## 14. 문서 소유권 중복 (스펙 트리 정리 후 잔여)

spec 트리를 패키지 폴더로 나눈 뒤 드러난 **같은 계약을 두 문서가 소유하는** 자리다. 계약이
어긋나서 생긴 문제가 아니라, **어긋날 수 있는 구조**가 남아 있다는 문제다.

**지금 바로 뜯어내지 않는다.** 아래 카탈로그들은 언어별 회귀 테스트가 내용을 고정하고 있어
문서만 먼저 고치면 게이트가 깨진다. **구현 갭을 닫는 커밋에서 문서와 테스트를 함께 옮긴다.**

| 중복 | 어디 | 누가 이겨야 하나 |
|------|------|------------------|
| client connector 표면이 **서버 언어 카탈로그**에도 들어 있다 | `server/languages/dotnet/02-handler-interfaces.ko.md`, `server/languages/java/02-handler-interfaces.ko.md` | **connector 언어 문서**([stream-connector/languages/](stream-connector/README.ko.md)). 서버 카탈로그에서 뺀다 |
| Kotlin **connector coroutine·`Flow` 표면**이 서버 폴더에 있다 | `server/languages/kotlin/02-handler-interfaces.ko.md` | `stream-connector/languages/kotlin/`을 새로 만들어 옮긴다 |
| connector **wire header 필드**를 서버 관측 문서가 함께 정의한다 | `server/52-message-flow-tracing.ko.md`, `server/53-flow-correlation.ko.md` | **[32](stream-connector/32-stream-connector.ko.md)가 wire를 소유**한다. 52/53은 추적 **의미**만 갖고 wire는 32를 참조한다 |
| `session-closing` **인코딩과 client 디코딩**을 서버 drain 문서가 함께 정의한다 | `server/54-graceful-drain-handoff.ko.md` | **32가 wire와 connector 동작을 소유**한다. 54는 **언제·왜 보내는가**만 갖는다 |
| connector **메트릭**(`zlink.stream.reconnects`)을 서버 메트릭 문서가 정의한다 | `server/51-runtime-metrics.ko.md` | connector가 emit하는 신호는 **32**로 옮긴다. 51은 서버가 emit하는 것만 갖는다 |

**판정 기준은 "누가 그 바이트를 만드는가"다.** connector가 생성·인코딩하는 것은 32가 소유하고,
서버가 관측·해석하는 의미만 5x가 갖는다. 지금은 두 문서가 같은 header layout을 각각 적고 있어,
한쪽만 고치면 조용히 갈라진다.

## 15. 구현 감사 — 스펙과 코드를 직접 대조해 발굴한 갭

§12는 **언어 간 표면 대조**로 찾은 차이다. 이 절은 다르다 — **스펙 문장 하나하나를 코드에서
찾아 읽고** 어긋난 자리를 기록했다. 그래서 **기준선인 `.NET`에서도 갭이 나왔다.** 다른 언어를
`.NET`에 맞추는 것만으로는 잡히지 않는 것들이다.

**모든 항목은 코드 인용으로 뒷받침한다.** 근거 없는 추정은 싣지 않는다.
**상세는 언어별 갭 문서(§16)가 소유한다.**

### 15.1 대조한 축

| 라운드 | 무엇을 물었나 | 범위 |
|---|---|---|
| **1** | 코드가 스펙대로 하는가 | 02 · 03 · 05 · 20~24 · 30 · 31 · 40 · 41 · 54 |
| **2** | 코드가 스펙대로 하는가 | 00 · 10 · 11 · 25 · 50~53 · 12(HTTP client) · 32(connector) |
| **3** | **코드가 스펙이 허용하지 않는 걸 하는가** + Redis store 대조 + 경합 경로 | 전 스펙 · 41 · SPOT/actor 런타임 |
| **4** | **샘플과 e2e가 자기 문서대로 하는가** | 공통 샘플 6종 · e2e config 11종 × 5개 언어 |

**라운드 3이 질문을 뒤집었더니 또 나왔다.** 같은 문서를 한 번 훑었다는 것은 **비어 있음의 증거가
아니다.** 감사는 **새 갭이 나오지 않을 때까지** 반복한다.

### 15.2 라운드별 결과 (2026-07-14)

| 언어 | R1~R3 (framework) | R4 (샘플·e2e) | 합계 |
|------|-------------------|---------------|------|
| `.NET` (기준선) | 18 | 17 | **35** |
| Java | 33 | 18 | **51** |
| Kotlin | Java 공유 | **10 (고유)** | — |
| Node / TypeScript | 33 | 15 | **48** |
| C++ | 40 | **104** | **144** |

**기준선에서 18건이 나온 것이 이 감사의 가장 큰 소득이다.** `.NET`을 정본으로 삼아 다른
언어를 맞추는 방식으로는 이 18건을 **검출할 수 없다.**

**C++이 가장 많다.** 레퍼런스 구현인데 그렇다.

**라운드 3에서 새로 드러난 부류가 둘이다.**

- **경합(race).** 지금까지 중 가장 잡기 어렵다 — 테스트가 통과하고 부하가 걸릴 때만 깨진다.
  spot close가 `.NET`·Java·Node에서 **check-then-act**이고(IMP-X12), C++은 같은 계약을 **락 두 개가
  서로 안 맞아서** 깬다(IMP-CP-34). **B1을 유일하게 제대로 하는 게 C++이고, B2를 유일하게 틀리는
  것도 C++이다.**
- **관찰할 수 없는 no-op.** 값을 받아 **검증한 뒤 적용하지 않는 option**이다. application은 설정이
  적용됐다는 결과를 받는다.
  `.NET`은 `Linger`를 수락해 놓고 소켓엔 **0을 강제**하고(IMP-DN-14), Java의
  `addForwardedMetadataKey`·C++의 `unhandled_dispatch_options_t`·Node의 `minThreads`가 모두
  같은 결함이다.

### 15.3 교차 언어 — 같은 결함이 여러 구현에 있다

| ID | 결함 | 어디 |
|----|------|------|
| **IMP-X1** | **해소.** pending actor row(`ActorRef` 비어 있음)는 [40 §2.3](server/40-location-runtime.ko.md)에 따라 모든 언어에서 resolve miss로 처리한다 | 해당 없음 |
| **IMP-X2** | **location store 상태 event source가 없다.** [50 §3](server/50-runtime-monitoring.ko.md#3-event-identifiers)의 `zlink.runtime.location.store_changed`와 `not_configured`, `ready`, `degraded`, `stopped` 상태를 게시하지 않는다 | Java · C++ |
| **IMP-X3** | **해소.** [20 §8](server/20-spot-messaging.ko.md)·[30 §7.2](server/30-stream-session.ko.md)의 설정 오류는 모든 언어에서 host 시작 전에 거부한다 | 해당 없음 |
| **IMP-X4** | **해소(감사 근거 오류).** 정식 spec은 location store read별 5초 상한을 고정하지 않는다. Drain은 [54 §3](server/54-graceful-drain-handoff.ko.md)의 전체 deadline, location runtime은 [40 §2.4](server/40-location-runtime.ko.md)의 owner lease renew timeout을 사용한다 | 해당 없음 |
| **IMP-X5** | **message-flow 관측자가 로그 모드에 묶여 침묵한다.** [52 §3](server/52-message-flow-tracing.ko.md)은 "관측자는 모드와 무관하게, `off`여도 발화한다"고 요구한다. ⇒ OTel로 흘리려고 관측자를 달고 로그를 끄면 **아무것도 안 온다** | Java · Node · C++ (`.NET`만 올바름) |
| **IMP-X6** | **`origin=lifecycle`을 생성하지 않는다.** [53 §4.2](server/53-flow-correlation.ko.md). ⇒ drain이 유발한 트래픽을 application 트래픽과 **구분할 수 없다** | Java · Node · C++ |
| **IMP-X7** | **해소.** 압축을 사용하면 모든 connector가 [32 §4.7](stream-connector/32-stream-connector.ko.md)에 따라 압축된 wire payload 크기에 send 한도를 적용한다 | 해당 없음 |
| **IMP-X8** | **수동 endpoint가 그 역할의 자동 연결 reconcile을 끄지 않는다.** [10 §5.2](server/10-channel-topology.ko.md). 따라서 수동 지정 이외의 store peer까지 연결하고 round-robin 대상으로 사용한다 | Java |
| **IMP-X9** | **해소.** proxy 자격증명은 대상 서버 요청에 전달하지 않고 필요한 proxy handshake에만 사용한다 | 해당 없음 |
| **IMP-X10** | **해소.** SPOT timer 등록 오류는 모든 언어에서 [25 §4.1](server/25-stage-wrapper-on-spot.ko.md)에 따라 startup에 거부한다 | 해당 없음 |
| **IMP-X11** | **해소.** `fanout.received`의 topic label은 모든 언어에서 등록 시점의 닫힌 집합으로 제한한다 | 해당 없음 |
| **IMP-X12** | **actor가 존재하는 Spot을 닫을 수 있다 — check-then-act 경합.** [21 §close](server/21-mesh-node.ko.md)는 "actor가 남아 있는 user Spot은 종료하지 않고 실패를 반환한다". 따라서 `OnLeaveActor`가 실행되지 않고 actor의 location row가 **해제된 Spot을 가리킨다** | Java |
| **IMP-X13** | **해소.** server는 ingress에서 `correlation_id`를 만들지 않고 client가 보낸 값을 그대로 전달한다 | 해당 없음 |
| **IMP-X14** | **`listPageSize`(기본 1000)를 읽는 곳이 없다.** 내부 기본값이 1000이 아니라 **무한**이라 모든 목록 조회가 **O(N) 전체 읽기**다 | C++ |
| **IMP-X15** | **해소.** 모든 언어가 [40 §2.4](server/40-location-runtime.ko.md)의 `storeFailureGrace` fail-static 유예를 적용한다 | 해당 없음 |
| **IMP-X16** | **`includeNativeDiagnostics`를 읽는 곳이 없다.** 다른 관련 option은 모두 적용된다 | Java |
| **IMP-X17** | **해소.** 첫 `GetOrCreate` 호출자의 취소는 같은 Spot을 기다리는 다른 호출자의 공유 생성을 취소하지 않는다 | 해당 없음 |
| **IMP-X18** | **해소.** 다섯 언어의 Redis fixture 시험이 정본 fixture의 전체 key와 byte 값을 비교한다 | 해당 없음 |

### 15.4 라운드 4가 무너뜨린 것 — **"통과했다"는 기록은 통과의 증거가 아니다**

라운드 4는 샘플과 e2e를 봤다. 그리고 **이 갭 문서 자신이 세 곳에서 거짓이었다는 것**을 드러냈다.

| 이 문서가 적었던 것 | 실제 |
|---|---|
| §13.2 "**연결 축은 규약과 일치한다**" | Java 샘플에 TicTacToe 밖 수동 연결이 **29곳**. `.NET`만 보고 판정했다 |
| 과거 Node 점검 기록의 "Node Config 11 ObservabilityOps runner는 **OBS-A1~C5 evidence와 함께 통과했다**" | 그 디렉토리엔 **e2e 앱이 없다.** 시나리오를 `echo "$scenario … PASS"`로 통과시킨다 |
| 각 언어 `feature-map.ko.md`의 **"구현 100%"** | C++ `ObservabilityOps` map은 **자기 runner가 PENDING이라 찍는 행**을 "구현"으로 적는다. `ToActorMessaging` map은 대역인 TA-A1~A4를 `implemented`로 적는다 |

**실패할 수 없는 검증이 있다.** Node의 probe 서버는 클라이언트가 단언할 `serviceRole`·`state`를
**리터럴로 만들어 낸다** — `serviceRole === Router && state === Ready`는 항상 참이다.

**약한 gate가 실제 버그를 가린다.** C++ Bingo는 **두 번째 player에게 게임 시작 notify를 보내지 않는다**
(제외 필터에 그 player를 넣는다). 클라이언트 검증이 "**두** player가 기다린다"를 제대로 단언하지
않아 아무도 몰랐다.

**그래서 규칙을 하나 세운다 — 완료 표시는 그 자체로 증거가 아니다.** feature-map·이전 라운드의
"해소" 기록·"통과" 로그는 **재검증 대상**이지 통과의 근거가 아니다.

#### e2e가 갭을 "못 잡는" 게 아니라, **잡을 수 없게 배치돼 있다**

C++ 감사가 더 깊이 팠더니 이게 나왔다. **버그와 그것을 놓치는 게이트가 정확히 같은 자리에서 만난다.**

| 갭 | 그것을 검증해야 할 e2e/게이트 | 왜 못 잡나 |
|---|---|---|
| **C++ Bingo가 2번째 player에게 start notify를 안 보낸다** | Bingo 클라이언트 게이트 | 게이트가 **client1에만** start 대기를 건다. client2는 아예 안 기다리고, `status == Running`도 안 본다. `.NET`은 양쪽에 걸고 둘 다 단언한다 |
| **IMP-CP-06** (store 장애 유예 없음) | e2e Config 6 | 장애를 `docker pause`로만 만든다(문서는 stop/restart 요구). 따라서 **IMP-CP-06의 발생 조건이 만들어지지 않는다** |
| **IMP-CP-35** (runtime에 lease join 없음) | e2e Config 6 | stale row 제외를 **Redis 확장이 대신 해 준다.** ⇒ framework가 lease를 join하든 말든 **초록이다** |
| **IMP-CP-13** (모니터링이 diff 없이 매 tick 발행) | e2e `MON-A2` | **그 결함 덕분에** 통과한다 |
| **IMP-CP-18** (폴백 로그가 `phase=` 대신 `outcome=`) | e2e `OBS-A2` | C++ 전용 토큰을 **오히려 못박는다** |

**그래서 규칙 하나를 더 세운다 — 구현을 고친 뒤 해당 e2e가 여전히 통과한다면 그 e2e가 틀린 것이다.**
고쳤는지 확인하려면 **먼저 그 e2e가 실패하는지부터 봐야 한다.**

### 15.5 계약 설계 결함 — 타입이 실수를 막아 주지 못한다

**기준선은 제대로 했다. 세 언어가 그 안전장치를 풀었고, 그중 하나가 실제로 버그를 냈다.**

actor join의 결과는 **수락 아니면 거절**이다. 계약([23 §3.3](server/23-spot-actor.ko.md))상 거절된
join의 reply를 성공 응답으로 되돌려 주면 안 된다. **그 규칙을 타입으로 강제할 수 있다** —
거절 대안에서 reply를 꺼내는 코드가 **컴파일되지 않게** 하면 된다.

| 언어 | 타입 모양 | 분기 없이 reply 접근이 컴파일되나 | 샘플 |
|------|-----------|-----------------------------------|------|
| **`.NET`** | `abstract record`가 **`Reply`를 노출하지 않는다.** `Accepted`/`Rejected`만 갖는다 | **아니오** — 패턴 매칭이 강제된다 | 안전 |
| Java | `sealed interface`가 **`TReply reply()`를 선언한다** | **예** — `result.reply()`가 분기 없이 컴파일된다 | 샘플은 `instanceof`로 분기한다(**잠재 위험**) |
| Kotlin | Java 타입을 공유한다 | 예 | 샘플은 `as?`로 분기한다 |
| Node | union의 **양쪽 갈래에 `reply`가 있다** | **예** — TS가 공통 속성 접근을 허용한다 | 미확인 |
| **C++** | `accepted_t`·`rejected_t` **양쪽 struct에 `.reply`가 있다** | **예** — 제네릭 람다가 양쪽에 컴파일된다 | **버그** |

**C++에서 실제로 터졌다.**

```cpp
std::visit ([] (const auto &value) { return value.reply; }, joined);   // 양쪽에 컴파일된다
```

Bingo와 TicTacToe의 join handler가 이 형태라 **거절된 join을 정상 성공 응답으로 클라이언트에
보낸다.** 같은 샘플의 다른 handler 3곳은 `get_if`로 제대로
분기한다 — **일관성이 없다는 것은 타입이 실수를 막아 주지 못한다는 뜻이다.**

**고쳐야 할 것:** `.NET`의 모양을 정본으로 삼는다. **공통 상위 타입에서 reply 접근자를 없앤다.**
Java는 `sealed interface`에서 `reply()` 선언을 빼고, Node는 union 갈래의 이름을 다르게 하거나
거절 갈래가 reply 대신 **거절 사유**를 들게 한다. C++은 `rejected_t`의 멤버 이름을 바꾼다.

**이 항목이 감사의 방법론을 하나 보여 준다** — 동작이 아니라 **타입의 모양을 언어 간에 대조**하면
"아직 안 터졌지만 터질 수 있는 것"이 보인다.

> **예측이 적중했다.** 위 문단은 원래 "Java는 지금은 멀쩡하지만 다음 사람이 C++과 같은 실수를
> 하는 것을 막지 못한다"였다. **이미 그 실수가 있었다.** Java TicTacToe의
> `PlayActorJoinGameHandler`가 `joined.reply()`를 **분기 없이** 부르고, 심지어
> `actor.joinGame(roomId)`를 **그 앞에서 커밋**한다.
> 거절된 join이면 actor의 게임 소속은 이미 커밋됐고, `Rejected(null).reply()`가 **NPE**가 된다.
> **타입이 초대한 실수가 실제로 일어났다.**

### 15.6 판정이 필요한 항목 — 스펙끼리 충돌한다

**해소했다.** 공통 channel 메시징 spec의 원인별 고정 로그 수준을 따른다.

`.NET`의 `sendLogLevel` / `publishLogLevel` 공개 옵션을 제거하고 send의 handler 없음·decode 실패·
invalid frame은 `Warning`, publish는 `Debug`, application handler 예외는 `Error`로 runtime 내부에서
고정했다. Java·C++의 남은 표면은 §2.7과 해당 언어 exact spec에서 추적한다.

## 16. 언어별 구현 차이 연결

언어별 exact spec은 public interface를 정의하면서 현재 구현과 다른 항목을 함께 표시한다. 표의 구현
차이 항목은 이 문서의 §2.7 또는 §12 상세 절을 참조한다. 구현 진행 상태와 완료 증거는 exact spec이나
이 문서에 중복해서 기록하지 않는다.

| 언어 | exact spec 위치 |
|------|-----------------|
| `.NET` | `server/languages/dotnet/` |
| Java | `server/languages/java/` |
| Kotlin | `server/languages/kotlin/` |
| Node.js / TypeScript | `server/languages/node/` |
| C++ | `server/languages/cpp/` |
