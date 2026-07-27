# Framework 언어별 구현 차이

[스펙 목차](README.ko.md) · [이전: Transport 연결 상태 확인](29-transport-liveness.ko.md)


이 문서는 정식 public contract가 아니다. 공통 스펙과 언어별 스펙을 기준으로 현재
구현에서 확인된 차이를 기록한다. 차이를 해결할 때 정식 스펙을 현재 코드에 맞춰
축소하지 않고, 구현과 contract test를 정식 스펙에 맞춘다.

대상은 `.NET`, Java/Kotlin, Node.js와 C++ framework다.

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

## 2. 열려 있는 gap 요약

공통 스펙과 언어별 exact spec을 기준으로 확인한 현재 차이는 다음과 같다. 언어별 public
declaration의 세부 차이는 `server/languages/<lang>/`의 exact spec이 소유하고, 여러 언어에 공통인
원인과 동작 차이는 이 문서의 상세 절이 소유한다.

| gap | 언어 | 목표 계약과 실제 차이 |
|---|---|---|
| §12.1 | Java/Kotlin | 수신 큐가 미수신 이력과 고정 상한을 보존해야 하지만 기존 메시지를 버리고 새 메시지를 유지하며, drop 관측·handler 없는 메시지 보존·`waitFor` 이력 조회가 계약과 다르다 |
| §12.2 | Java, C++ | `onActorJoin` admission 구현을 타입이 강제해야 하지만 선택 사항이라 누락해도 컴파일되고 모든 join이 거절된다 |
| §12.8 | Java | runtime event가 sealed event 계층이 아니며 location source 등록 네 개가 없고 event handler가 `void`를 반환한다 |
| §12.9 | Java/Kotlin, C++ | Spot handle이 전송 mesh를 소유해야 하지만 outbound 호출이 handle과 별도로 ChannelName 또는 node RID를 받는다 |
| §12.15 | Java | 비동기 실패를 오류 코드를 가진 공통 예외로 정규화하지 않는다 |
| §12.22 | C++ | HTTP client에 `submit`·`yield`와 DI 서버 표면이 없고 blocking `fetch<T>()`가 공개되어 있다 |
| §12.23 | Java/Kotlin, C++ | worker callback에 cancellation 신호를 전달하지 않는다 |
| §12.24 | 전 언어 | actor join의 location CAS보다 source leave나 target membership 공개를 먼저 실행한다 |
| §12.25 | `.NET`, Java/Kotlin, C++ | 계약 밖 receive-count, 수신 큐 admission 우회 또는 operation별 codec 선택·registry가 남아 있다 |
| §12.26 | Java/Kotlin | Config 5·7 E2E가 exact RouteMesh runtime options 대신 [ChannelName](01-glossary.ko.md#channelname) 전용 options를 사용한다 |
| §12.27 | Java/Kotlin, C++ | Actor location이 [Spot](01-glossary.ko.md#spot) lifecycle generation을 보존하지 않아 Spot ID 재사용 뒤 stale [membership](01-glossary.ko.md#membership)을 구분할 수 없다 |
| §12.28 | C++, Java/Kotlin, Node | STREAM Actor dispatch의 global Actor authority 계약과 실제 runtime 연결을 다시 맞춰야 한다. `.NET`은 여러 Object Mesh를 허용하고 resolve한 `ActorRef.MeshName`으로 framework route를 선택한다 |
| §12.29 | C++, Java/Kotlin, Node | Location provider의 opaque authority CAS와 Relocation Store를 relocation coordinator에 연결하는 작업이 남아 있다. `.NET`은 accepted request의 durable replay cursor·terminal completion·reply relay ACK와 source cleanup을 production 경로에 연결했다 |
| §12.31 | Java/Kotlin, Node, C++ | Actor relocation metric이 `mesh_name`, 닫힌 `outcome`과 실패 terminal을 기록하지 않는다 |
| §12.32 | 전 언어 | 알 수 없는 non-JSON content-type을 decode 전에 거부하지 않는다 |
| §12.33 | Java/Kotlin, Node, C++ | [MeshName](01-glossary.ko.md#meshname) 중심 [RouteMesh](01-glossary.ko.md#routemesh)·MeshNode 등록 표면이 package·sample·E2E까지 일관되게 적용되지 않았고 분리 builder나 production in-memory location helper가 남아 있다 |
| §12.34 | `.NET`, C++ | ActorRef의 공통 세 필드 밖 공개 상태가 남아 있다 |
| §12.35 | C++ | Actor generation이 하나의 lifetime 안에서 join마다 증가한다 |
| §12.36 | C++ | `Relocate`·`Shutdown`, terminal result, factory-attached relocation policy와 provider capability가 exact interface에 맞게 구현되지 않았다 |
| §12.37 | `.NET` | RouteMesh runtime snapshot의 Core 미노출 필드를 빈 값이나 근사값으로 채운다 |
| §12.38 | Java/Kotlin | RouteMesh runtime [snapshot](01-glossary.ko.md#snapshot)의 Core 미노출 필드를 빈 값이나 근사값으로 채운다. Drain은 RouteMesh가 하나인 host에서만 host 공유 operation을 사용하고, 둘 이상이면 다른 MeshNode까지 종료하지 않도록 요청을 거부한다 |
| §12.39 | 전 언어 | ClientServer dual-role 등록과 local Server transport 선택은 구현됐다. ChannelName 단일 주소, exact role builder·listener network identity·monitoring snapshot과 process E2E가 남았다 |
| §12.40 | 전 언어 | classic fanout 전용 publisher descriptor·store·RID allocation과 endpoint 없는 automatic subscriber가 적용되지 않았다 |
| §12.41 | 전 언어 | maintenance runtime의 target restore·replay·source cleanup·reply ACK까지 이어지는 production 경로와 process 장애 E2E를 완성해야 한다. `.NET`은 durable accepted request completion, closed relay ACK와 exact source lease expiry를 연결했으며 실제 두 host 장애 recovery 검증이 남아 있다 |
| §12.42 | `.NET`, Java/Kotlin, Node, C++ | maintenance relocation transport가 service wire command 30~35·40~46의 canonical frame 대신 언어별 private control envelope를 사용하거나 production wire 연결이 없다 |
| §12.43 | 전 언어 | 다섯 언어의 공개 one-way call은 비동기 결과로 전환됐다. 그러나 언어별 admission runtime에 signal 없는 재시도, blocking executor, terminal queue cleanup과 Logical Multicast commit barrier 차이가 남아 있고 Config 13 process E2E가 없다 |
| §12.44 | 전 언어 | Instance Spot exact public surface, opaque [authority](01-glossary.ko.md#authority) CAS 기반 cold activation과 네 언어 runtime의 actor-free lifecycle·fencing·recovery가 완성되지 않았다 |
| §12.45 | `.NET`, Java/Kotlin, C++ | User Spot aggregate relocation은 command 30 source accept 뒤 participant 전체의 typed capacity bundle으로 aggregate prepare를 한 번 실행하고, target factory·Restore 뒤 같은 aggregate fence를 commit해야 한다. Standalone relocation capacity fence와 뒤늦은 aggregate prepare를 함께 사용하면 capacity를 이중 예약하므로 금지한다 |
| §12.49 | 전 언어 | Host relocation mode와 exact application-version target 선택이 구현되지 않았다. 현재 runtime은 `PlannedMaintenance`와 `RollingUpdate`를 구분하지 않으며 언어별 target filter에도 차이가 있다 |

## 10. Stream Connector wire·검증 계약 차이

[Stream Connector 공통 스펙](stream-connector/32-stream-connector.ko.md)과 언어별 exact spec을
기준으로 남아 있는 차이는 다음과 같다.

| 범위 | 목표 계약 | 실제 구현 차이 |
|---|---|---|
| Java/Kotlin 수신 큐 | 미수신 이력, 고정 상한과 overflow 관측을 보존한다 | 기존 메시지를 버리고 새 메시지를 유지하며, 기본 상한·drop 오류·handler 없는 메시지 보존·`waitFor` 이력 조회가 계약과 다르다(§12.1) |
| 전 언어 수신 content-type | wire가 선언한 content-type과 등록 codec이 다르면 decode 전에 거부한다 | 알 수 없는 non-JSON content-type을 JSON·기본 serializer로 해석하거나 raw payload로 전달한다(§12.32) |

## 11. gap 제거 조건

각 항목은 다음 조건을 모두 만족해야 닫을 수 있다.

1. 언어별 public declaration이 정식 interface spec과 일치한다.
2. package 또는 assembly의 실제 export 목록에서 내부 구현 타입이 제거된다.
3. contract test가 전체 타입과 시그니처를 검증한다.
4. 공통 E2E가 같은 기능과 관찰 가능한 결과를 검증한다.
5. 이 문서에서 해당 차이를 제거한다.

## 12. 상세 구현 차이

§12.1~§12.19의 정확한 public declaration 차이는 언어별 exact spec이 소유한다. 아래 절은 여러
언어에 공통이거나 source·runtime·E2E를 함께 바꿔야 하는 현재 차이를 설명한다.

### 12.22 C++ HTTP client가 framework 계약 밖에 있다

**C++ 미충족.** [12 HTTP client](http-client/12-http-client.ko.md)는 HTTP client를 framework 동반
client로 규정하고 terminator·turn seam·서버 등록 표면을 고정한다. 현재 C++ HTTP client는 이
통합 계약을 제공하지 않는다.

| 항목 | 계약 | 현재 |
|------|------|------|
| terminator | `submit` / `async` / `yield` / callback | C++는 완료 방식 전체를 제공하지 않는다 |
| Spot turn 인지 | `SpotWide` User Spot과 Instance Spot의 `yield`가 shared turn을 반환한다 | C++는 framework 실행 turn과 연결되지 않는다 |
| 서버 표면 | DI 주입 client(`submit`/`async`/`yield`/callback) | C++에는 서버 등록 표면이 없다 |
| blocking 표면 | 두지 않는다 | C++ `fetch<T>()`가 남아 있다 |

그 결과 **spot handler에서 외부 API를 호출하면 실행 줄이 그대로 막힌다.** actor 입·퇴장 시 외부
데이터를 가져오는 흐름이 room 전체와 timer를 멈춘다 — 이 client가 존재해야 하는 이유가 바로
그 경로인데 표면이 없다.

**고쳐야 할 것:**

- 세 terminator(`submit`/`async`/`yield`)와 callback 완료 경로를 제공한다. `yield`는 `SpotWide` User
  Spot과 Instance Spot에서만 활성화한다.
- **turn seam**(execution scheduler 주입점)을 공개 계약으로 둔다. framework가 DI 등록 시 spot
  turn을 아는 scheduler를 꽂는다. C++ HTTP client에는 **같은 형태의 API 표면이 있다**
  (`framework_resume_scheduler_t`) — 다만 framework 런타임이 아직 그것을 주입하지 않으므로 표면만
  있고 통합은 검증되지 않았다.
- **서버용 DI 표면**을 신설한다. application이 명명 등록하고 handler가 주입받는다. 정적 팩토리는
  client-side 전용으로 남긴다.
- blocking 언래핑 terminator를 public 표면에서 제거한다.
- **바이너리 의존은 framework → HTTP client 한 방향을 유지한다.**

### 12.23 C++·Java/Kotlin worker cancellation 부재

**C++와 Java/Kotlin 미충족.** [04 §1.2](05-async-execution-policy.ko.md)는 worker를 CPU worker와
I/O worker로 나누고, 둘 다 `async`·`yield` terminator와 cancellation 신호를 갖도록 규정한다.
두 구현 모두 worker 분리와 terminator는 제공한다. C++ callback은 `std::stop_token`을 받지 않고,
Java/Kotlin의 `ZLinkWorkerTask.run()`과 `ZLinkIoWorkerTask.run()`도 cancellation 인자를 받지 않아 timeout,
caller cancellation과 shutdown을 실행 중인 작업에 전달할 수 없다.

**고쳐야 할 것:**

- 각 언어의 worker callback에 표준 cancellation 표현을 전달하고, 늦은 완료가 먼저 정해진 terminal
  결과를 바꾸지 않는 contract test를 둔다.

### 12.24 전 언어 actor join commit 순서

**전 언어 미충족.** [23 §4](15-spot-actor.ko.md#4-actor-join과-commit-순서)는 admission accept 뒤 location
authority가 expected Actor ObjectGeneration과 current Spot authority를 비교해 owner와 membership을 하나의
CAS로 먼저 commit하도록 고정한다. CAS가 성공한 뒤에는 target `OnJoinedActor`, source `OnLeaveActor`를
차례로 실행한다.
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

1. caller turn에서 target admission을 요청하고 expected Actor [ObjectGeneration](01-glossary.ko.md#objectgeneration)·current Spot authority를 보존한다.
2. accepted reply를 받은 location authority가 target [owner](01-glossary.ko.md#owner)와 Spot membership을 CAS commit한다.
3. CAS가 성공한 뒤 target `OnJoinedActor`를 실행한다. CAS가 실패하면 target·source callback을
   실행하지 않고 source membership을 유지한다.
4. Target callback 뒤 source `OnLeaveActor`를 실행한다. Callback failure는 commit 이후 복구 절차로
   처리하며 source로 rollback하지 않는다.
5. 서로 다른 Spot 쌍은 노드 전역 세마포어 없이 병행할 수 있어야 한다.

**E2E:** [config-8 TD-E2](../e2e/config-8-execution-turn.ko.md)(user→user join)의 commit marker
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

### 12.26 Java/Kotlin route-mesh runtime options E2E 표면 차이

Java 10.0.0 exact interface는 ChannelName만 받는
`ZLinkRouteMeshRuntimeOptions.channel(channelName)`과 MeshNode·ChannelName runtime options를 공개
계약으로 고정한다. Process-local channel index가 유일한 owner MeshNode를 선택하므로 caller가 MeshName을
함께 전달하지 않는다. 서로 다른 RouteMesh에 같은 ChannelName을 등록하면 process-local 주소가 충돌하므로
host는 startup 설정 오류로 거부한다. Java source와 E2E에 남은 MeshName+ChannelName overload는 제거
대상이다. Kotlin은 Java runtime을 그대로 사용하므로 같은 ChannelName 단일 표면을 호출한다. 기존 ChannelName 전용
`ZLinkChannelRuntimeOptions.clientServerChannel(channelName)`은 classic client/server channel에만
사용한다.

Java와 Kotlin의 Config 5 RL-B4와 Config 7 MON-A3는 아직 exact 표면으로 weight 0·100·10000의 범위와 부하 제외
의미를 검증하지 않는다. 두 E2E를 ChannelName 단일 표면으로 전환하고, 서로 다른 MeshName에 같은
ChannelName을 등록하면 startup에서 거부하는 contract test를 추가해야 한다.

### 12.27 Java/Kotlin·C++ Actor location의 Spot generation 미구현

[Location Runtime §2](21-location-runtime.ko.md#2-같은-id의-재생성과-owner-변경을-구분하는-값)과 다섯 언어 exact interface는 Actor location에
현재 Spot의 [lifecycle generation](01-glossary.ko.md#lifecycle-generation)을 보존한다. 같은 [Spot ID](01-glossary.ko.md#spot-id)가 종료 뒤 다시 사용되면 이 값으로 낮은
generation의 membership과 새 membership을 구분한다.

Java/Kotlin과 C++의 public `ActorLocation` record와 공식 Redis codec에는 이 필드가 없다.
따라서 같은 Spot ID의 Spot이 재생성되면 stale actor row가 현재 위치처럼 해석될 수 있다.

각 언어는 record, in-memory·Redis codec, location lifecycle과 stale 판정을 함께 갱신하고 다음을
contract test로 고정해야 한다.

- Actor가 Spot에 join할 때 현재 Spot generation을 location row에 기록한다.
- resolve와 relocation admission은 row의 Spot generation과 현재 Spot owner를 함께 검증한다.
- Redis round-trip이 unsigned 64-bit generation을 손실 없이 보존한다.
- 같은 Spot ID를 더 큰 generation으로 다시 만든 뒤 낮은 generation의 actor row를 stale로 거부한다.

### 12.28 전 언어 STREAM Actor dispatch의 global authority 연결 미구현

[Session Actor Dispatch §2·§9](20-session-actor-dispatch.ko.md)는 `EnableActorDispatch()`가
MeshName을 받지 않고 global Actor authority를 사용하도록 고정한다. 같은 process에 Object Mesh가 여러 개
있어도 startup 오류가 아니며, resolve한 ActorRef의 MeshName으로 current owner MeshNode를 선택한다.

`.NET`은 이 계약에 도달했다. Startup은 Actor dispatch를 사용할 때 Location Store와 Object role 하나 이상을
요구하지만 Object Mesh 수를 하나로 제한하거나 MeshName을 추론하지 않는다. STREAM socket은 특정 Object
MeshNode를 공유하지 않으며 session Actor bind·dispatch는 resolve한 `ActorRef.MeshName`을 framework route에
전달한다. 여러 Object Mesh startup 회귀와 전체 Unit test가 이 경계를 검증한다.

- Java/Kotlin과 Node는 여전히 `enableActorDispatch(meshName)`을 공개하고 해당 MeshNode에 session dispatch를
  고정한다.
- C++는 exact `enable_actor_dispatch()`와 global Actor authority를 사용하는 session dispatch 연결이 없다.

각 언어는 다음을 contract test로 검증해야 한다.

- Actor dispatch를 사용하지 않는 STREAM-only host는 Object Mesh와 Location Store 없이 시작한다.
- Actor dispatch를 사용하면 Object `Client` 또는 `Server` role 하나 이상과 Location Store를 요구한다.
- Object Mesh가 여러 개여도 startup이 성공한다.
- 서로 다른 Mesh에 존재하는 ActorRef를 같은 STREAM node에서 bind·dispatch할 수 있다.
- resolve한 ActorRef의 MeshName·NodeRid·generation fence가 stale하면 다른 Mesh로 fallback하지 않는다.

### 12.29 전 언어 durable authority와 Relocation Store 연결 미구현

[Spot Actor §7](15-spot-actor.ko.md#7-실패와-recovery)과
[Location Store provider §4](22-location-store-redis.ko.md#4-conditional-atomic-batch)는 current owner와
relocation state를 표현하는 private record를 같은 atomic batch에서 전이하고, process 종료 뒤 successor가
immutable relocation root와 replay cursor로 처리를 이어 가도록 요구한다.

목표 exact interface는 Actor·Instance phase별 Store를 공개하지 않는다. Root에 등록한 Location provider가
opaque payload의 exact read와 expected Store version 기반 atomic batch를 제공한다. Framework coordinator만
relocation phase, source·target identity, object fence와 recovery lease를 해석한다. `Recreate` 또는 `Snapshot`
policy가 하나라도 있거나 [Instance Spot](01-glossary.ko.md#entry-spot-user-spot과-instance-spot) factory가 하나라도 있는 host는 accepted journal, application state와
recovery payload를 보존할 Relocation Store도 정확히 하나 등록한다. Instance Spot [factory](01-glossary.ko.md#factory)가 없고 모든 factory가
`Disabled`인 same-node 구성에서만 Relocation Store를 생략할 수 있다.

`.NET`은 opaque authority CAS, immutable relocation root, aggregate publication과 target replay를 production
scheduler에 연결했다. Accepted request handler가 끝나면 successor root에 replay cursor와 terminal completion을
먼저 기록하고 모든 participant authority의 reference·checksum·completion count를 aggregate CAS로 바꾼다.
Source reply는 request/ACK로 전달하며 ACK 뒤 pending delivery state를 successor root와 authority count에
반영한다. ACK CAS 전에 process가 중단되면 durable cursor를 읽어 handler를 다시 실행하지 않고 pending terminal
payload만 relay한다. 모든 terminal count와 ACK count가 맞은 current successor root에서 source cleanup phase를
완료한다. Codec round-trip, durable cursor·ACK·cleanup coordinator test와 전체 Unit test가 이 경계를 검증한다.

Java/Kotlin, Node와 C++에는 phase별 Actor relocation Store, Instance owner Store, in-memory pending map과 별도
relocation adapter가 섞여 있거나 coordinator와 production scheduler 연결이 남아 있다. 이 구조는 provider에
Framework 상태 기계를 누출하고 Actor·Instance가 서로 다른 recovery 규칙을 갖게 만든다. 일부 durable 전이가
존재하더라도 공통 opaque authority 계약, durable relocation root와 process 장애 E2E가 끝까지 연결되지 않아 목표
기능으로 판단하지 않는다.

각 언어는 공식 Location Store와 Relocation Store 구현을 별도 class로 제공하고 다음을 contract test로 고정해야 한다.

- Logical authority key는 object kind와 logical identity를 충돌 없이 encode한다.
- Read는 current payload, opaque Store version, Store time과 조건부 lease expiry를 한 snapshot으로 반환한다.
- Compare-exchange는 expected version이 current일 때만 owner와 transaction payload를 한 번에 바꾼다.
- Commit 전 abort와 commit 뒤 recovery는 같은 authority revision과 coordinator lease로 순서를 정한다.
- Relocation reference, checksum과 [replay cursor](01-glossary.ko.md#replay-cursor)는 authority CAS와 연결되고 orphan payload는 retention expiry가 정리한다.
- Accepted request의 terminal completion과 reply relay delivery state는 successor root에 먼저 기록하고 expected
  StoreVersion CAS로 authority reference·checksum·count를 교체한다. Source ACK 뒤에만 pending relay를 완료한다.
- 같은 object의 동시 relocation, 늦은 source cleanup과 commit 이후 callback 실패가 committed target을 지우지 않는다.

### 12.31 Java/Kotlin·Node·C++ relocation metric outcome 미구현

[Runtime Metrics §4](25-runtime-metrics.ko.md#4-object와-stream)는
`zlink.relocation.completed`와 `zlink.relocation.duration`에 `mesh_name`, `object_kind`, `policy`와 닫힌
`outcome=completed|aborted|recovered|failed|shutdown`을 기록하도록 고정한다. Duration은 prepare부터
terminal phase까지의 시간이며 Location commit만으로 성공을 기록하지 않는다.

남은 구현은 성공 경로의 label 없는 값만 기록한다.

- Java `ZLinkActorRuntime.java:629-633`은 두 계기를 `Map.of()` 빈 label로 기록한다.
- Kotlin은 Java runtime을 공유한다.
- Node `actor-relocation-runtime.ts:146-157`은 commit callback에서 label 없이 count와 duration을 기록한다.
- C++ `spot_runtime.cpp:3126-3128`은 label 없이 counter와 histogram을 기록한다.

각 언어는 relocation operation이 MeshName, 시작 시각과 terminal outcome을 한 context로 소유하게 하고,
activation·abort·timeout·[shutdown](01-glossary.ko.md#shutdown)의 각 terminal에서 정확히 한 번 기록해야 한다. 실패 뒤 성공으로 다시
세거나 local join을 relocation으로 세지 않는 contract test와 Config 11 OBS-B2를 label까지 검증하도록 갱신한다.

### 12.32 전 언어 수신 content-type 검증 결함

[Framework API §9](06-framework-api.ko.md#9-codec)은 송신 업무 타입에 맞는 extension이 없으면
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

### 12.33 Java/Kotlin·Node·C++ RouteMesh·MeshNode 통합 표면 차이

[Framework API §3](06-framework-api.ko.md#3-routemesh-등록)과 다섯 언어 exact interface는 물리
MeshName 하나를 `AddRouteMesh`·`addRouteMesh`·`add_route_mesh`로 등록하고 반환된 MeshNode builder가
ChannelName, handler group, node client, manual peer, Spot과 Actor 구성을 소유하도록 고정한다. Production
구성은 공식 [location store](01-glossary.ko.md#location-store)를 사용하며 in-memory store 선택 helper를 공개 표면에 두지 않는다.

현재 source와 package의 차이는 다음과 같다.

- Java/Kotlin `ZLinkFrameworkOptions`는 `addClientServerChannel`, `addRouteMeshChannel`, `addSpotMesh`,
  `useInMemoryLocationStores`를 유지하고 `addRouteMesh`가 없다.
- Node source에는 `addRouteMesh(meshName)`과 MeshNode runtime이 추가됐다. Actor 관련 exact interface와
  E2E actor-local handler는 기존 표면을 사용해 package contract와 일치하지 않는다.
- C++ source·package의 `zlink_framework_options_t::add_route_mesh(mesh_name)`은 목표 계약대로
  `mesh_node_builder_t`를 반환하지만 `spot_mesh_builder_t`와 `connect_peer_pub`도 공개 표면에 남아 있다.

### 12.34 C++ ActorRef 필드 집합 불일치

[상호작용 모델 §7](03-interaction-model.ko.md#7-spot과-actor)과
[Actor 모델 §2](14-actor-model.ko.md#2-actor-identity와-서로-독립적인-상태)는 `ActorRef`를 논리
`ActorId`, `ObjectGeneration`, 현재 `MeshName`과 owner `NodeRid` 네 값으로 고정한다. endpoint,
내부 frame, location row와 Actor type은 참조에 포함하지 않는다. 별도의 public snapshot type도
제공하지 않는다.

C++ `actor_ref_t`는 `actor_type` 필드와 accessor를 추가로 노출하고,
`actor_ref_snapshot_t::to_actor_ref(actor_type)` 호출자가 snapshot에 없는 type을 다시 주입해야 한다.

C++은 `actor_ref_t`를 네 target field로 맞추고 `actor_ref_snapshot_t`를 public 표면에서 제거해야 한다.
Actor handler 선택에 필요한 Actor type은 Actor manager와 runtime registry가 소유하며 application의
참조 복원 호출자에게 전달하지 않는다. `.NET`은 이미 target field와 snapshot 제거 계약에 맞는다.

### 12.35 C++ Actor lifetime 중 generation 변경

**C++ 미충족.** [Spot과 Actor membership §1](15-spot-actor.ko.md#1-identity와-authority)은 Actor
generation을 생성 성공 시 해당 Actor lifetime의 값으로 확정하며 destroy까지 변경하지 않도록 고정한다.
같은 MeshNode의 Spot 이동과 다른 MeshNode로의 relocation은 source와 target에서 같은 Actor generation을
사용하고, 성공한 location commit에서 current Spot authority만 바뀌어야 한다.

C++ runtime은 같은 MeshNode의 이동과 remote relocation target `ActorRef`를 만들 때 기존 generation에 1을
더한다(`spot_runtime.cpp:1227-1229`, `2662-2665`, `3206-3209`, `3406-3409`). C++ ST-F2와 contract
gate도 이 값을 요구한다. Java testkit의 fake backend와 `.NET` 단위 테스트의 remote join mock에도 같은
증가 방식이 남아 있어 contract test가 잘못된 값을 정상으로 받아들일 수 있다.

**고쳐야 할 것:**

- C++에서 생성 시 확정한 Actor generation이 destroy까지 변경되지 않게 하고, join·relocation target
  `ActorRef`가 source Actor ID와 generation을 그대로 사용하게 한다.
- C++ E2E와 contract test, Java testkit과 `.NET` test double에서 이동 전후 generation 동일성을 검증한다.
- 성공한 이동에서는 owner Node RID와 current Spot authority만 바뀌고, destroy 뒤 새 Actor 생성에서만 다음
  Actor generation을 할당하는지 언어별 contract test로 확인한다.

### 12.36 C++ 11.0 host maintenance public surface 미구현

**C++ 부분 충족.** Install-tree public header와 application host runtime은
`relocate(relocation_options_t)`와 `shutdown(...)`을 별도 operation으로 제공한다.
Host state는 `Preparing → Serving → Relocating → Relocated`와
`Serving|Relocated → Draining → Stopped`를 구분한다. `retire`, `drain_result_t`,
`drain`과 `await_drained` 공개 표면은 제거했다. Relocation 성공은 host와
infrastructure connection을 종료하지 않으며, 이후 `shutdown`이 별도로 resource를
정리한다.

[C++ exact interface](server/languages/cpp/interfaces/README.ko.md)는 rolling maintenance의 정식 진입점을
`Relocate`와 `Shutdown`으로 고정하고, 구현 상세 상태 기계는 runtime 내부에 둔다. 다음 항목은 아직
완료되지 않았다.

- Actor와 Instance Spot factory에 연결하는 typed relocation policy와 Snapshot state adapter
- opaque authority CAS와 Relocation Store capability
- Framework runtime maintenance snapshot과 event

별도 Actor relocation registry, phase별 Store와 public operation state machine은 추가하지 않는다.
Install-tree header와 clean consumer contract test가 exact interface와 일치해야 한다.

### 12.37 .NET IZLinkRouteMeshRuntime snapshot의 Core 미노출 필드

**.NET 부분 충족.** [.NET topology monitoring](server/languages/dotnet/interfaces/10-topology-monitoring.ko.md)과
[Runtime monitoring](24-runtime-monitoring.ko.md)은 MeshNode snapshot에 peer별 ChannelName
set, channel별 ready member 수, drain seal 상태와 pending relocation·STREAM barrier 수를 요구한다.

현재 .NET `IZLinkRouteMeshRuntime` 구현은 exact interface와 event stream(polling 파생)을
제공하지만 Core `zlink_mesh_node_status_t`/`zlink_mesh_peer_entry_t`가 위 값을 노출하지 않아
해당 field는 빈 값(빈 목록·0·false)으로 채운다. channel [ready](01-glossary.ko.md#ready) member 수는 admitted peer 수
기반 근사값이다. per-mesh drain은 host 공유 drain에 위임한다(단일 mesh host에서는 동일 의미).

**고쳐야 할 것:**

- Core status/peer 표면이 위 값을 노출하면 binding·seam을 거쳐 실측값으로 교체한다.
- 다중 mesh host의 선택적 drain이 필요해지면 mesh 단위 drain seam을 추가한다.

### 12.38 Java/Kotlin ZLinkRouteMeshRuntime snapshot의 Core 미노출 필드

**Java/Kotlin 부분 충족.** [Java monitoring exact interface](server/languages/java/interfaces/monitoring.ko.md)와
[Runtime monitoring](24-runtime-monitoring.ko.md)은 MeshNode snapshot, bounded observer별 event
stream과 MeshName 단위 drain 표면을 요구한다.

Java의 `ZLinkRouteMeshRuntime`은 exact snapshot·event·drain 타입과 Spring DI 등록을 제공하고, Java
binding의 `zlink_mesh_node_peer_channels` 연결로 peer별 ChannelName과 channel별 ready member 수도
실측한다. Java 표면을 그대로 사용하는 Kotlin도 같은 계약을 호출할 수 있다. 다만 Core
status·monitor가 다음
값을 제공하지 않아 snapshot 전체를 실측값으로 채울 수 없다.

- drain deadline·work seal과 pending relocation·STREAM barrier 수
- location store의 마지막 실패 시각

현재 이 값은 0·빈 `Optional`로 채운다. host drain을 MeshName별
메서드가 공유하므로 여러 MeshNode를 한 host에 등록한 경우 선택한 MeshName만 drain하지 않는다.

**고쳐야 할 것:**

- Core status·peer·monitor가 위 값을 노출하면 Java binding과 framework runtime snapshot에 실측값을
  연결한다.
- location runtime health에 마지막 실패 시각을 보존하고 snapshot에 연결한다.
- 여러 MeshNode를 한 host에 등록할 수 있는 계약을 유지하려면 mesh 단위 drain seam을 추가한다.

### 12.39 전 언어 ClientServer exact public·monitoring 표면 잔여

정식 exact interface는 Channel send/request의 대상을 ChannelName 하나로
고정하고 RouteMesh membership을 `Channel(channelName)` 뒤의 `Server()` 또는 `Client()`로
선택한다. [weight](01-glossary.ko.md#weight), handler group과 typed handler는 server builder만 제공한다. ClientServer는
RouteMesh [descriptor](01-glossary.ko.md#descriptor)를 재사용하지 않고 ChannelName+ServerRid key의 전용 descriptor와 runtime을
사용한다. Listener는 root BindHost·AdvertiseHost 기본값, listener별 override와 automatic
discovery port `0` 규칙을 같이 적용한다.
같은 ClientServer ChannelName에는 Client와 Server를 각각 한 번 등록할 수 있고, local Server도 remote와
같은 readiness·weight·drain 조건으로 실제 DEALER→ROUTER transport를 거쳐 선택한다. Monitoring의 local
role은 `(ChannelName, Role)`의 별도 registration 두 개를 aggregate projection으로 손실 없이 나타낸다.
Logical Multicast도 ChannelName과 topic만 받고 process-local channel index가 owner MeshNode를 선택한다.
선택된 MeshName은 runtime monitoring에 남지만 caller-facing signature에 노출하지 않는다.

네 runtime은 같은 ClientServer ChannelName에 Client와 Server를 각각 한 번 등록하고 동일 역할 중복을
startup configuration error로 거부한다. Local Server도 remote와 같은 readiness·weight·drain 조건으로
전용 discovery와 실제 transport 경로를 거쳐 선택한다. RouteMesh와 ClientServer의 같은 ChannelName
충돌도 startup에서 거부한다.

남은 source·package 차이는 다음과 같다.

- `.NET`은 `ChannelName(...)`, `SetWeight(0)`으로 client-only를 표현하고 channel client가
  MeshName과 ChannelName을 함께 받는다. Exact role builder와 network options 표면이 없다.
- Java와 Kotlin은 RouteMesh `channelName(...)`과 기존 ClientServer `enableClient/enableServer`를
  사용한다. Exact role builder와 listener network identity 표면이 없다.
- Node.js와 NestJS는 `channelName(...)`, MeshName+ChannelName client signature와 endpoint 문자열
  listener를 공개한다. Exact ClientServer builder와 network identity 표면이 없다.
- C++은 ChannelName 단일 client 호출과 기존 ClientServer builder의 일부를 제공하지만
  `enable_client/enable_server` 표면과 endpoint 문자열을 사용한다. Exact RouteMesh role builder와
  공통 network identity 표면이 없다.
- 다섯 언어에는 ClientServer runtime monitoring public surface가 아직 없어 별도 Client·Server registration을
  `ClientAndServer` 계열 값으로 나타내는 aggregate snapshot projection이 없다.
- Dual-role registration, local·remote weighted selection, weight `0`, drain과 RouteMesh name 충돌은
  언어별 internal contract test로 검증했다. 실제 여러 process를 사용하는 E2E는 runtime 완료 뒤 활성화한다.
- Logical Multicast source는 .NET과 Node.js에서 여전히 MeshName+ChannelName+[topic](01-glossary.ko.md#topic)을 받고,
  Java는 이 기존 overload와 ChannelName+topic overload를 함께 노출하여 Kotlin에도 기존 overload가
  보인다. C++ source는 ChannelName+topic 표면을 사용한다.

**고쳐야 할 것:** 각 언어의 source, package contract fixture, sample과 E2E를 exact interface에
맞추고 ChannelName 충돌, 역할별 중복, Client+Server 동시 등록, local·remote 동일 선택과 transport,
wildcard advertise, 경로별 error·completion 규칙을
contract test와 process E2E로 고정한다. Logical Multicast의 기존 MeshName overload는 compatibility
alias로 남기지 않고 다섯 언어 package 표면에서 제거한다.

### 12.40 전 언어 classic fanout automatic discovery 계약 미구현

**전 언어 미구현.** 정식 exact interface는 [classic fanout](01-glossary.ko.md#classic-fanout) publisher를 MeshNode, ClientServer 또는 generic
role record와 분리한 전용 descriptor로 게시한다. Descriptor key는 ChannelName과 Publisher RID이며
lifecycle generation, descriptor revision, endpoint, drain state, security identity와 owner lease 정보를
보존한다. Subscriber row는 게시하지 않는다.

Store를 등록한 publisher는 고정 Publisher RID 또는 routing ID 자동 할당을 startup 전에 선택하고 실제 bind
endpoint를 전용 descriptor로 게시한다. Store가 없는 publisher는 manual endpoint 대상으로 계속 사용할 수
있지만 allocation과 automatic discovery 등록은 수행하지 않는다. Endpoint 없는 automatic subscriber는
store를 필수로 사용하고 같은 ChannelName의 유효하며 drain 중이 아닌 publisher를 모두 연결한다. Manual
subscriber는 store 없이 명시한 endpoint만 연결하며 두 subscriber mode를 한 channel에 섞지 않는다.

현재 source·package 차이는 다음과 같다.

- `.NET`은 publisher/subscriber reconcile 일부가 있지만 endpoint 없는 public subscriber 설정, Publisher
  RID·allocation과 fanout 전용 descriptor·store가 없어 public API에서 [automatic discovery](01-glossary.ko.md#automatic-discovery)가 완성되지
  않는다.
- Java와 이를 공유하는 Kotlin, Node와 C++은 fanout peer 선택 로직이 있지만 generic peer record와 role을
  사용한다. 전용 fanout row·key·stamp·query로 이동해야 한다.
- `.NET` source·package에는 classic fanout outbound client가 없고 Java/Kotlin, Node와 C++은 topic을
  발행 인자로 받거나 Logical Multicast result 또는 결과 없는 호출을 사용한다. 다섯 언어 모두
  ChannelName과 typed event만 받는 전용 fanout call과 bounded one-way admission 결과로 맞춰야 한다.
- 기존 [routing ID](01-glossary.ko.md#routing-id) allocation member가 MeshName만 이름으로 받는 언어는 member identity를
  `(MeshNode, MeshName)` 또는 `(FanoutPublisher, ChannelName)`으로 표현하도록 바꿔야 한다.

**고쳐야 할 것:** 각 언어 source와 공식 Redis extension에 전용 publisher descriptor·store·codec·change
stamp를 적용하고 classic fanout outbound client·call을 정확 인터페이스에 맞춘다. Builder와 startup
validation은 manual publisher 회귀를 보존하면서 automatic subscriber의 store 누락, fixed/allocated
Publisher RID 충돌과 두 subscriber mode 혼합을 거부해야 한다.
Contract test와 process E2E는 같은 ChannelName의 복수 publisher 전체 연결, 다른 ChannelName 제외, owner
lease 만료·drain·재게시 reconcile과 manual publisher/subscriber의 store 없는 동작을 검증해야 한다.

### 12.41 전 언어 host maintenance production 경로 미완성

정식 계약은 MeshNode별 drain policy를 제공하지 않는다. Host `Relocate`는 Entry
Spot Actor, `PerActor` User Spot의 Actor, Instance Spot과 `SpotWide` User Spot
aggregate에 infrastructure notification을 예약하고 현재 turn을 끝낸 ready unit부터
bounded relocation을 수행한다. Permit을 모두 얻기 전에는 queue를 seal하지 않는다.
Seal 시점에 실행하지 않은 message, accepted journal과 대상별 timer 정보는 Relocation
Store에 저장해 target에서 복원한다. `Shutdown`만 새 relocation 없이 local resource를
bounded cleanup한다.

현재 source·package 차이는 다음과 같다.

- `.NET`은 host-wide first-intent barrier, readiness-first scheduler, permit-before-seal, current-turn boundary,
  source hold relay, queue·accepted journal·logical timer capture와 User Spot·member Actor aggregate publication을
  구현했다. Target replay는 accepted request의 replay cursor와 terminal completion을 successor root와
  authority에 먼저 반영한다. Source reply는 `TerminalReceived`와 `AlreadyTerminal`을 구분해 ACK하고, relay가
  실패하면 frozen source owner의 exact lease가 Missing·stale·expired인지 Store에서 확인한 경우에만
  `SourceLeaseExpired`를 durable delivery state로 기록한다. 재시도는 durable cursor와 pending terminal payload를
  읽으므로 application handler를 중복 실행하지 않는다. Preflight는 Spot aggregate와 standalone Actor가 공유하는
  provisional capacity를 누적하고 limit `0`을 unlimited로 해석한다. 실제 두 host process 장애 E2E와 Entry Spot
  standalone Actor callback 순서 검증은 남아 있다.
- Java/Kotlin, Node와 C++은 위 단계를 일부 구현했지만 target factory·restore staging, aggregate publication,
  replay, source cleanup과 completion ACK가 하나의 production scheduler 경로로 끝까지 연결되지 않았다.
- Java/Kotlin에는 별도 `ZLinkDrainControl` 공개 표면이 남아 있어 host runtime이 termination intent와 terminal
  result를 소유하는 목표 계약과 다르다.
- 기존 구현의 Entry Spot standalone Actor maintenance에는 target relocation
  callback과 source leave callback이 남아 있다. 두 callback과 대응 public method를
  제거하고 Actor adapter, queue·timer, source relay와 session route만 Framework가
  이전해야 한다.
- `SpotWide` User Spot과 member Actor는 하나의 aggregate permit·root·commit
  generation으로 이전해야 한다.
- `PerActor` User Spot은 `Recreate` shell과 Actor별 owner를 사용해야 한다. 현재
  runtime은 Spot authority를 먼저 target으로 바꾸고 `ToSpot`·Create·Join을 target,
  `ToActor`를 Actor별 current owner로 분리하는 transient split state와 relay barrier를
  구현하지 않았다.
- Actor queue seal부터 target admission까지 1초 목표를 측정하는 metric과 warning이
  없으며, 초과를 failure나 rollback 조건으로 사용하지 않는 검증도 없다.
- `SpotWide`의 `AnyTurnBoundary`·`ApplicationSignaled` option,
  `RelocationReady().Defer()` queue barrier와 source `Continued`·target `Relocated`
  default no-op callback이 다섯 runtime production scheduler에 연결되지 않았다.
  Invalid mode·context·duplicate call의 mutation 전 오류와 callback recovery cursor도
  구현해야 한다.

각 언어는 계약 밖 drain 표면과 runtime 분기를 제거하고 host maintenance barrier, object coordinator,
Relocation Store와 execution queue를 연결해야 한다. Contract test와 Config 5 `RL-F10~F14`,
Config 11 `OBS-C3`는 느린 current turn 격리, permit-before-seal, frozen queue·journal·timer 복원, hold relay,
Entry·PerActor Actor callback 부재와 owner relay, `SpotWide` aggregate commit,
application-signaled safe point와 callback ordering, 1초 interruption 측정,
precommit source 복원, bounded force stop과 terminal result
한 번을 검증해야 한다.

### 12.42 네 runtime의 canonical relocation service wire 미연결

[Service wire schema](../../../../runtime/protocol/service-wire-v1.schema.json)는 maintenance relocation을
`relocationReady`, `relocationData`, `relocationAck`, `relocationSeal`, `relocationComplete`,
`relocationPrepare`, `relocationReserved`, `replyRelay`와 `replyRelayAck` command로 고정한다. Stable
RelocationId, target attempt generation, coordinator, participant, exact request-source fence와 closed ACK 상태는
이 frame의 필수 field다.

현재 `.NET` Spot relocation은 `.stage.v1`·`.publish.v1`·`.abort.v1`·`.reply.v1` typed Route packet을
사용한다. Java는 `ZLinkSpotRelocateControl` 전용 magic과 command kind를 사용하고 Node는 NodeRequest payload 안의
별도 relocation control envelope를 사용한다. C++ maintenance component도 canonical command의 production
encode·dispatch 경로가 없다. 각 언어의 공통 service-wire codec은 command number를 known set으로 인식하지만
해당 command body 전체를 encode·decode하여 runtime state machine에 전달하지 않는다. 이 상태에서는 다른 언어가
선택된 target으로 relocation을 시작할 수 없고, 언어별 ACK가 schema의 exact fence를 증명하지 못한다.

언어별 private packet name과 control magic을 제거하고 schema field 순서·bound를 구현한 하나의 canonical codec을
각 runtime의 raw RouteMesh infrastructure dispatch에 연결해야 한다. Byte golden fixture는 네 구현이 같은 frame을
생성하고 서로 decode하는지 검증하며, mixed-language process E2E는 source·target 모든 방향에서 reservation,
data/ACK, completion, reply relay와 duplicate `terminalReceived|alreadyTerminal` ACK를 검증해야 한다. 새 public API나
canonical frame을 감싸는 두 번째 application packet protocol을 추가해서는 안 된다.

### 12.43 전 언어 one-way async-only admission 계약 미적용

[비동기 실행 정책 §1.3](05-async-execution-policy.ko.md#13-one-way-submit)은 Framework public one-way call을
결과값 없는 비동기 terminator 하나로 고정한다. Queue가 일시적으로 가득 차면 operation family가 소유한
유한한 send timeout까지 기다리고, 그 안에 수락하지 못하면 `DeadlineExceeded` exception으로 완료해야 한다.

다섯 언어의 공개 one-way call과 사용처에는 동기 `TrySubmit` 계열이나 public admission status가 없어야 한다.
공개 계약 검사도 모든 terminator가 비동기 void 완료와 exceptional completion을 제공하는지 확인한다. 현재
남은 구현 차이는
다음과 같다.

- C++ Node direct는 현재 Core의 admitted pipe 결과만 사용한다. Config 13 process topology에는 Location Store의
  descriptor·location resolve snapshot이 연결되어 있지 않으므로, descriptor가 없는 RID와 알려졌지만 route가
  준비되지 않은 RID가 모두 `RouteNotConnected`가 된다. [Node direct](01-glossary.ko.md#node-direct) submit 전에 target이 논리적으로 존재하는지
  판단할 기준 데이터를 조회해야 두 상태를 구분할 수 있다.
- Node.js는 expected RID를 모두 명시한 manual peer registry에서는 unknown RID를 `TargetNotFound`로 분류하고,
  registry에 남아 있지만 ready route가 없는 RID는 `RouteNotConnected`로 분류한다. Self RID direct submit도
  기존 node-direct dispatcher의 local admission을 사용해 결과값 없이 완료한다. Discovery mode나 expected
  RID가 없는 [manual endpoint](01-glossary.ko.md#manual-endpoint)가 섞인 구성에는 unknown RID와 이전에 연결됐던 disconnected RID를 구분할
  target이 논리적으로 존재하는지 판단할 기준 데이터가 없다. 이 구성은 Core의 pipe 결과만 사용하므로 두
  상태를 완전히 구분하지 못한다.
- JVM은 expected RID를 모두 명시한 manual peer registry에서 unknown RID를 `TargetNotFound`로 분류하고,
  registry에 남아 있지만 ready route가 없는 RID는 `RouteNotConnected`로 분류한다. Self RID direct submit은
  기존 Mesh application dispatcher와 drain claim을 사용하는 local admission으로 결과값 없이 완료한다.
  Discovery mode나 expected RID가 없는 manual endpoint가 섞인 구성에는 target이 논리적으로 존재하는지 판단할
  기준 데이터가 없으므로 두 상태를 완전히 구분하지 못한다. JVM Config 13은 Node direct·ChannelName·classic
  fanout의 일부 process 시나리오만 검증하며 Spot·Actor·session·STREAM과 drain·shutdown 경합 evidence는 없다.

.NET, C++와 JVM에서는 일반 signal admission이 동작한다. .NET과 Node.js에서는 queue가 없는 Logical
Multicast direct handoff와 commit barrier도 동작하며, Node.js binding은 Core call을 event loop 밖에서
실행한다. Target별 partial detail과 publish 전용 monitoring은 제거해야 한다. 네 runtime의
Config 13 runner와 feature map은 존재하지만,
.NET·C++·JVM·Node.js도 현재
구현한 일부 process 시나리오만 검증한다. Family 전체의 pending waiter, queue reservation, transport attempt,
commit과 cleanup counter가 timeout·cancellation·shutdown 경쟁 뒤 0인지 확인하는 process evidence가 남아 있다.

.NET exact interface는 `ConfigureRouterSocket()`에 송신·수신 HWM과 timeout을 각각 제공한다. 그러나 Core
MeshNode는 물리 ROUTER에 하나의 HWM override만 적용하고, 현재 .NET binding은 `RouterHighWaterMark`와
`SendTimeout`만 제공한다. Framework는 송신 HWM과 send timeout을 기존 binding 표면에 전달할 수 있지만,
수신 HWM과 receive timeout을 다른 값에 임의로 덮어쓰면 exact interface의 의미가 달라진다. 공통 계약에서
물리 ROUTER option을 단일 값으로 표현할지, Core와 binding이 방향별 값을 실제로 지원할지 결정하기 전까지
수신 쪽 두 설정은 구현 gap으로 유지한다. `ConfigureSpotPublisher()`에도 HWM과 send timeout이 남아 있어
같은 물리 ROUTER 설정의 owner가 중복된다. 이 표면을 제거하거나 Logical Multicast 전용 의미로 다시 정의하는
변경도 다섯 언어의 public contract를 함께 바꾸는 별도 설계 작업으로 처리한다.

Logical Multicast는 partial admission 뒤 전체 publish를 다시 실행할 수 없으므로 일반 send-ready 재시도
경로를 사용할 수 없다. 모든 언어는 unbounded payload queue가 없는 bounded I/O executor를 사용한다.
즉시 worker slot을 얻지 못한 operation은 bounded handoff waiter 안에서만 send timeout까지 기다린다.
Waiter capacity까지 모두 사용 중이면 새 payload를 보관하지 않고 `DeadlineExceeded`로 즉시 완료한다.
Slot을 얻은 operation만 direct handoff로 Core blocking publish를 한 번 시작하고,
Core call이 시작된 뒤에는 cancellation이나 shutdown이 snapshot의 나머지 target 제출을 중단하지 않는 commit
barrier를 구현해야 한다. Contract test는 여섯 submit status, local exceptional completion, finite timeout
validation, cancellation·timeout·shutdown terminal 경합, bounded waiter overflow, no late admission과
multicast single-call을 함께 검증해야 한다.

### 12.44 전 언어 global Spot placement 계약 미구현

정식 계약은 Spot manager의 명시적인 `Create`와 `GetOrCreate`가 User Spot만 생성하도록 요구한다. 두 operation은
target node나 endpoint를 받지 않으며 single-use call로 동작한다. Direct send/request의 시작 method는 global
Spot ID만 받고 Spot 전용 fluent call을 반환한다. Instance intent가 없는 call은 existing Ready owner만
resolve한다. [Instance intent](01-glossary.ko.md#instance-intent)를 가진 call만 Missing Instance Spot의 [cold activation](01-glossary.ko.md#cold-activation)을 시작하며 stable type을
생략하면 선택한 Mesh의 distinct Instance type이 하나일 때 자동 선택한다. 여러 type이면 caller가 type을
명시한다. 별도 address, resolver와 handle은 제공하지 않는다.

현재 Core public header와 runtime의 Instance Spot driver, activation token과 binding projection은 10.x 전환 중
생긴 구현 입력이다. 11.0 목표는 이 service 상태 기계를 Core 또는 공통 C ABI에 유지하지 않는다. 각 언어
Framework runtime이 해당 binding의 public raw socket, timer, poller와 monitor API 위에서 activation barrier를
구현한다. `struct_size`, driver record, StoreVersion, authority generation과 owner token은 Framework public
API가 아니며 application이나 일반 binding 사용자에게 노출하지 않는다.

Framework의 현재 차이는 다음과 같다.

- 다섯 언어의 source에는 exact interface가 정한 global `SpotRef`, User Spot manager create call, actor-free
  lifecycle, type factory와 Spot 전용 fluent send/request가 완성되어 있지 않다. Application one-way call은 동기
  `TrySubmit` 없이 언어별 비동기 submit 하나만 제공해야 한다.
- Java의 기존 Spot interface는 Actor lifecycle을 함께 상속하므로 actor-free base를 분리하지 않으면 모든
  Java Spot factory가 Instance 등록에서 거부된다. Node.js도 일반 Spot과 Instance lifecycle type을 분리해야
  한다.
- 공식 Redis extension과 공통 Location provider에는 opaque authority read·compare-exchange, Store time과 lease
  snapshot이 구현되어 있지 않거나 Actor·Instance phase별 API로 나뉘어 있다. Instance factory를 등록한
  runtime은 provider의 authority capability를 startup에서 검사해야 하며 MeshNode descriptor에 Instance type,
  relocation policy와 application version capability를 게시해야 한다.
- Source의 eligible-node selection과 first-message activation envelope, target-owned generic reservation,
  Ready-visible ordering, factory 실패 cleanup, lease-derived local admission deadline, bounded stale-route
  forwarding과 Relocate·close 순서가 구현되어 있지 않다. Source가 target transport 전에 Instance owner claim을
  만드는 현재 구현은 목표 계약이 아니며 target CAS winner만 factory를 실행하도록 교체해야 한다.
- Activation outcome·duration, pending budget, claim conflict·takeover metric과 `surface=instance_spot` message-flow
  drop 관측이 언어별 runtime에 연결되어 있지 않다.
- PlayerQuest와 OrderWorkflow sample은 User Spot의 수동 local GetOrCreate·resolve 절차를 사용한다. Global
  User Spot manager create 경쟁, Instance fluent cold activation, close 뒤 새 generation 활성화와 외부 state 복구를
  검증하지 않는다.

구현은 protocol schema, authority fixture와 다섯 언어 exact interface를 먼저 고정한 뒤 C++·.NET·JVM·Node.js
runtime에서 병렬로 진행한다. Java와 Kotlin은 JVM runtime을 공유하지만 각 public artifact의 signature를 따로
검증한다. 네 runtime과 cross-language E2E가 같은 동작을 증명한 candidate에서 Core service driver와 네 binding
projection을 제거한다.
Remote target의 queue 미수락 receipt와 자동 request 재제출은 첫 계약에 추가하지 않는다. Instance intent가
없는 일반 message는 durable creation intent 없이 owner를 선택하거나 Instance를 선제 activation하지 않는다.

### 12.45 전 언어 User Spot execution mode와 barrier 미구현

정식 계약은 User Spot factory의 기본 `SpotWide`와 선택형 `PerActor`를 요구한다.
`SpotWide` member Actor는 Actor FIFO claim 안에서 User Spot gate를 얻는다.
`PerActor`는 Actor별 FIFO lane, Spot direct·lifecycle lane과 timer별 FIFO lane을
사용한다. 현재 runtime은 언어마다 Actor mailbox와 Spot queue의 중첩 순서가
다르고 yielded continuation까지 기다리는 close·snapshot·relocation barrier가 없다.

각 runtime은 `Actor claim → User Spot gate` 순서를 지키고 application continuation을
inline으로 실행하지 않아야 한다. Barrier는 admission과 participant 변경을 먼저
seal한 뒤 active·yielded Actor·Spot·timer lane이 같은 generation의 안전 경계에
도달했을 때만 capture한다.

### 12.46 전 언어 Yield allowlist와 submit 전 검증 미구현

`Yield`는 Channel·Spot·Actor request와 I/O·CPU worker call에만 존재하고
`SpotWide` User Spot과 Instance Spot에서만 실행할 수 있다. 현재 public interface에는
Actor Join과 일반 owner call의 `Yield`가 남아 있고 일부 runtime은 outbound
operation이나 worker를 시작한 뒤 execution context를 검사한다.

Entry Spot·Entry Actor·`PerActor`·Node·Channel·owner 밖의 호출은 operation ID,
outbound admission, worker scheduling과 queue를 변경하기 전에
`InvalidConfiguration`으로 끝나야 한다. 같은 gate를 다시 기다리는 `Async`와
자기 Actor로 보내는 awaited request도 timeout에 의존하지 않고 submit 전에
거부해야 한다.

### 12.47 전 언어 typed capacity와 Location transaction 미구현

현재 구현의 generic active object·pending activation 수와 scalar capacity delta는
Actor와 Spot의 다른 비용 및 User Spot aggregate를 표현하지 못한다. 목표 계약은
Actor total, Spot total과 선택적인 Spot stable-type bucket을 typed vector로
예약한다. `0`은 unlimited이고 activation concurrency는 population limit과 별도다.

Creation·commit·abort·destroy·relocation과 aggregate commit은 vector 전체와
authority를 하나의 Location Store transaction에서 변경해야 한다. 공식 Redis
provider는 `{zlink-location-v3}` epoch로 migration하고 monitoring은 계층별
active·reserved·limit을 제공해야 한다.

### 12.48 전 언어 Entry Spot lifecycle ID 미구현

Entry Spot은 MeshNode diagnostic prefix에 `-entry-`와 독립적인 RFC 4122 UUID v4
lowercase canonical 문자열을 붙인 Spot ID를 lifecycle마다 발급한다. Runtime과
provider는 같은 lifecycle의 exact descriptor mapping, replacement lifecycle의 새
Spot ID와 global namespace 충돌 처리를 구현해야 한다.

Active collision이면 record를 변경하거나 다른 ID로 재시도하지 않고
`SpotIdConflict` startup failure를 반환한다. Caller가 지정한 User·Instance Spot ID가
Entry Spot 예약 형식과 같으면 Location Store reservation과 factory 전에
`InvalidConfiguration`으로 거부한다. RID 문자열을 parse하거나 MeshNode RID 전체를
결합해 Entry Spot 관계를 복원하지 않는다.

### 12.49 전 언어 relocation mode와 exact version 선택 미구현

목표 계약은 host relocation을 두 mode로 분리한다. `PlannedMaintenance`는 source와 application
version이 정확히 같은 target만 사용한다. `RollingUpdate`는 caller가 source보다 큰 target version을
명시하고 그 version과 정확히 일치하는 target만 사용한다. 두 mode는 version을 먼저 적용한 뒤
maintenance wave, capability, capacity와 placement weight를 순서대로 적용한다.

현재 production runtime은 이 mode와 target version을 public input과 result에 보존하지 않는다.
`.NET`과 C++ target selection은 source 이상의 version을 허용하므로 같은-version 점검과 새-version
전환을 구분하지 못한다. Node.js는 readiness 단계의 version·wave 조건을 실제 workload target 선택에서
다시 검증하지 않는다. Java/Kotlin은 host preflight가 version·wave 조건을 적용하지 않는다.

모든 언어에서 다음 항목을 함께 구현해야 gap을 닫을 수 있다.

1. Mode와 target version option을 host lifecycle operation에 전달하고 결과에도 effective version을 남긴다.
2. 잘못된 조합은 state와 admission을 변경하기 전에 public argument error로 거부한다.
3. 요청한 exact version이 없으면 deadline까지 기다린 뒤 `Blocked/TargetUnavailable`로 끝내며 다른
   version으로 자동 전환하지 않는다.
4. Mode와 effective version이 같은 concurrent call만 shared operation에 합류시킨다. 다른 option은
   `Blocked/OperationInProgress`로 거부한다.
5. Planned maintenance, rolling update, mixed-version candidate, target 부재와 concurrent conflict를
   contract test와 process E2E에서 검증한다.

### 12.50 전 언어 User Spot inventory tree와 aggregate root CAS 미구현

목표 계약은 User Spot 하나에 포함할 수 있는 Actor 수를 1,024개로 제한하지 않는다.
Framework는 Spot과 member Actor 전체를 최대 1,024개·encoded 1 MiB의 immutable leaf
chunk로 나누고, 필요하면 index chunk를 추가하여 Location Store inventory tree를
만든다.

모든 chunk를 저장하고 전체 count와 digest를 확인한 뒤 aggregate authority의 owner,
generation, inventory root와 capacity를 한 번의 CAS로 전환한다. 이 CAS 전에는 모든
participant가 source owner를 사용하고, CAS 뒤에는 모두 target owner를 사용한다.
Actor별 owner row 전체를 하나의 Store transaction에서 바꾸지 않는다.

현재 service wire schema와 production runtime은 aggregate participant vector와 record
하나에 최대 1,024개를 넣는 구조다. 이 구조는 Actor가 1,023명을 넘는 User Spot을
relocation할 수 없다. 모든 언어에서 다음 항목을 구현해야 gap을 닫을 수 있다.

1. Location Store에 immutable inventory leaf·index chunk와 작은 aggregate authority
   record를 저장한다.
2. Participant negotiation과 relocation staging을 한 vector가 아니라 inventory
   root·count·digest와 chunk stream으로 처리한다.
3. Actor direct resolve가 current aggregate generation과 owner를 따르게 한다.
4. Join·leave는 새 inventory path를 준비한 뒤 Actor membership과 aggregate root를
   expected-version batch로 함께 바꾼다.
5. Actor 10,000개를 포함한 User Spot relocation에서 CAS 전 부분 visibility가 없고
   CAS 뒤 전체가 target으로 전환되는지 process E2E로 검증한다.

### 12.51 Host·topology 상태 이름 미적용

목표 계약은 relocation 완료 상태와 결과를 `Relocated`로 표현한다. `Draining`은
`Shutdown`이 이미 수락한 작업과 resource를 정리하는 동안에만 사용한다. Topology
하나의 가용성은 host lifecycle과 분리된 `TopologyState`·`TopologyReason`으로
표현한다.

다섯 언어 exact interface는 host 상태의 `Relocated`와 topology 상태의
`TopologyState` 이름을 사용한다. C++의 이전 `drain_state_t`와 `drain_event_t`는 host
lifecycle 조회를 중복하므로 public contract에서 제거했다.

C++ public source와 host runtime은 `Relocating`·`Relocated` 상태와 분리된
`Relocate`·`Shutdown` operation을 사용한다. Java·Kotlin·Node.js public source와
runtime 전이, metric label과 contract test를 exact interface에 맞춘 뒤 나머지
차이를 닫는다.

## 13. 샘플 계약 차이

[공통 샘플 규약](../sample/README.ko.md)과 개별 sample 문서를 기준으로 남아 있는 차이는
다음과 같다.

| 대상 | 목표 계약 | 실제 sample 차이 |
|---|---|---|
| C++ DeliveryDispatch | client가 `DeliveryStatusNotify`의 `Assigned → Accepted → PickedUp → Delivered` 순서를 직접 검증하며, sample 계약에 없는 server 판정 메시지를 두지 않는다 | client가 순서를 검증한 뒤에도 계약에 없는 `ServerAssertionReq/Res`와 `/self-check/assert`를 사용해 server가 계산한 `passed` 값을 추가로 받는다 |

## 14. 문서 소유권 중복

같은 계약을 둘 이상의 정식 문서가 소유해 한쪽만 바뀌면 계약이 달라질 수 있다. 아래 표의 목표
소유자로 계약을 모으고, 해당 문서를 읽는 contract test도 같은 위치로 옮겨야 한다.

| 중복 | 어디 | 누가 이겨야 하나 |
|------|------|------------------|
| Kotlin **connector coroutine·`Flow` 표면** | server exact interface에서는 제거했으며 `stream-connector/languages/kotlin/` 이관이 남아 있다 | connector package exact interface를 별도로 고정한다 |
| connector **wire header 필드**를 서버 관측 문서가 함께 정의한다 | `26-message-flow-tracing.ko.md`, `27-flow-correlation.ko.md` | **[32](stream-connector/32-stream-connector.ko.md)가 wire를 소유**한다. 52/53은 추적 **의미**만 갖고 wire는 32를 참조한다 |
| `session-closing` **인코딩과 client 디코딩**을 서버 drain 문서가 함께 정의한다 | `28-graceful-drain-handoff.ko.md` | **32가 wire와 connector 동작을 소유**한다. 54는 **언제·왜 보내는가**만 갖는다 |
| connector **메트릭**(`zlink.stream.reconnects`)을 서버 메트릭 문서가 정의한다 | `25-runtime-metrics.ko.md` | connector가 emit하는 신호는 **32**로 옮긴다. 51은 서버가 emit하는 것만 갖는다 |

**판정 기준은 "누가 그 바이트를 만드는가"다.** connector가 생성·인코딩하는 것은 32가 소유하고,
서버가 관측·해석하는 의미만 5x가 갖는다. 지금은 두 문서가 같은 header layout을 각각 적고 있어,
한쪽만 고치면 조용히 갈라진다.

## 15. 스펙·구현 직접 대조에서 확인한 갭

§12는 언어 간 public surface를 대조한 차이를 다룬다. 이 절은 정식 스펙의 요구를 실제 source,
sample과 E2E가 검증하는지 직접 대조해 확인한 현재 차이를 기록한다. 상세 source 위치와 언어별
수정 범위는 §16의 언어별 gap 문서가 소유한다.

### 15.1 대조 범위

| 대조 축 | 확인하는 차이 |
|---|---|
| 정식 스펙과 source | 공개 타입·runtime 동작·경합 경로가 정식 계약을 따르는지 확인한다 |
| 공통 sample과 언어별 sample | 공개 사용 예제의 topology·handler·결과 검증이 공통 sample 계약을 따르는지 확인한다 |
| 공통 E2E와 언어별 gate | 실제 application 경로, fault injection과 실패를 놓치지 않는 assertion이 공통 scenario를 검증하는지 확인한다 |

### 15.2 기록 기준

이 절에는 재현 가능한 현재 차이만 둔다. 구현 진행 상태나 누적 통과 횟수는 gap 제거 근거로
사용하지 않는다. gap을 제거하려면 위반 구현에서 해당 contract test나 E2E가 먼저 실패하고,
수정 뒤 같은 검증이 성공해야 한다.

### 15.3 여러 언어에서 확인되는 구현 차이

| ID | 목표 계약과 실제 차이 | 대상 |
|---|---|---|
| **IMP-X2** | [50 §5](24-runtime-monitoring.ko.md#5-structured-log)는 `zlink.runtime.location.store_changed`와 `not_configured`, `ready`, `degraded`, `stopped` 상태를 요구한다. Java와 C++는 이 location store 상태 log source를 게시하지 않는다 | Java · C++ |
| **IMP-X5** | [52 §4.1](26-message-flow-tracing.ko.md#41-실행-중에-기록-수준-변경)은 `off`에서 level read와 branch 외의 trace 전용 작업을 금지하고 public observer·event DTO를 제공하지 않는다. 네 runtime source에는 observer·event DTO가 public으로 남아 있으며, 실행 중 `off` 전환 뒤 event·flow context·telemetry queue item을 만들지 않는 allocation·queue contract test가 완성되지 않았다 | .NET · Java · Node · C++ |
| **IMP-X6** | [53 §4](27-flow-correlation.ko.md#4-flow를-만드는-시점)의 `origin=lifecycle`을 생성하지 않아 drain이 유발한 트래픽과 application 트래픽을 구분할 수 없다 | Java · Node · C++ |
| **IMP-X8** | [10 §5.2](07-channel-topology.ko.md)는 수동 endpoint를 지정한 역할의 automatic reconcile을 중단하도록 요구한다. Java는 store의 다른 peer도 연결하고 round-robin 대상으로 사용한다 | Java |
| **IMP-X12** | [21 §close](13-mesh-node.ko.md)는 actor가 남은 user Spot의 종료를 실패로 끝내도록 요구한다. Java의 check-then-act 경합은 actor가 존재하는 Spot을 종료해 actor location row가 제거된 Spot을 가리킬 수 있다 | Java |
| **IMP-X14** | C++는 `listPageSize`를 읽지 않아 기본 1000개 단위 page 대신 목록 전체를 한 번에 읽는다 | C++ |
| **IMP-X16** | Java는 `includeNativeDiagnostics`를 검증하지만 runtime에 적용하지 않는다 | Java |
| **IMP-X17** | [54 §4~5](28-graceful-drain-handoff.ko.md#4-target을-선택하기-전에-확인하는-조건)는 manual service topology가 하나라도 있으면 `Relocate`를 `Blocked/ManualTopologyUnsupported`로 차단하고, 각 automatic RouteMesh에 source 자신을 제외한 non-draining replacement가 최소 하나 있으며 exact RID·lifecycle generation이 source Core peer table에서 admitted·ready가 된 뒤에만 `Relocating`을 게시하도록 요구한다. Empty·source-only·all-draining snapshot은 `TargetUnavailable`이다. .NET은 local manual registration blocker, exact descriptor/Core peer fence, `Relocating` publication rollback과 Green `Ready` → old `Relocating` → relocation → old `Draining`·barrier → descriptor·owner lease release → disconnect 순서를 구현했으며 minimum replacement gate 보강을 진행 중이다. Node.js는 같은 local manual registration blocker, minimum replacement와 exact peer readiness gate, cleanup ordering을 구현했다. Multi-Mesh `Relocating` descriptor publication은 host state 변경 전에 수행하며 일부 write 또는 응답 유실이 발생하면 시도한 모든 descriptor를 `Serving`으로 되돌린다. Rollback까지 확인되면 `Blocked/StoreUnavailable`, 확인할 수 없으면 안전하게 `ForceStopped/TeardownFailed`로 끝낸다. Build와 focused topology·drain contract 38/38이 통과했다. Java·Kotlin·C++ runtime과 실제 process rolling E2E에는 같은 gate와 cleanup ordering이 남아 있다. | .NET · Java · Kotlin · C++ · process E2E |

### 15.4 E2E gate 검증력 차이

E2E gate는 실제 application과 runtime이 만든 값을 검증해야 한다. probe가 기대값을 직접 만들거나,
공통 계약과 다른 토큰을 정답으로 고정하면 구현 결함이 있어도 gate가 성공할 수 있다.

| 대상 | 목표 | 실제 차이 |
|---|---|---|
| Node `DiscoveryRegistryHa` location probe | location runtime이 관측한 role과 state를 client가 검증한다 | probe가 `serviceRole=Router`, `state=Ready`를 직접 만들어 반환하므로 runtime이 잘못된 값을 내도 client assertion이 성공할 수 있다 |
| C++ `ObservabilityOps` `OBS-A2` | [52 §3](26-message-flow-tracing.ko.md#3-공통-attribute)의 dispatch error field와 닫힌 값을 검증한다 | runner가 공통 field인 `outcome=failed` 대신 C++ 전용 `phase=error`를 요구해 계약과 다른 구현을 통과 조건으로 고정한다 |

각 gate는 다음 조건을 만족해야 gap 제거 증거가 된다.

- scenario가 요구하는 실제 server·client application을 실행한다.
- 기대값은 probe가 합성하지 않고 runtime event, metric, reply 또는 외부 store 상태에서 읽는다.
- 정상 경로뿐 아니라 결함을 재현하는 fault injection과 부정 assertion을 포함한다.
- 알려진 위반 구현이나 같은 결함을 의도적으로 넣은 변형에서 먼저 실패한다.

### 15.5 Deferred Actor Join, Context composition과 MessageContext 차이

[Actor model](14-actor-model.ko.md), [Spot·Actor membership](15-spot-actor.ko.md)과
[비동기 실행 정책](05-async-execution-policy.ko.md)은 목표 계약을 먼저 고정한다.
Runtime에는 handler-scoped `Defer`와 기본 barrier가 들어갔지만 아래 production
연결과 cross-language 검증이 남아 있다. 정식 interface가 존재한다는 사실만으로
구현이 완료되었다고 판단하면 안 된다.

| ID | 목표 계약과 현재 구현 차이 | 대상 |
|---|---|---|
| **IMP-JOIN-1** | Actor handler의 process-local `Defer`, failure cleanup, 64 Join·8 MiB 제한과 Actor queue boundary는 네 runtime에 들어갔다. .NET·Node.js·C++에는 User·Entry Spot과 timer handler가 여러 member Actor intent를 같은 handler terminal에 묶는 production dispatch 연결과 직접 E2E가 남아 있다. | .NET · Node.js · C++ |
| **IMP-JOIN-2** | Cross-node Accepted durable completion이 완전하지 않다. .NET은 target Actor materialization 뒤 cursor recovery는 제공하지만 application state·queue·timer의 aggregate materialization이 남아 있다. Java·Kotlin은 routed User Spot 경로만 완료되어 Core-native `JoinEntrySpot`·Spot 경로가 남고, Node.js·C++는 cross-node Accepted manifest와 target Actor mailbox callback 연결이 남아 있다. | .NET · Java · Kotlin · Node.js · C++ |
| **IMP-JOIN-3** | `Defer`가 Spot gate·Actor claim을 유지하고 `SpotWide`의 마지막 Yield continuation terminal을 barrier로 사용하는 focused 구현은 존재한다. `PerActor`·Entry Yield 금지, awaited cycle과 transition race를 실제 public dispatch로 검증하는 다섯 언어 E2E runner가 아직 없다. | .NET · Java · Kotlin · Node.js · C++ |
| **IMP-CTX-1** | Actor·User·Entry·Instance Spot은 factory에서 받은 exact Context를 보관하고 ID 중복 factory 인자를 제거해야 한다. Same-node는 Context를 유지하고 cross-node는 ObjectGeneration을 유지한 새 owner Context를 사용하며 source Context를 fence해야 한다. | .NET · Java · Kotlin · Node.js · C++ |
| **IMP-CTX-2** | 공통 inbound 타입은 `MessageContext`, Route·Publish·Session은 specialized MessageContext여야 한다. Send·Request·Spot Actor marker context, Actor request reply option과 이전 filter invocation 이름을 제거해야 한다. | .NET · Java · Kotlin · Node.js · C++ |
| **IMP-CTX-3** | Actor handler는 containing Spot, Actor, MessageContext와 payload를 받아야 한다. `PerActor`·Entry에서 공유 Spot state를 직렬화하려면 명시적인 Spot send/request를 사용해야 한다. | .NET · Java · Kotlin · Node.js · C++ |
| **IMP-ERR-1** | `RelocationDisabled=37`, `RelocationTargetUnavailable=38`, `RelocationFailed=39`를 같은 숫자와 의미로 공개해야 한다. | .NET · Java · Kotlin · Node.js · C++ |

구현은 exact interface, runtime queue·authority·relocation state machine, durable
manifest, contract test와 E2E를 함께 바꿔야 한다. Kotlin은 Java의 동기 `defer()`를
그대로 사용하며 coroutine wrapper를 만들지 않는다. `Defer`는 one-way terminal이
아니므로 one-way `Submit` naming과 실행 결과 계약을 재사용하지 않는다.

### 15.6 Session–Actor binding route와 disconnect 차이

[31](20-session-actor-dispatch.ko.md)은 Bind 뒤 Actor별 exact route를 저장하고 relay·request relay·
disconnect마다 Location Store를 조회하지 않으며, physical disconnect 때 current binding 전체에 자동
all-settled 통지를 수행하도록 요구한다.

| ID | 목표 계약과 현재 구현 차이 | 대상 |
|---|---|---|
| **IMP-SA-1** | 다섯 runtime의 focused contract는 Bind 때 저장한 route 사용, hidden rebind 금지, physical disconnect all-settled·dedupe·failure cleanup과 same-generation route fence를 검증한다. 그러나 실제 process connection을 끊어 여러 binding callback, no-Store relay와 stale token 거부를 함께 관측하는 E2E는 아직 없다. | .NET · Java · Kotlin · Node.js · C++ |
| **IMP-SA-2** | Relocation의 durable `Completed` 뒤 command 44·45 route switch·ACK, steady normalization 뒤 target admission을 여는 순서를 실제 process relocation과 Store 상태로 검증하지 못했다. 새 incarnation이 explicit bind 없이 기존 binding을 교체하지 않는다는 부정 assertion도 같은 E2E에 남아 있다. | .NET · Java · Kotlin · Node.js · C++ |

## 16. 언어별 구현 차이 연결

언어별 exact spec은 public interface를 정의하면서 현재 구현과 다른 항목을 함께 표시한다. 표의 구현
차이 항목은 이 문서의 §2 또는 §12 상세 절을 참조한다. 구현 진행 상태와 실행 증거는 exact spec이나
이 문서에 중복해서 기록하지 않는다.

| 언어 | exact spec 위치 |
|------|-----------------|
| `.NET` | `server/languages/dotnet/` |
| Java | `server/languages/java/` |
| Kotlin | `server/languages/kotlin/` |
| Node.js / TypeScript | `server/languages/node/` |
| C++ | `server/languages/cpp/` |
