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

구현 stage의 상태, 담당자와 실행 이력 원본은 이 문서에 기록하지 않는다. 차이가 실제로 남아 있음을
설명하는 데 필요한 최소 test 결과만 적고, 전체 실행 증거와 진행 상태는 execution ledger가 소유한다.

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
| §12.34 | Node.js, C++ | ActorRef가 공통 네 필드와 일치하지 않거나 별도 snapshot 변환 표면이 남아 있다 |
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
| §12.45 | Java/Kotlin, C++ | User Spot aggregate relocation은 command 30 source accept 뒤 participant 전체의 typed capacity bundle으로 aggregate prepare를 한 번 실행하고, target factory·Restore 뒤 같은 aggregate fence를 commit해야 한다. Standalone relocation capacity fence와 뒤늦은 aggregate prepare를 함께 사용하면 capacity를 이중 예약하므로 금지한다. `.NET`은 source accept에서 aggregate prepare를 한 번 실행하고 같은 fence로 target staging과 commit을 끝낸다 |
| §12.49 | 전 언어 | Host relocation mode와 exact application-version target 선택이 구현되지 않았다. 현재 runtime은 `PlannedMaintenance`와 `RollingUpdate`를 구분하지 않으며 언어별 target filter에도 차이가 있다 |
| §12.52 | 전 언어 | `Message Follow`의 Actor·Spot 전체 조합과 relocation payload·대량 처리·서비스 연속성 process E2E가 없다. `.NET`은 Actor·Spot route를 구현했지만 전체 matrix를 검증하지 않았고, Java/Kotlin·Node.js·C++는 Spot route 구현도 남아 있다 |
| §12.53 | 전 언어 E2E | 정식 public contract는 `SpotId` 문자열을 사용하지만 기존 process fixture와 application DTO에 `SpotRid`·`spotRid`·`spot_rid` 이름과 RoutingId 변환이 남아 있다 |
| §12.54 | `.NET`, Java/Kotlin, Node.js E2E | 여러 fixture가 제거된 handler context, Spot handle resolver, 이전 builder와 rich Store 표면을 사용해 현재 framework source와 compile되지 않는다 |
| §12.57 | Java/Kotlin | Global Actor ID request가 remote owner route를 resolve한 뒤에도 local-only dispatch에서 실패한다 |
| §12.58 | Java/Kotlin, Node.js, C++ | `.NET`은 양쪽 모두 Object Client이고 RouteMesh Channel Server membership도 없는 pair의 connection만 생략하고 `NotConnected`와 `NotRequired`를 구분한다. Object Client와 Channel Server의 동시 등록, weight `0` Server capability, public Node direct 분류, connecting expected RID와 backend Object role 설정을 함께 검증해야 한다. 다른 runtime의 같은 동작이 남아 있다 |
| §12.59 | 전 언어 | Byte 기반 Core HWM은 bindings까지 적용됐지만 Framework host 전체 Application HWM, Auto memory 계산, application receive 중단·재개, completion send permit과 public monitoring은 구현되지 않았다. Socket HWM exact type도 새 64-bit byte 계약으로 수렴해야 한다. |

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
relocation phase, source·target identity, object fence와 recovery lease를 해석한다. `RecreateOnRelocation` 또는 `PreserveStateWith`
policy가 하나라도 있거나 [Instance Spot](01-glossary.ko.md#entry-spot-user-spot과-instance-spot) factory가 하나라도 있는 host는 accepted journal, application state와
recovery payload를 보존할 Relocation Store도 정확히 하나 등록한다. Instance Spot [factory](01-glossary.ko.md#factory)가 없고 모든 factory가
`DisableRelocation`인 same-node 구성에서만 Relocation Store를 생략할 수 있다.

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
다시 해석하지 않고 `ProtocolError`로 완료하도록 고정한다. 송신 기본값은 수신 wire 선언을
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
등록된 codec만 허용하고, 알 수 없는 non-JSON 값은 handler를 호출하지 않은 채 `ProtocolError`로
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

### 12.34 Node.js·C++ ActorRef 필드 집합 불일치

[상호작용 모델 §7](03-interaction-model.ko.md#7-spot과-actor)과
[Actor 모델 §2](14-actor-model.ko.md#2-actor-identity와-서로-독립적인-상태)는 `ActorRef`를 논리
`ActorId`, `ObjectGeneration`, 현재 `MeshName`과 owner `NodeRid` 네 값으로 고정한다. endpoint,
내부 frame, location row와 Actor type은 참조에 포함하지 않는다. 별도의 public snapshot type도
제공하지 않는다.

C++ `actor_ref_t`는 `actor_type` 필드와 accessor를 추가로 노출하고,
`actor_ref_snapshot_t::to_actor_ref(actor_type)` 호출자가 snapshot에 없는 type을 다시 주입해야 한다.
Node.js `ActorRef`는 `nodeRid`, `actorId`, `generation`만 제공한다. `meshName`이 없고
`ObjectGeneration`도 `generation`이라는 이전 이름을 사용한다. 별도
`ZLinkActorRefSnapshot`과 양방향 변환 함수도 공개한다.

C++과 Node.js는 ActorRef를 네 target field로 맞추고 별도 snapshot type과 변환 함수를 public
표면에서 제거해야 한다. Actor handler 선택에 필요한 Actor type은 Actor manager와 runtime registry가
소유하며 application의 참조 복원 호출자에게 전달하지 않는다. `.NET`과 Java는 target field 계약에
맞는다.

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

- Actor와 Instance Spot factory에 연결하는 typed relocation policy와 state-preserving adapter
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
  읽으므로 application handler를 중복 실행하지 않는다. Command 35 completion은 target ACK까지 재시도한다.
  Target은 prepare, target lifecycle generation, committed authority generation, command fingerprint와
  applied marker를 deterministic Relocation Store reference에 보관한다. 같은 lifecycle에서 in-memory
  terminal이 제거되어도 exact retry를 처리한다. Process restart로 lifecycle generation이 바뀌면 이전
  command 35를 stale로 거부한다. Source cleanup state는 target completion 증거로 사용하지 않는다.
  Exact publication이면 target completion을 실행하고, 같은 authority generation의 검증된 steady
  authority만 이미 완료된 상태로 인정한다. Standalone Actor는 session route commit ACK와 unseal을
  완료한 뒤 local route를 확정하고 steady authority를 게시한다. Steady 게시 뒤에는 await 없이
  handoff admission을 연다. 따라서 unseal 실패 retry가 누락된 단계를 다시 실행한 뒤에만 steady로
  전환한다. Receipt가 24시간 뒤 만료되면 ACK를 추측하지 않고 거부한다. Preflight는
  Spot aggregate와 standalone Actor가 공유하는
  provisional capacity를 누적하고 limit `0`을 unlimited로 해석한다. 실제 두 host process 장애 E2E와 Entry Spot
  standalone Actor callback 순서 검증은 남아 있다.
- Java/Kotlin, Node와 C++은 위 단계를 일부 구현했지만 target factory·restore staging, aggregate publication,
  replay, source cleanup과 completion ACK가 하나의 production scheduler 경로로 끝까지 연결되지 않았다.
- Java/Kotlin에는 별도 `ZLinkDrainControl` 공개 표면이 남아 있어 host runtime이 termination intent와 terminal
  result를 소유하는 목표 계약과 다르다.
- `SpotWide` User Spot과 member Actor는 하나의 aggregate permit·root·commit
  generation으로 이전해야 한다.
- `.NET`은 `PerActor` User Spot의 `RecreateOnRelocation` shell과 Actor별 owner를 production
  scheduler에 연결했다. Spot authority를 먼저 target으로 바꾸고 Actor를 독립적으로
  이전한다. `ST-G3` fresh process는 Actor 100개의 generation 유지, Capture·Restore
  payload 일치, Actor별 `transfer_in` 1회와 이동 뒤 Actor·Spot dispatch를 확인했다.
  증거는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-075736-1950230`이다.
  이동 중 Create·Join과 Actor별 source·target 분할을 연속 traffic으로 관찰하는 보강
  시나리오는 Message Follow 검증이 계속 소유한다.
- `.NET` Entry Actor는 current turn과 permit wait가 끝나 queue admission을 닫은
  시점부터 target admission을 다시 열 때까지 production histogram과 warning으로
  측정한다. 정상 경로와 Capture·Restore를 각각 1.25초 지연한 실제 process E2E가
  1초 초과 후에도 같은 relocation을 완료하는지 검증한다. 별도 E2E는
  infrastructure relocation에서 application membership callback을 호출하지 않는지
  확인한다.
  `.NET`의 `PerActor` target shell authority 선전환과 Actor별 production relocation은
  연결됐다. 모든 relocation unit에 공통 1초 계측을 적용하는 작업과 Java/Kotlin,
  Node, C++의 같은 metric·warning·actual process 검증은 남아 있다.
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
  준비되지 않은 RID가 모두 `Unavailable`이 된다. [Node direct](01-glossary.ko.md#node-direct) submit 전에 target이 논리적으로 존재하는지
  판단할 기준 데이터를 조회해야 두 상태를 구분할 수 있다.
- Node.js는 expected RID를 모두 명시한 manual peer registry에서는 unknown RID를 `NotFound`로 분류하고,
  registry에 남아 있지만 ready route가 없는 RID는 `Unavailable`로 분류한다. Self RID direct submit도
  기존 node-direct dispatcher의 local admission을 사용해 결과값 없이 완료한다. Discovery mode나 expected
  RID가 없는 [manual endpoint](01-glossary.ko.md#manual-endpoint)가 섞인 구성에는 unknown RID와 이전에 연결됐던 disconnected RID를 구분할
  target이 논리적으로 존재하는지 판단할 기준 데이터가 없다. 이 구성은 Core의 pipe 결과만 사용하므로 두
  상태를 완전히 구분하지 못한다.
- JVM은 expected RID를 모두 명시한 manual peer registry에서 unknown RID를 `NotFound`로 분류하고,
  registry에 남아 있지만 ready route가 없는 RID는 `Unavailable`로 분류한다. Self RID direct submit은
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
  Ready-visible ordering, factory 실패 cleanup, lease-derived local admission deadline, bounded Message Follow와
  Relocate·close 순서가 구현되어 있지 않다. Source가 target transport 전에 Instance owner claim을
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
`InvalidOperation`으로 끝나야 한다. 같은 gate를 다시 기다리는 `Async`와
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
`InvalidOperation`으로 거부한다. RID 문자열을 parse하거나 MeshNode RID 전체를
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

`.NET`의 in-memory·provider coordinator와 Relocation envelope는 participant 전체
1,024개 상한을 제거했다. Opaque provider는 participant metadata를 최대 1,024개·
encoded 1 MiB인 leaf page로 나누고, page reference가 1,024개를 넘으면 같은 제한의
상위 page를 재귀적으로 만든다. Aggregate root에는 전체 수, leaf 순서 digest, 최상위
page reference와 level별 page 수만 둔다. Page는 version이 있는 전용 binary codec으로
인코딩하므로 JSON serializer 출력 형식이 checksum의 기준이 되지 않는다.

Commit·recovery는 key에서 정한 level·index, page checksum·크기, 연속된 range와 순서,
level별 page 수, leaf digest와 participant metadata·payload checksum을 다시 확인한다.
누락·손상·순서 변경은 `RelocationDataLost`로 처리한다. 10,000 participant는 leaf
10개로 준비·commit·cleanup되며 provider·coordinator·envelope focused test가
통과했다. 손상된 root의 전체 수는 leaf page 수×1,024 범위를 먼저 확인하므로 전체
수만으로 collection을 미리 할당하지 않는다.

다른 언어 구현과 실제 process E2E가 남아 있으므로 이 gap은 유지한다. 다음 항목을
완료해야 닫을 수 있다.

1. Java·Kotlin, Node.js와 C++ Location Store도 같은 leaf·index page와 작은 aggregate
   authority record를 저장한다.
2. 다섯 언어의 participant negotiation과 relocation staging을 한 vector가 아니라
   inventory root·count·digest와 page stream으로 처리한다.
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

### 12.52 Message Follow와 relocation workload 검증 미구현

[Config 10 Track I](../e2e/config-10-spot-actor-relocation.ko.md)는
실제 payload 크기, 대량 relocation, 서비스 연속성과 `Message Follow`를 하나의
운영 workload에서 검증한다.

`.NET` runner에는 `ST-I1`, `ST-I4`, `ST-I5`, `ST-I6`의 실제 scenario와 selector를
연결했다. I1은 Actor 4 KiB·64 KiB·8 MiB·64 MiB와 Instance Spot·`SpotWide`
64 KiB·1 MiB·32 MiB·64 MiB를 실제 cross-node relocation한다. deterministic state의
Capture·Restore byte와 opaque Relocation Store blob의 size·read-back checksum을
대조한다. 각 profile은 Store 측정을 reset한 격리 실행이다. 이 selector들은 아직
diagnostic 또는 부분 구현이므로 공식 `all`에서 실행하지 않는다. Java·Node.js·C++에는
`ST-I1~I6` selector가 없고 Kotlin에는 별도 SpotActorTransfer runner 자체가 없다.

`ST-I2/I3`가 이전에 사용한 `RelocationUnitTerminalStore`는 실제 terminal을 관찰하지
않았다. Actor는 당시 lifecycle callback에서 기록했고, Spot은 authority commit 전의
adapter `RestoreAsync`에서 기록했다. 이 값을 기준으로 target
admission 순서를 판정하면 commit 전 상태를 완료로 오인할 수 있다. Process E2E는
public `RelocateAsync` terminal, 최종 location과 `ObjectGeneration`, terminal 뒤
source·target handler 결과를 사용해야 한다. Exact authority commit과 target
admission 순서는 provider·runtime contract test가 검증한다.

추가 대조에서 다음 E2E 차이도 확인했다.

- `ST-I1-INSTANCE`는 네 profile을 source node에 만든 뒤 host relocation, target
  `Restore`, public location과 Relocation Store read-back을 확인하도록 수정했다.
  이전 activation-only 실행 결과는 relocation 통과 증거로 사용하지 않는다.
  Queue·journal·timer, permit contention, 320 MiB aggregate와 participant 5개
  반복은 아직 없다. 따라서 전체 `ST-I1` selector는 `passed`가 아니라
  `diagnostic_only`로 종료하고 blocker를 출력한다. 수정 뒤 process 실행
  `20260728-022204-146992`는 네 Instance Spot을 source에 만들었지만 5분
  relocation deadline에 도달했다. Source `Capture`는 네 개 중 세 개만
  기록했고 target `Restore`는 0건이었다. HTTP 499와 client
  `HTTP request exceeded timeout`으로 끝났으므로 완료 증거가 아니다.
- `ST-I2`는 `ST-I2-RECREATE`와 `ST-I2-SNAPSHOT`으로 분리했다. 각 selector는 fresh
  host process에서 독립 elapsed time과 units/s를 계산한다. 정본 실행은 moving target
  request·one-way를 필수로 하며 0건은 실패한다. Actor request는 original operation과
  connection-scoped correlation의 일대일 연결과 duplicate 0건을 확인한다. Queue·journal
  evidence도 집합 비교가 아니라 handler 도착 순서를 검증한다. 다만 1초 interruption,
  encoded bytes/s, payload latency, CPU, peak RSS와 Store byte 측정은 남아 있다.
  Creation-reservation production blocker를 우회하지 않고 정본 규모를 다시 실행해야 한다.
- `ST-I3`의 Spot flow correlation은 relocation traffic 시작 전 flow ID watermark로
  baseline을 제외한다. `SpotWide` 최종 owner equality는 유지 상태 확인에만 사용하며
  atomic publication 통과 증거로 사용하지 않는다. Commit 전 participant 0개 공개와 commit 뒤
  전체 공개를 함께 관찰할 수 없어 `spotwide_pre_post_visibility` blocker로 남긴다.
- `ST-G5` Entry Actor selector는 relocation 전·중·후 request와 one-way를 계속
  제출한다. Canonical replay는 `relocationReplay`를 Entry Spot request router까지
  전달한다. Target은 preserved backlog에 mailbox 순서를 먼저 배정하고 direct
  admission을 연다. Accepted request의 durable cursor 갱신과 reply relay 완료도
  accepted sequence 순서로 처리한다.
- Fresh process `logs/20260728-092319-3148154`의 `ST-G5-SMALL`은 interruption
  0.388267초, source 마지막 handler에서 target 첫 handler 또는 reply까지 377 ms,
  request 71건과 one-way 80건을 기록했다. Loss와 duplicate는 각각 0건이며 FIFO,
  원래 operation ID·deadline·request correlation을 보존했다. 이 결과는 Entry Actor
  small profile만 완료한다. Slow profile, PerActor·SpotWide·Instance Spot과
  Spot Message Follow 전체 matrix는 계속 남아 있다.
- `.NET` SpotWide payload는 Spot과 Actor의 `Capture` callback을 순차 실행한 뒤 하나의
  logical stream으로 합친다. 64 MiB 이하는 하나의 data chunk로 저장하고, 그보다 큰
  stream의 저장·read-back 확인·읽기·renew는 최대 4개와 encoded component bytes 256 MiB
  단위로 병렬 실행한다. Manifest는 모든 chunk를 확인한 뒤에만 저장하고 target은 I/O
  완료 순서가 아니라 manifest 순서로 stream을 복원한다. Focused
  `RelocationTree` test 7/7이 통과했다. 이 결과는 Store I/O 병렬성만 증명하며
  SpotWide unit의 1초 service interruption 달성을 증명하지 않는다.
- 측정하지 않은 interruption·resource·Store 수치는 추정하거나 0으로 채우지 않는다.
  `ST-I2/I3` selector는 이 blocker가 남아 있는 동안 `diagnostic_only`다.

I4~I6에는 public global Actor ID와 operation ID별 transport delivery gate를 연결했다.
Gate는 resolver가 commit 전에 선택한 delivery를 실제 submit 직전에 멈춘다. Relocation
뒤 fresh owner를 다시 찾는 호출로 대체되지 않는다. Caller와 HTTP DTO에는 owner RID,
ObjectGeneration과 Message Follow hop을 노출하지 않는다.

I4~I6와 ST-F4/F5의 process 판정에서 `Zlink.Framework.ActorHandoff`
Information log의 `message_follow_*` 문자열 의존을 제거했다. 현재 판정은 relocation 전에
resolve된 delivery를 지연한 뒤, public terminal reply와 application handler evidence를 사용한다.
Final owner handler는 정확히 한 번, 이전 owner handler는 0회여야 한다. Request는 reply marker와
original timeout을 보존해야 한다. Duration 뒤 delivery는 `ActorLocationStale`이고 target handler는
0회여야 한다.

Route entry 수, next-hop 내부값과 실제 memory 제거는 public application 계약으로 관찰할 수 없다.
이를 process E2E 성공으로 가장하지 않는다. Provider/runtime contract test에서 bounded lifecycle을
검증해야 하며, 해당 검증이 없으면 route cleanup case는 blocked 상태다.

I4는 source 처리 baseline과 commit 뒤 one-way·request를 연결했다. Source dispatch와
request reply relay를 고친 뒤 `logs/20260728-001043-1662047`에서 held one-way·request의
내부 marker를 관찰했다. 이 과거 실행은 현행 public black-box 완료 증거로 사용하지 않는다.
수정한 scenario는 final owner의 exactly-once 처리, source handler 0회와 reply marker를 확인한다.
Baseline은 source handler가
끝난 뒤 relocation을 시작하므로 seal 직전 `MF-AO-QUEUE` 증거가 아니다. `MF-AO-QUEUE`,
host relocation의 commit 전 hold, Spot 네 조합과 PerActor split는 남아 있다. I5는 Actor request correlation
역순 release, expiry와 Message Follow 중 original absolute deadline을 연결했고
`logs/20260728-002118-1750005`에서 correlation A/B exact reply, late reply 폐기와
expiry stale rejection을 관찰했다. 현행 scenario는 runtime marker 대신 같은 결과를 public
terminal과 application handler 0회로 판정한다. 수정 뒤 `logs/20260728-035609-2262342`에서
I4와 함께 focused process를 재검증했다. 이 실행은 아래에 남은 case를 포함하지 않으므로
전체 I5 완료 증거가 아니다.
Duplicate, 이전 generation, loop, 8-hop과 record·byte bound 및 Spot 조합은 남아 있다. I6은 Actor의 두 번 relocation과
delayed request를 연결했다. 현행 assertion은 public `Find`의 final owner가 정확히 한 번
처리하고 source·중간 owner handler가 처리하지 않는지 확인한다. Route cleanup 내부값은 public
black-box로 관찰할 수 없어 별도 runtime contract test가 필요하다. 첫 hop의 benign lease-renewal
StoreVersion conflict는 latest snapshot으로 재시도하도록 고쳤다. 단독 실행
`logs/20260728-003822-1911426`은 두 번째 target restore·location commit 뒤 source
cleanup completion이 끝나지 않았다. 세 번째 relocation, one-way,
recovery와 Spot 조합은 아직 없다.

이 scenario는 fixture 값을 성공 결과로 만들지 않는다. Public opaque Store 위에
provider-backed authority repository를 연결했고 Redis와 in-memory parity test가 통과했다.
2026-07-27 process 재실행은 이전 `Conflict(Missing)`을 지나 User Spot·Actor 생성과
admission 승인까지 진행했다. Reservation 기반 Actor creation commit의 authority snapshot을
local ownership coordinator에 연결하고 Actor publish를 그 뒤로 옮겨 첫 deferred Join의
`NotFound`도 수정했다. `ST-A1` process는 통과했지만 Track I 네 scenario는 각각의
남은 matrix와 process 검증을 끝내지 않아 완료 증거가 아니다.

추가 재실행에서는 fixture 자체의 배치 오류를 확인했다. `ST-B1`은
`logs/20260727-225502-168874`에서 Actor와 User Spot이 같은 node에 배치되어 실제 relocation이
일어나지 않았고 `transfer_out` 증거가 없어서 실패했다. `ST-I5`도
`logs/20260727-225133-156186`에서 deferred Join 요청의 접수 응답만 확인한 뒤 delivery gate를
해제했다. 실제 Join completion은 `InternalFailure`였고 지연 request는 source Entry Spot에서
handler를 찾지 못했다. 두 결과는 Message Follow 실패 증거가 아니라, node별 stable type과
`TargetNodeRid`에 의존한 fixture 및 deferred completion 대기 누락이다.

이 fixture는 세 node의 동일 stable type, caller 비지정 create와 public node-wide placement
weight로 고쳤다. Deferred Join endpoint도 접수 응답 대신 completion callback을 기다린다.
`ST-A1`은 `logs/20260727-230711-505861`에서 통과했다. Remote `ST-B1`은 source
Entry Spot identity, canonical phase progress, source ownership release와 unbound completion
fence를 수정한 뒤 `logs/20260727-235500-1282505`,
`logs/20260727-235556-1284429`에서 연속 통과했다. 두 실행은 Actor를 actor-a,
Spot을 actor-b에 자동 배치하고 target restore, authority convergence와 후속 probe까지 확인했다.

I1에는 Actor 외에 Instance Spot과 `SpotWide`의 `small`·`normal`·`large`·`boundary`
adapter, opaque Store 총 byte·checksum과 process peak RSS 측정을 추가했다. Instance Spot은
초기 activation-only 진단 `logs/20260727-232318-914304`에서
4 KiB·64 KiB·8 MiB·64 MiB fixture를 사용했다. 이 크기와 실행은 현행 relocation profile이나
완료 증거가 아니다. 현행 Instance Spot profile은 64 KiB·1 MiB·32 MiB·64 MiB이며,
아직 다음 process blocker가 남아 있어 `all`에 포함하지 않는다.

- Actor payload 전체 profile은 remote Join 기반 fixture를 다시 실행해야 한다.
- SpotWide small의 commit 직후 public lookup이 canonical relocation target route를
  해석하도록 고쳤다. `logs/20260728-000859-1518859`는 65,536 B application state,
  66,247 B Store put과 711 B envelope overhead의 byte·checksum·restore를 통과했다.
- Actor adapter가 정확히 64 MiB를 반환하는 독립 selector를 추가했다. 첫 실행
  `logs/20260728-004839-2032371`에서 fixture header 74 bytes를 application state에
  잘못 더한 문제를 고쳤지만 수정 뒤 process evidence는 아직 없다.
- SpotWide 64 MiB 실행 `logs/20260728-003937-1922103`은 typed runtime failure 없이
  relocation 진행 중 외부 5분 execution limit에 도달했다. 완료나 SLO 증거가 아니다.

Queue·journal·timer profile, permit contention과 320 MiB·5-participant aggregate도 아직
process scenario에 연결하지 않았다. Empty optional `SourceSpotId`를 absent로 encode하지 못한
wire 오류는 수정했지만, 위 activation과 relocation blocker가 남아 있으므로 I1 완료 증거가 아니다.

`ST-I2`, `ST-I3-INSTANCE`, `ST-I3-SPOTWIDE`에는 정본 workload와 scale 환경변수를
연결했다. 정본 기본값은 `RecreateOnRelocation` Actor 10,000개, `PreserveStateWith` Actor 1,000개,
Instance Spot 1,000개, SpotWide 100개와 Spot별 Actor 100개다. .NET production host lifecycle은
mode/options를 받는 `RelocateAsync(...)`와 별도 `ShutdownAsync(...)`를 DI에서 제공한다.
Relocation 성공 뒤에는 infrastructure를 종료하지 않고 `Relocated` 상태를 유지한다.
Public contract 67/67과 lifecycle focused unit test 40/40이 통과했다.

Target scheduler는 mode가 정한 exact application version을 Actor·Spot preflight와 실제
relocation에 동일하게 적용한다. `PlannedMaintenance`는 source와 같은 version,
`RollingUpdate`는 caller가 지정한 더 높은 exact version만 선택하며 `>=` fallback을
허용하지 않는다. 대상이 없으면 deadline까지 재조회한 뒤 `Blocked/TargetUnavailable`로
끝난다. Focused relocation test 159/159와 최신 전체 .NET Unit test 1,077/1,077이 통과했다.
축소 process smoke는 scenario 경로와 새로운 production gap을 확인했다.

- `logs/20260728-000226-1408117`: `RecreateOnRelocation` Actor 4개와 `PreserveStateWith` Actor 2개를 만든 뒤
  control Actor·Spot request와 one-way baseline은 오류 0으로 통과했다. Host relocation은
  target admission 없이 `PreserveStateWith` Actor Capture를 반복하고 terminal을 반환하지 않았다.
- `logs/20260728-005525-2344250`, `logs/20260728-010329-2556033`: `RecreateOnRelocation` Actor 1개와
  `PreserveStateWith` Actor 1개에서도 같은 nonterminal을 재현했다. Target preflight는 완료됐지만
  source relocation 내부에서 target Restore·admission은 0이었다. 동시에 remote workload
  reply가 reply capability를 보존하지 못해 retry됐다. 이 reply terminal과 source
  relocation 정지의 인과를 분리해 수정해야 한다.
- `logs/20260728-000501-1448286`: Instance Spot 2개와 같은 control traffic baseline은
  통과했다. 두 Spot Capture가 반복됐지만 target restore와 terminal에 도달하지 못했다.
- concurrent setup은 `logs/20260728-000118-1347500`에서 서로 다른 Actor creation
  reservation이 변경됐다는 conflict로 실패했다.

따라서 축소 smoke와 SpotWide small 결과를 정본 규모, relocation 완료 시간이나 서비스 연속성 완료 증거로
사용하지 않는다. 위 production terminal과 concurrent creation gap을 해결한 뒤 정본
규모에서 relocation 중·후 성공률, 누락·중복·순서와 p99를 다시 측정해야 한다.

2026-07-28 E2E 교차 리뷰에서 scenario 자체의 검증 gap도 확인했다.

- Scale 실행은 `diagnostic_only` terminal로 분리했다.
- Request와 one-way는 각각 독립 open-loop pacer로 바꾸고 offered·submitted·accepted·
  failed 수를 기록한다.
- 생성 count를 완료 수로 사용하던 출력은 제거했다. Public `RelocateAsync` terminal과
  final location을 확인한 뒤 실제 확인된 unit 수를 `completed`로 기록한다.
  `SpotWide`는 Spot aggregate 수를 completed unit으로 세고 Spot과 member Actor의
  확인 수는 `verified_participants`로 따로 기록한다.
  정상 `Relocated` 반복의 `safe_aborted`와 `blocked`는 0이며, terminal 확인 전에
  workload report를 성공으로 남기지 않는다.
- Operation ID, absolute deadline, request correlation과 handler admission 시각을
  workload evidence에 추가했다. Public `Find` 결과와 host terminal을 relocation
  전후에 대조한다. Public Actor·Spot ref에는 내부
  `AuthorityOwnerGeneration` 숫자를 추가하지 않는다. Exact fence는 provider contract
  test가 검증하고 process E2E는 이전 generation의 stale 결과와 Message Follow의
  terminal-once 동작으로 검증한다.
- SpotWide는 모든 member Actor의 final owner와 host terminal 뒤 target handler를
  확인한다. Aggregate 단일 CAS 호출 자체는 provider contract test가
  검증하고 process E2E는 participant 전체의 observable admission barrier를 검증한다.
  Spot request correlation은
  public `IZLinkRuntimeMessageFlowObserver`의 `received`·`replied` event를 대조하도록
  고쳐 별도 handler context나 private transport metadata 없이 관찰한다.
- 현재 Actor delivery gate는 E2E assembly의 `InternalsVisibleTo`, internal metadata와
  production DI hook에 의존한다. 이 결과는 application이 public API만 사용하는 최종
  Message Follow 증거가 아니다. 외부 transport harness가 Framework가 생성한 operation을
  지연하는 방식으로 교체해야 한다.

Java/Kotlin·Node.js·C++ runner에는 `ST-I1~ST-I6`이 없다. `.NET` runtime은 Actor와
Spot의 이전 route를 일정 기간 current owner로 전달한다. Java/Kotlin·Node.js·C++의
focused 구현은 Actor route만 처리하며 Spot route 연결이 남아 있다. 어느 언어도 다음
계약 전체를 process E2E로 검증하지 않았다.

Java·Node.js·C++의 공식 `all` selector는 이전 route를 caller가 직접 지정하는
전환 대상 `ST-F4/F5`도 실행한다. 시나리오를 삭제하거나 assertion을 약화하지 않는다.
각 언어가 public global Actor ID의 resolve 뒤 delivery를 transport에서 지연하는 fixture로
전환할 때까지 해당 실행 결과는 현행 `ST-F4/F5` 완료 증거가 아니다.

Java의 evidence store는 현재 route 등록 marker만 보존하고 relay·거부·route 제거
marker를 버리지만 client는 이 marker를 요구한다. Kotlin은 별도 SpotActorTransfer
runner·feature map·server module이 없다. Java runner 실행을 Kotlin runtime 증거로
사용할 수 없다. 따라서 Java `ST-F4/F5` fixture 수정과 별개로 Kotlin process E2E를
새로 연결해야 한다.

Production code 대조 결과도 다음 차이가 남아 있다.

| Runtime | Actor Message Follow | Spot Message Follow | Track I에서 남은 핵심 차이 |
|---|---|---|---|
| Java/Kotlin | generation과 1,024-message·16 MiB 제한을 검사하지만 accepted handoff packet에 original operation ID와 Message Follow hop이 없고, relay request에 original absolute deadline 대신 기본 timeout을 다시 적용한다 | 없음 | duplicate·loop, durable multi-hop recovery와 payload·bulk process 증거가 없다 |
| Node.js | Immutable internal context에 128-bit operation ID, correlation·reply route, source·target owner fence, ObjectGeneration, hop·visited-owner chain, payload checksum과 original absolute deadline을 보존한다. Route별 terminal dedupe와 loop·8-hop·1,024-message·16 MiB bound를 적용한다. 만료 request는 handler queue 전에 끝내고 late reply를 폐기한다 | codec field만 있고 route 수명과 relay를 소유하는 runtime이 없다 | restart recovery와 payload·bulk process 증거가 없다 |
| C++ | production relay는 target route를 사용하지만 1,024-message·16 MiB·hop 제한을 적용하는 admission API가 unit test에서만 호출된다. Relay request는 새 30초 envelope를 만들어 original operation·deadline·correlation을 보존하지 않고, Actor client는 Location Store에서 fresh owner를 다시 찾아 재시도한다 | 없음 | bounded committed route만 사용하는 전달, original operation·deadline·correlation 보존과 payload·bulk process 증거가 없다 |

따라서 세 runtime의 component code나 F6 결과를 Actor·Spot 전체 Message Follow 또는
Track I 완료 증거로 사용하지 않는다.

세부 계약을 production code와 대조한 결과는 다음과 같다. `부분`은 field나 helper는
있지만 모든 실제 relay 경로가 그 계약을 사용하지 않는다는 뜻이다.

| 계약 | Java/Kotlin Actor | Node.js Actor | C++ Actor | 세 runtime Spot |
|---|---|---|---|---|
| one-way·request | 구현 | 구현 | 구현 | 미구현 |
| commit 전 accepted queue | 구현 | 구현 | 구현 | 미구현 |
| commit 뒤 stale physical route Message Follow | 구현 | 구현 | 구현 | 미구현 |
| original operation ID | 미구현 | 구현 | 미구현 | 미구현 |
| original absolute deadline | 미구현 | 구현 | 미구현 | 미구현 |
| correlation·reply route | 부분 | 구현 | 미구현 | 미구현 |
| ObjectGeneration·owner generation | ObjectGeneration만 검사 | 둘 다 검사 | ObjectGeneration만 검사 | 미구현 |
| duplicate terminal·loop | 미구현 | 구현 | 미구현 | 미구현 |
| 8-hop 제한 | 미구현 | 구현 | production relay에는 미적용 | 미구현 |
| route당 1,024 messages·16 MiB | 구현 | 구현 | production relay에는 미적용 | 미구현 |
| duration·cleanup | process memory에서 구현 | process memory에서 구현 | process memory에서 구현 | 미구현 |
| restart recovery·multi-hop cleanup | 미구현 | process 내 multi-hop route와 cleanup은 구현, restart recovery는 미구현 | 미구현 | 미구현 |

여기서 `Message Follow`는 relocation commit 뒤 이전 physical route에 도착한 message를
새 owner로 전달하는 기능만 뜻한다. Session의 일반 reply relay나 relocation commit 전
accepted queue는 기존 이름을 유지한다.

`.NET`의 Spot request runtime은 absolute deadline을 wire와 inbound record에 보존하고
Message Follow hop마다 남은 시간만 전달하도록 수정했다. 만료된 ingress는 handler queue
전에 `TimedOut`으로 끝내며 late reply를 폐기한다. Focused runtime·relocation·Message Follow
test는 통과했지만, 실제 process에서 relay 중 deadline을 넘기는 `MF-DEADLINE` 반복은 아직 없다.

Node.js Actor request도 absolute deadline을 internal stream metadata, accepted handoff와
relay envelope에 보존한다. 각 hop은 남은 시간만 사용하고 만료 request를 target
handler queue 전에 `DeadlineExceeded`로 끝내며 late reply를 폐기한다. Actor route key는
ActorId, ObjectGeneration과 source authority owner generation을 사용하고 relay 전에 full
source·target owner fence를 다시 확인한다. 같은 operation ID·checksum·request·reply route의
중복은 같은 terminal Promise에 합류하므로 target handler가 한 번만 실행된다.

Node workspace build, Actor client·handoff focused 30/30과 authority transfer 3/3이 통과했다.
Fresh process `ST-F4` `log/20260728-082930-2149821`과 A→B→C `ST-F5`
`log/20260728-082947-2149807`에서는 보류한 one-way와 positive request가 final owner에서
각각 한 번 처리되고 reply도 한 번 돌아왔다. 모든 process stderr는 0 bytes다. Spot route
runtime, restart recovery, payload·bulk와 process `MF-DEADLINE` 증거는 남아 있다.

Spot public call은 Spot ID만 받고 terminal 실행 안에서 route를 찾는다. Relocation 전에
선택한 opaque route를 보존해 commit 뒤 제출하는 public operation은 없다. Message Follow가
만료된 뒤 새 global-ID call은 cache 수명 계약에 따라 current owner를 다시 찾으므로
만료된 이전 physical route delivery도 만들 수 없다. Private resolved handle, owner RID나
generation을 E2E에 노출하지 않는다. 따라서 Spot commit 후·expiry matrix는 process 밖
transport harness가 runtime이 만든 frame을 resolve 뒤 지연·복제하는 방식으로 검증해야 한다.

- Actor와 Spot의 one-way·request를 authority commit 전후에 각각 보낸다.
- operation identity, generation, deadline, correlation과 reply route를 유지한다.
- 기간 만료, duplicate, loop와 8-hop 제한을 검증한다.
- Message Follow route 하나가 대기하는 message 1,024개와 encoded byte 16 MiB 제한을 검증한다.
- 같은 object가 연속으로 이동하는 multi-hop route를 검증하고 사용이 끝난 route를 제거한다.
- 실제 encoded Actor·Spot payload와 relocation 중 control traffic의 성공률·latency를 측정한다.

Message Follow marker 이름도 같은 의미로 사용해야 한다.

- route 등록: `message_follow_registered`
- current owner 전달: `message_follow_relay`
- route 제거: `message_follow_route_removed`
- 기간 만료: `message_follow_expired`
- generation·hop·bound 거부: `message_follow_rejected`

`.NET` Actor·Spot과 Node.js Actor는 이 이름을 사용한다. Java/Kotlin production
Actor 경로는 route 등록 marker만 제공하며 relay·제거·만료·거부 marker가 없다.
C++ Actor 경로는 등록·relay·제거·만료 marker를 제공하지만 거부 marker가 없다.
Java/Kotlin·Node.js·C++에는 Spot Message Follow 자체가 없다.

언어별 `SpotActorTransfer` feature map에는 실제 구현 범위와 runtime blocker를
구분해 기록한다. Runtime marker나 단위 test만으로 process E2E 완료로 판정하지 않는다.

`.NET`은 relocation source가 commit 뒤 Store에서 읽은 실제 authority owner generation으로
Message Follow를 시작한다. 비교 규칙은 정확히 `source + 1`이 아니라 유효 범위와
`target > source`만 검사한다. Capacity reservation과 aggregate prepare가 target generation을
먼저 예약하고, target admission·factory·context와 authority commit이 같은 값을 사용한다.
Abort된 예약의 generation은 재사용하지 않는다. 따라서 중간 예약이 취소되어 target generation이
`source + 2` 이상이어도 staging과 commit이 같은 값으로 완료된다.

In-memory Store와 opaque provider adapter가 모두 예약값을 commit에 사용한다.
`.NET` unit 1,098/1,098, provider adapter focused 2/2, contract 67/67과 Redis 25/25가 통과했다.
`ST-F4` process 재검증 `logs/20260728-033031-1716434`에서도 duration 안에 해제한 operation을
target handler가 한 번 처리하고 만료 operation은 stale로 끝났다.

`.NET`의 Message Follow request reply backpressure 구현은 수정했다. Source는 direct reply
entry를 queue admission 전에 제거하지 않는다. Local direct reply와 remote reply relay는
reply capability에 보존한 original absolute deadline까지만 다시 제출한다. 같은 capability의
동시·중복 reply는 terminal claim 하나만 실행한다. Cancellation은 claim을 반환하므로 deadline
안의 재전달 한 번이 같은 entry를 이어서 사용할 수 있다. Focused Message Follow test 26/26이
backpressure 뒤 성공, deadline 종료, deadline 0의 configured fallback, cancellation 뒤 재전달,
local·remote queue admission 재시도, duplicate terminal과 deadline 뒤 remote 전송 0회를 검증했다. 실제 process E2E에서 source reply
queue backpressure를 주입하는 fixture와 `ST-I4`·`ST-I5` assertion을 연결했다. Fixture는 route,
pending request와 reply capability를 바꾸지 않고 reply submit 직전에 `Backpressured`를 반환한다.
Runtime retry loop는 이 internal transport fixture의 결과를 실제 transport와 같은 bounded retry로
처리한다. `20260728-035609-2262342`에서 ST-I4·I5 focused process가 통과했다. Actor commit 직후
Message Follow request의 reply backpressure·release·deadline은 확인했지만, Spot 조합과
duplicate·generation·loop·hop·queue bound는 아직 gap이다.

2026-07-28 reply backpressure 변경 뒤 `.NET` Unit 1,097/1,097이 통과했다.
Public Contract의 마지막 독립 실행은 67/67이다.

### 12.53 E2E fixture의 SpotRid 잔여

정식 계약에서 Spot의 논리 주소는 UTF-8 문자열 `SpotId`다. `RoutingId`는 MeshNode의
`NodeRid`에만 사용한다.

네 언어의 production framework source는 `SpotId`를 사용하지만, 여러 E2E fixture와
application DTO가 이전 `SpotRid` 이름을 유지한다. 일부 Java E2E는 이 값을 다시
`RoutingId`로 변환하므로 이름만 오래된 문제가 아니라 현재 public API와도 맞지 않는다.

모든 E2E fixture는 다음 기준으로 바꾼다.

- Spot 주소 field와 local variable은 언어 관례에 맞는 `SpotId` 이름을 사용한다.
- Spot ID를 `RoutingId`로 변환하지 않는다.
- Node의 transport identity만 `NodeRid`와 `RoutingId`를 사용한다.
- HTTP evidence DTO의 wire field도 `spotId`, `sourceSpotId`, `targetSpotId`로 통일한다.
- 변경한 fixture는 해당 언어의 현재 framework source project를 참조해 다시 build한다.

### 12.54 E2E fixture의 제거된 public API 사용

현재 framework source를 직접 참조해 build하면 다음 차이가 드러난다.

| 대상 | 확인 결과 | 주요 원인 |
|---|---:|---|
| `.NET 전체 solution` | Linux build 260 errors | Sample과 다른 E2E에 제거된 `ActorRefSnapshot`, 이전 drain API·handler context·Spot manager terminal이 남아 있고 일부 consumer project가 provider abstraction reference를 포함하지 않는다 |
| `.NET AutomaticTurnDispatch` | 5개 project compile 통과, process TD-A2·TD-D1·TD-D2·TD-F3 통과 | Runner가 canonical selector를 실제 client scenario로 전달하고 실행 수를 검증한다. `20260728-035646-2274534`에서 TD-A2 1개가 실행됐고 알 수 없는 selector는 fixture 시작 전 exit code 64로 실패한다. 내부 session route handler 네 개의 DI 등록을 복구했다. Route commit ACK가 검증한 session owner RID를 Steady authority 정리 뒤까지 보존해 ingress를 unseal한다. `20260728-050239-3599515`에서 TD-D1, `20260728-050340-3689192`에서 TD-D2·TD-F3가 실제 multi-process 실행으로 통과했다 |
| `.NET SpotActorTransfer` | ST-A1 Client·ActorNode compile 통과, current ST-A1 process 실패 | Same-node join과 Relocation Store artifact 0건은 고정했다. Runtime이 same-node authority commit evidence를 발행하지 않아 `admission -> authority_committed -> leave -> joined -> success_reply` 전체 순서를 검증할 수 없다. 증거: `logs/20260728-042012-2770643` |
| Java SpotActorTransfer | 현재 Client·Shared·ActorNode compile 통과 | 제거된 route-bearing `ActorRef` 제출은 제거했지만 `ST-F4/F5` transport delivery-delay fixture는 아직 없다 |
| Kotlin SpotActorTransfer | Client·Shared·JavaClient·ActorNode compile 통과, process ST-A1 실패 | Actor가 다른 owner에 배치되면 public global Actor ID request가 `actor is not local`로 실패하는 Java runtime cross-node routing 차이가 있다 |
| Node.js SpotActorTransfer | Client·ActorNode·Session과 workspace build가 통과한다. Native MeshNode의 relocation control을 application node-direct dispatch 전에 처리하며 internal route handler도 native dispatcher에 연결한다. Fresh `ST-F4` `20260728-082930-2149821`과 3-node `ST-F5` `20260728-082947-2149807`에서 one-way·positive request의 final-owner handler와 reply가 각각 한 번이고 모든 stderr가 0 bytes다. Runner가 delivery·terminal·중복 0을 terminal gate로 강제한다 | 현행 `ST-F4/F5` 범위의 gap 없음. Spot Message Follow와 Track I payload·bulk·restart recovery는 별도 gap이다 |
| C++ SpotActorTransfer | 강화한 ST-A1·ST-B1 contract gate 통과, process는 host compile 전 차단 | ST-A1은 same-node와 `admission -> authority_committed -> leave -> joined -> success_reply`, Relocation Store·Message Follow 0건을 요구하고 ST-B1은 commit-before-joined를 요구한다. 기존 host source에는 제거된 Spot handler context·factory/context 생성·Actor 생성자·transfer adapter·builder·location option이 남아 actual process를 시작하지 못한다 |

각 fixture는 정식 exact interface의 `MessageContext`, global `SpotId`, 현재 builder와
opaque Store 등록 표면을 사용해야 한다. 기존 scenario를 삭제하거나 assertion을 줄여
compile만 통과시키면 gap이 닫힌 것으로 보지 않는다.

`.NET` SpotActorTransfer의 이전 `probe-ref`·`send-ref` fixture도 제거했다. HTTP endpoint는
call을 제출할 process만 선택한다. Framework call은 public global Actor ID만 받으며 owner
RID와 ObjectGeneration을 application payload로 전달하지 않는다. Bounded route cache가 있는
process에서 제출한 call로 stale source delivery를 만든다. Public Store authority gap 때문에
이 fixture의 process 검증은 아직 완료되지 않았다.

`ST-F4/F5`는 application이 이전 owner의 `ActorRef`를 직접 지정하는 방식으로 검증하면 안 된다.
Public API로 global Actor ID에 operation을 제출한 뒤, resolver가 선택한 transport delivery를
fixture가 지연해야 한다. 이 fixture가 없으면 endpoint를 global Actor ID 호출로 바꿔 PASS를
만들지 않고 `전환 대상`으로 유지한다.

### 12.57 Java runtime의 cross-node Actor messaging 미연결

Actor caller는 global Actor ID만 전달한다. Framework는 Location Store에서 current owner를 찾고,
다른 node에 있으면 일반 RouteMesh transport로 request 또는 one-way를 보내야 한다.

현재 Kotlin `SpotActorTransfer ST-A1`은 public API와 automatic placement로 Actor를 `actor-b`
process에 만든다. 이후 `actor-a` process에서 같은 global Actor ID로 request하면 Java runtime이
remote route를 사용하지 않고 `actor is not local`로 실패한다. Kotlin wrapper의 signature 문제가
아니며 Java runtime의 public Actor client dispatch 연결이 빠진 것이다.

특정 Node RID에 Actor를 강제로 배치하거나 old `ActorRef`, raw route와 test-only local registry로
우회하면 안 된다. Java runtime에서 global ID resolve, exact owner fence, remote submission과 reply
correlation을 연결하고 Java·Kotlin 양쪽 process E2E로 검증해야 한다.

### 12.58 Object Client pair RouteMesh connection 생략 수렴 필요

두 MeshNode의 object role이 모두 `Client`이더라도 RouteMesh Channel Server는
application target이 될 수 있다. Automatic discovery는 descriptor에서 양쪽 object
role과 RouteMesh Server membership을 함께 확인한다. 양쪽 모두 Object Client이고
Server membership도 없을 때만 connection intent를 만들지 않는다. Manual endpoint는
handshake에서 같은 조건을 확인한 뒤 ready 전에 socket을 닫고 `NotRequired`
terminal로 끝낸다. 같은 endpoint와 configuration generation에는 reconnect를
반복하지 않는다.
Public monitoring은 이 결과를 `NotRequired`로 표시하고, 연결이 필요하지만 준비되지
않은 `NotConnected`와 구분해야 한다. `NotRequired`는 ready peer, liveness와 health
failure 집계에서 제외한다.

Object Client에는 RouteMesh Channel Server를 함께 등록할 수 있다. 이 조합은 object
placement target은 아니지만 Channel target이다. Server membership은 weight가 `0`이어도
connection 필요성을 만든다. Channel Client membership만 있는 pair는 connection을
만들지 않는다. ClientServer와 classic fanout role은 별도 topology이므로 이 판정에
포함하지 않는다. Object Client에 application Node direct handler를 등록하는 조합은
계속 socket bind 전에 거부한다.
Object Client RID를 Node direct target으로 지정하면 다른 RID로 바꾸거나 새 connection을
만들지 않고 `NotFound`로 끝내야 한다. Object Client↔Object Server,
Object Server↔Object Server와 object role `None`인 Channel-only topology의 기존
연결 규칙은 유지한다.

`.NET` low-level runtime은 automatic descriptor filtering, manual admission terminal,
startup validation과 public peer 상태를 이 계약에 맞췄다. Channel Client의 weight
`0`은 server capability로 계산하지 않는다. Focused unit 46건, targeted contract
7건과 전체 unit 1,191건이 통과했다. AutomaticTurnDispatch Session fixture도
Object Server로 바로잡아 build했다.

후속 high review의 세 finding은 닫았다. `.NET`은 target을 `Unknown`,
`ObjectClientTarget`, `RequiredNotConnected`, `ReadyEligible` 중 하나로 분류한다.
따라서 연결이 필요하지만 Ready가 아닌 peer와 Node direct target이 아닌 Object Client를
내부에서 구분하고, public error는 각각 `Unavailable`과 `NotFound`로 투영한다. Admission 전 peer
snapshot은 expected RID를 사용해 descriptor와 Connecting peer를 한 항목으로
표시한다. Internal backend의 `SetObjectRole`은 모든 구현이 제공해야 한다.

기존 증거는 public automatic·manual Node direct, Connecting peer 단일 표시,
Server→Client의 `NotConnected`와 Client-only pair의 `NotRequired`를 검증했다.
Object Client와 RouteMesh Channel Server의 동시 등록, local·remote Server membership,
weight `0` Server가 connection을 유지하는 새 회귀 증거를 추가해야 한다.

Framework가 bound Session의 owner에게 보내는 route seal·abort·commit·unseal은 application
Node direct 호출이 아니다. `.NET`은 이 네 control packet만 허용하는 내부 request 경로를 사용하고,
public `IZLinkRouteClient`는 Object Client target을 계속 `NotFound`로 거부한다. Low-level
internal relay와 public 거부 focused test는 통과했다. Bingo process smoke는 cross-node Actor authority
commit 뒤 handoff completion reply ACK가 caller로 돌아오지 않아 아직 완료되지 않았다. 이 문제는
Object Client target 분류가 아니라 request reply correlation의 남은 gap이다.
`NotRequired`는 peer 목록에만 남고 retry·liveness·health failure·ready count에서
제외된다.

`.NET` Linux actual-process Config 1 `RM-A3`는 Automatic·Manual Client pair의
`NotRequired`, 20초 reconnect 억제, 연결이 필요한 peer의 `NotConnected`, 정상
Server↔Client·Server↔Server 연결을 확인했다. RouteMesh Channel Server가 있는
Object Client pair의 Ready 연결과 weight `0` 대조군은 다시 실행해야 한다. Config 13 `SA-E2E-08`도 manual Ready
Object Client를 Node-direct target으로 지정했을 때 Send·Request가 `NotFound`이고
peer 수가 변하지 않음을 확인했다. Java/Kotlin, Node.js와 C++ runtime에는 같은 연결
생략, manual terminal과 public 상태 투영을 구현해야 한다.

### 12.59 Framework inbound dispatch backpressure 미구현

[Framework API](06-framework-api.ko.md#21-수신-payload가-memory를-계속-늘리지-않게-한다)는 host 전체에서
handler가 아직 끝나지 않은 application payload byte를 계산하고, HWM에 도달하면 새 application receive만
중단하도록 요구한다. 이미 받은 job, 별도 Completion connection의 request reply·bounded Framework service
control과 Core send-ready callback은 계속 처리해야 한다. 현재 Completion API는 request reply만 전달하므로
liveness·admission·relocation·reply recovery command를 같은 physical connection에서 전달하는 Core·bindings
capability가 남아 있다.

Core와 네 bindings는 connection별 64-bit byte HWM과 Application·Completion connection pair를 제공한다.
현재 다섯 Framework runtime에는 host 전체 byte accounting, Auto HWM 계산, receive pause·resume와
completion send permit 연결이 없다. Configuration과 host status의 새 exact interface도 구현 source와
package export에 아직 없다. `.NET` 기준 구현과 회귀 test를 먼저 완료하고, 나머지 언어는 같은
accounting·ordering·startup error를 이식한다.

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
| **IMP-X5** | [52 §4.1](26-message-flow-tracing.ko.md#41-실행-중에-기록-수준-변경)은 `off`에서 level read와 branch 외의 trace 전용 작업을 금지하고 public observer·event DTO를 제공하지 않는다. Java·Node.js·C++ runtime에는 observer·event DTO가 public으로 남아 있으며, 실행 중 `off` 전환 뒤 event·flow context·telemetry queue item을 만들지 않는 allocation·queue contract test가 완성되지 않았다. .NET은 public observer·event DTO와 file·label 설정을 제거했고 `off`에서 operation ID, flow context와 Activity를 만들지 않는 focused test를 통과했다. | Java · Node · C++ |
| **IMP-X6** | [53 §4](27-flow-correlation.ko.md#4-flow를-만드는-시점)의 `origin=lifecycle`을 생성하지 않아 drain이 유발한 트래픽과 application 트래픽을 구분할 수 없다 | Java · Node · C++ |
| **IMP-X8** | [10 §5.2](07-channel-topology.ko.md)는 수동 endpoint를 지정한 역할의 automatic reconcile을 중단하도록 요구한다. Java는 store의 다른 peer도 연결하고 round-robin 대상으로 사용한다 | Java |
| **IMP-X12** | [21 §close](13-mesh-node.ko.md)는 actor가 남은 user Spot의 종료를 실패로 끝내도록 요구한다. Java의 check-then-act 경합은 actor가 존재하는 Spot을 종료해 actor location row가 제거된 Spot을 가리킬 수 있다 | Java |
| **IMP-X14** | C++는 `listPageSize`를 읽지 않아 기본 1000개 단위 page 대신 목록 전체를 한 번에 읽는다 | C++ |
| **IMP-X16** | Java는 `includeNativeDiagnostics`를 검증하지만 runtime에 적용하지 않는다 | Java |
| **IMP-X17** | [54 §4~5](28-graceful-drain-handoff.ko.md#4-target을-선택하기-전에-확인하는-조건)는 manual service topology가 하나라도 있으면 `Relocate`를 `Blocked/ManualTopologyUnsupported`로 차단하고, 각 automatic RouteMesh에 source 자신을 제외한 non-draining replacement가 최소 하나 있으며 exact RID·lifecycle generation이 source Core peer table에서 admitted·ready가 된 뒤에만 `Relocating`을 게시하도록 요구한다. Empty·source-only·all-draining snapshot은 `TargetUnavailable`이다. .NET은 local manual registration blocker, exact descriptor/Core peer fence, `Relocating` publication rollback과 Green `Ready` → old `Relocating` → relocation → old `Draining`·barrier → descriptor·owner lease release → disconnect 순서를 구현했으며 minimum replacement gate 보강을 진행 중이다. Node.js는 같은 local manual registration blocker, minimum replacement와 exact peer readiness gate, cleanup ordering을 구현했다. Multi-Mesh `Relocating` descriptor publication은 host state 변경 전에 수행하며 일부 write 또는 응답 유실이 발생하면 시도한 모든 descriptor를 `Serving`으로 되돌린다. Rollback까지 확인되면 `Blocked/StoreUnavailable`, 확인할 수 없으면 안전하게 `ForceStopped/TeardownFailed`로 끝낸다. Build와 focused topology·drain contract 38/38이 통과했다. Java·Kotlin·C++ runtime과 실제 process rolling E2E에는 같은 gate와 cleanup ordering이 남아 있다. | .NET · Java · Kotlin · C++ · process E2E |
| **IMP-X18** | .NET의 relocation 예약·prepare·commit 결과 타입 일부는 Framework 내부 coordinator와 Store adapter만 사용하지만 assembly의 public 타입으로 남아 있다. 이번 generation 보정은 새 public API를 추가하지 않는다. 결과 타입을 provider SPI 뒤로 숨기고 application public surface에서 제거하는 작업은 별도 POSD public-boundary 변경으로 수행한다. | .NET |

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
