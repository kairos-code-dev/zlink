# Kotlin — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> Java 런타임을 공유하므로 **Kotlin 고유 표면(`suspend`·`Flow`·DSL)**의 갭만 여기 둔다. 런타임 동작 갭은 [java](java.ko.md)가 소유한다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 8건. 완료 0건.**

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.3** — 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)
- [ ] **§12.14** — Kotlin option helper가 수신 한도를 되돌린다 (Kotlin)
- [ ] **§12.19** — typed 표면 경계 (Java, Kotlin)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 언어별 표면 차이 상세

### §12.3 근거 없는 공개 표면과 connect 상태 처리 (Java, Kotlin)

**계약 위반(Java).** 다음 두 표면은 공통 스펙에 근거가 없고 다른 언어에도 없다.

- connector `disconnect()` / `reconnect()` — [32 §6](../stream-connector/32-stream-connector.ko.md)의 연결 lifecycle
  표면은 connect / close / dispatch 셋뿐이며, 재연결은 자동 reconnect 옵션이 담당한다.
  **Kotlin wrapper(`ZLinkKotlinStreamConnector`)도 같은 두 메서드를 그대로 위임 노출한다.**
- **`connect()`가 진행 중인 연결 시도를 기다리지 않는다.** `Connecting`이나 `Reconnecting`
  상태에서 다시 호출하면 기존 시도를 기다리지 않고 새 연결 시도를 시작하며, 예약된 reconnect
  작업은 scheduler에 그대로 남는다. 계약은 진행 중인 시도의 결과를 기다리는 것이다
  ([32 §6](../stream-connector/32-stream-connector.ko.md)).
- `ZLinkActorPlacement(preferredNodeRid, routeMesh)` — [22 §4](../server/22-actor-model.ko.md)와
  [31 §10.2](../server/31-session-actor-dispatch.ko.md)는 remote node를 직접 지정하는 actor 생성 표면을 두지
  않는다고 규정한다.

### §12.14 Kotlin option helper가 수신 한도를 되돌린다 (Kotlin)

**미충족(Kotlin).** Kotlin의 compression option helper가 options를 복사할 때
`maxReceivedMessages`를 전달하지 않는 constructor overload를 골라, 사용자가 지정한 값을
`Integer.MAX_VALUE`로 되돌린다. wrapper는 buffering 정책을 바꾸면 안 되며 모든 option 값을
보존해야 한다([languages/java/03 §13](../stream-connector/languages/java/03-stream-connector.ko.md)).

### §12.19 typed 표면 경계 (Java, Kotlin)

**미충족.** 두 항목이다.

- Java `send(Object)`가 raw `ZLinkStreamEncodedPayload`도 그대로 받아 typed 경로에서 처리한다.
  raw payload는 raw 표면이 소유해야 한다.
- Kotlin wrapper에 목표 계약에 없는 request `await<T>()` overload 2개(typed·raw)가 있다. 목표
  선언에 없는 공개 표면은 두지 않는다.

## 라운드 2 (2026-07-14)

**Kotlin 고유 갭은 새로 나오지 않았다.** Kotlin은 Java 런타임을 공유하므로 라운드 2의 Java 항목
(**IMP-JV-11 ~ IMP-JV-20**)과 교차 언어 항목(**IMP-X5·IMP-X6**)이 **그대로 적용된다.**
[java 체크리스트](java.ko.md)를 함께 본다.

## 라운드 3 (2026-07-14)

**Kotlin 고유 갭은 이번에도 나오지 않았다.** 공개 표면 전체가 Kotlin 카탈로그와 일치한다.

Kotlin은 Java 런타임을 공유하므로 라운드 3의 Java 항목(**IMP-JV-21 ~ IMP-JV-33**)이 **그대로
적용된다.** 특히 아래 둘은 Kotlin 사용자에게도 그대로 열려 있다.

- **IMP-JV-21** — `systems.zlink.framework.execution`의 내부 실행기가 public이다. Kotlin 앱도
  spot의 turn 큐에 직접 작업을 밀어 넣거나 공유 worker pool을 `close()`할 수 있다.
- **IMP-JV-24** — Spring host 자동 drain이 25초다(스펙 30초). Kotlin Spring Boot 앱도 같은 경로를 탄다.

[java 체크리스트](java.ko.md)를 함께 본다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

> **"Kotlin 고유 갭은 없다"는 앞의 판단을 정정한다.** 그건 **런타임에만** 맞다.
> **샘플과 e2e는 별개 코드베이스이고, Kotlin은 양방향으로 갈린다.**

**Kotlin이 더 나은 축**: SupportChat 자동 연결, ShoppingMall의 읽기 전용 query와 완전한 §15 단언,
DeliveryDispatch의 actor relay(Java는 건너뛴다).

**Kotlin이 더 나쁜 축**:

- [ ] **SMP-KT-01** (**절대 규칙 위반**) — TicTacToe 밖 샘플이 **수동 연결을 쓴다**(12곳)
- [ ] **SMP-KT-02** (결함) — ShoppingMall이 **문서가 "사라진다"고 한 saga 오케스트레이터를 되살렸다** — 비내구 in-process 큐라 크래시 시 continuation을 잃는다(무손실 요구 위반)
- [ ] **SMP-KT-03** (미구현) — ShoppingMall에 **HTTP edge가 없고**, 내부 메시지 `ContinueOrderWorkflowReq`를 **클라이언트가 직접** 보낸다
- [ ] **SMP-KT-04** (미구현) — GameQuest·ShoppingMall에 **owner Spot이 없다**
- [ ] **SMP-KT-05** (결함) — 샘플 6개 중 **2개가 coroutine 표면을 아예 안 켠다**(`useCoroutineHandlers` 미호출). ShoppingMall은 `suspend` 안에서 **`LockSupport.parkNanos`로 블로킹**한다
- [ ] **SMP-KT-06** (결함) — DeliveryDispatch 기본 클라이언트가 **HTTP 폴링 루프**다(규약 금지)
- [ ] **SMP-KT-08** (결함) — DeliveryDispatch가 모든 고객 상태 push를 **`customer-1` actor로 보낸다**
- [ ] **SMP-KT-09** (**실패할 수 없는 단언**) — DeliveryDispatch client가 상태 push의 **도착 순서를 검증하지 않는다**
- [ ] **SMP-KT-10** (미구현) — Bingo의 번호 매긴 release gate가 **join·start·card·draw·reward 필드를 빠뜨린다**
- [ ] **SMP-KT-11** (미구현) — TicTacToe의 번호 매긴 release gate가 **topology·player·join·milestone 필드를 빠뜨린다**
- [ ] **E2E-KT-01** (결함) — **SpotService의 "클라이언트"가 사실 framework 호스트**다 — `@EnableZLinkFramework`로 mesh를 등록하고 `outbound.requestToSpot(...)`을 **클라이언트 코드에서 직접** 호출한다. **Java 쪽은 깨끗하다**
- [ ] **E2E-KT-02** (결함) — **Config 10이 2/20**이고 나머지 18개를 **Java 클라이언트에 위임**한다. `Shared/`도 `feature-map`도 없고 Redis 컨테이너 접두사도 틀렸다
- [ ] **E2E-KT-03** (결함) — Config 2에서 시나리오 **6개 누락**. `RC-A6`(**P0**)는 클라이언트 시나리오 없이 **셸 `grep`으로** 검증하는데 feature-map은 "구현 완료"로 적는다
- [ ] **E2E-KT-04** (결함) — `DiscoveryRegistryHa`에 **`Client/Scenarios/`가 없다** — 32줄 `when`이 477줄 god-context로 분기한다(규약이 금지한 `AllScenario` 형태)

## 라운드 6 — E2E 전 config 구성 축·Config 1 심층

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-KT-07** (결함) | [E2E README §2.1:150-177](../../common/e2e/README.ko.md): readiness 기본값은 **3초**, poll은 0.1초이며 runner 상단의 명시적 상수로 둔다 | `SpotService/run_e2e.sh:31-33`은 30초, `RuntimeMonitoring/run_e2e.sh:23-24`와 `DiscoveryRegistryHa/run_e2e.sh:29-30`은 60초다. `RegistryMessaging`·`PubSub`도 `LOCAL_READINESS_ATTEMPTS=200`과 0.1초 poll로 20초를 허용한다(`RegistryMessaging/run_e2e.sh:12-14`, `PubSub/run_e2e.sh:24-26`). 긴 대기가 네 config의 수렴 실패를 가린다 |
| **E2E-KT-08** (미구현) | [E2E README:499-512](../../common/e2e/README.ko.md): 기본 외에 **reverse 1회 + 고정 seed shuffle 1회**를 최소 실행한다 | `e2e-kotlin/run_e2e_all.sh:24-28,50-53`은 모든 config에 `all`만 한 번 전달한다. `E2E_START_ORDER`를 실제로 읽는 Kotlin runner는 `ToActorMessaging/run_e2e.sh:15-16` 하나뿐이며 통합 게이트가 reverse/shuffle을 호출하는 곳은 0건이다. ⇒ 대부분 config는 축이 없고, 유일하게 구현한 config도 기본 게이트에서 forward만 돈다 |
| **E2E-KT-09** (미구현) | [E2E README:487-497,546-547](../../common/e2e/README.ko.md): Config 2·9 P0는 **route mesh 없음 × session/spot 분리 배치** 조합을 실행한다 | Config 2의 Play와 Session이 route mesh를 조건 없이 등록한다(`SpotService/.../PlayApplication.kt:82-98`, `.../SessionApplication.kt:78-84`). runner의 P0 topology selector에도 route mesh 제거 변형이 없다(`SpotService/run_e2e.sh:734-765`). Config 9는 E2E-JV-16과 같은 두 역할(actor/caller)뿐이라 session 분리 자체가 없다. ⇒ 요구 조합이 생성되지 않는다 |
| **E2E-KT-10** (**가짜 통과**) | [config-1 RM-C2:174-182](../../common/e2e/config-1-location-messaging.ko.md)는 미존재 rid의 **public error**를, [RM-C4:194-202](../../common/e2e/config-1-location-messaging.ko.md)는 **timeout + 늦은 handler 완료**를 요구한다 | `RmC2TargetedRouteScenario.kt:26-27`은 앱이 만든 `failed` boolean만 본다. `RmC4TimeoutIsolationScenario.kt:11-24`도 `failed` 하나와 follow-up evidence만 보고 slow 요청의 완료 evidence는 검사하지 않는다. ⇒ 임의 예외나 즉시 실패도 두 시나리오를 통과시킨다 |
| **E2E-KT-11** (미구현) | [config-1 RM-C8:228-238](../../common/e2e/config-1-location-messaging.ko.md): 상한 근접 왕복뿐 아니라 **`MaxMessageSize` 초과 거부와 이후 회복**을 검증한다 | `RmC8PayloadRoundTripScenario.kt:15-30`은 1 B·4 KiB·256 KiB·1 MiB 성공 왕복만 보낸다. 초과 payload와 실패 단언은 없다. 그런데 `feature-map.ko.md:23-24`는 상태를 `implemented`로 쓰고, `:28-29`에서 후반부 미구현을 별도 설명으로만 인정한다 |
| **E2E-KT-12** (결함) | [E2E README §2.6:336-353](../../common/e2e/README.ko.md): **로그/evidence 경로를 JVM system property로 전달하지 않는다** | Kotlin E2E의 `System.getProperty` 세 곳은 모두 `java.io.tmpdir`을 **로그 경로 기본값**으로 읽는다 — `RegistryMessaging/.../ConsumerOptions.kt:30`, `.../Provider/.../ServerOptions.kt:37`, `.../Workflow/.../ServerOptions.kt:30`. 즉 금지 대상을 정확히 읽고 있으며, 이 config의 역할 서버 셋이 CLI/config 파일 밖의 JVM 전역 상태에 의존한다 |
| **E2E-KT-13** (미구현) | [E2E README:514-519](../../common/e2e/README.ko.md): 표면을 넘은 actor ref는 **`generation > 0`**을 어서션한다 | `EnsureActorHandler.kt:18-25`가 generation을 응답에 싣고 `RemoteActorAuthHandler.kt:26-35`가 그대로 `ActorRef` 생성에 사용하지만 양수 검사는 없다. client-visible `ActorAuthRes`는 generation을 아예 버린다(`SpotService/Shared/.../Contracts.kt:263-270`). Kotlin E2E production source 전체에 generation 양수 단언은 0건이다 |

### Kotlin 샘플 추가 감사 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-KT-01** | `common/sample/README.ko.md:133-142` — 서버 간 연결은 공유 location store 기반 자동 연결로 구성한다. **절대 규칙: TicTacToe만 수동 연결을 사용할 수 있다.** `EnableClient(endpoint)`·`ConnectRouter(...)`·`ConnectPeerPub(...)`를 쓰지 않으며, "위반이 하나라도 있으면 해당 샘플 변경은 완료된 것으로 판단하지 않는다" | TicTacToe 밖 샘플의 수동 연결 **12곳** — `Bingo/Server/Api/.../ApiServerApplication.kt:42-43`(`enableClient(PlayAChannelEndpoint)`·`enableClient(PlayBChannelEndpoint)`), `Bingo/Server/Session/.../SessionServerApplication.kt:49`(`connectRouter`), `Bingo/Server/Play/.../PlayServerApplication.kt:70`(`connectPeerPub`), `GameQuest/Server/GameApi/.../Program.kt:99`(`enableClient(endpoint)`), `DeliveryDispatch/Server/CourierSession/.../CourierSessionApplication.kt:39,43`, `.../Dispatch/.../DispatchServerApplication.kt:53,57`, `.../CourierGateway/.../CourierGatewayApplication.kt:41,45`, `.../CourierSpotNode/.../CourierSpotNodeApplication.kt:41`(모두 `connectRouter`). ⇒ location store 등록·조회·연결 lifecycle이 끊겨도 이 샘플들은 초록으로 뜬다 — 자동 연결 결함이 드러날 자리를 수동 endpoint가 메워 준다 |
| **SMP-KT-02** | `common/sample/event/shoppingmall.ko.md:219` — "saga 오케스트레이터 / 단계 소비자"는 **사라지고** owner spot이 이벤트 접기로 다음 단계를 판정해 직접 진행한다. `:555-566` — `StartOrderWorkflowReq` handler는 `Created`까지만 돌리고 같은 흐름 안에서 **`ContinueOrderWorkflowReq` 호출을 기다리지 않고 예약**한다. 이는 crash 뒤 재개와 **같은 메커니즘**이다 | `Server/OrderWorkflow/.../handlers/StartOrderWorkflowHandler.kt:22` — framework 재개 호출 대신 `continuations.enqueue(request.orderId)`로 프로세스 메모리 큐에 넣는다(`WorkflowContinuationQueue.kt:11-16`, 순수 `LinkedBlockingQueue`). `WorkflowSagaWorker.kt:13-27,37-53`이 daemon Thread 하나(`"shoppingmall-workflow-saga-worker"`)로 그 큐를 빼서 `workflow.continueWorkflow(orderId)`를 **직접** 호출한다. ⇒ 문서가 사라진다고 한 **saga worker가 이름까지 그대로 살아 있고**, 큐가 비내구·프로세스 로컬이라 노드가 죽으면 대기 중이던 continuation이 사라진다. 재개가 framework 메시지 경로를 타지 않으므로 owner 라우팅·순서 실행 보장도 받지 못한다 |
| **SMP-KT-03** | `common/sample/event/shoppingmall.ko.md:18,266,305` — 클라이언트는 `CommerceApi`에 **HTTP로** 주문 시작·상태 조회를 요청하고, 클라이언트가 마주하는 창구는 `CommerceApi` 하나뿐이다. `:450-452,559-561` — `ContinueOrderWorkflowReq`는 owner spot이 **자기 자신에게 예약하는 내부 재개 명령**이다 | `Server/CommerceApi/.../CommerceApiApplication.kt:67` — `.web(WebApplicationType.NONE)`. HTTP endpoint가 0개다(`@RestController`·`@PostMapping` grep 0건). 클라이언트는 대신 **자기가 framework 호스트**가 되어(`Client/.../ClientApplication.kt:18-19,29-35`) `requestToChannel(commerceApiChannel(...), ...)`로 채널 요청을 보내고, `ShoppingMallClientScenario.kt:133-141`에서 내부 재개 명령 `ContinueOrderWorkflowReq`를 **클라이언트가 직접** 보낸다. `.NET`은 `CommerceApi`에 `MapPost`/`MapGet` HTTP edge를 둔다(`dotnet/samples/ShoppingMall/Server/CommerceApi/Program.cs`). ⇒ "HTTP 진입 + 내부 메시지 은닉"이라는 이 샘플의 구도가 뒤집혔다 |
| **SMP-KT-04** | `common/sample/event/gamequest.ko.md:13,19,124-127` — player별 quest 판정은 **`PlayerQuestSpot`**이 맡고 `PlayerId` 기준 owner로 event를 직렬 처리한다. `common/sample/event/shoppingmall.ko.md:350` — 어느 `CommerceApi`로 들어와도 owner 라우팅이 항상 같은 **`OrderWorkflowSpot`**으로 보낸다 | 두 샘플 소스 전체에 `spot` 문자열이 **0건**이다(`samples/kotlin/{GameQuest,ShoppingMall}/**/src`). GameQuest는 `Server/QuestMission/.../Program.kt:79-81`에서 평범한 client-server channel(`questOwnerChannelFor(instanceName)`) + `quest-owner` handler group으로 끝나고, ShoppingMall은 `Server/OrderWorkflow/.../OrderWorkflowApplication.kt:43-45`가 `workflowChannel(instanceId)` server 하나다. owner 선택도 framework spot 배치가 아니라 앱이 한다 — `Server/CommerceApi/.../OrderWorkflowRouter.kt:38-39`의 `topology.workflowInstanceForOrder(orderId)`. ⇒ 이 두 샘플의 존재 이유인 **owner spot의 직렬 실행·이동·location 조회를 한 번도 실행하지 않는다** |
| **SMP-KT-05** | [kotlin guide 02 §coroutine handler 켜기:30-33](../../kotlin/guide/02-getting-started.ko.md) — `useCoroutineHandlers(...)`는 suspend handler를 실행할 dispatcher/scope를 지정하는 설정이다. [kotlin guide 03 §6:53-55](../../kotlin/guide/03-concepts.ko.md) — **"handler 안에서 blocking 호출(`Thread.sleep`, blocking JDBC, `CompletableFuture.join` 등)을 직접 쓰지 않는다."** 불가피하면 `withContext(Dispatchers.IO)`로 옮긴다 | 6개 샘플 중 **DeliveryDispatch(0건)와 ShoppingMall(0건)만 `useCoroutineHandlers`를 부르지 않는다**(Bingo 6, SupportChat 6, GameQuest 4, TicTacToe 4). 그러면서 두 샘플 모두 suspend handler를 쓴다 — DeliveryDispatch 20개 파일, ShoppingMall 12개 파일이 `ZLinkSuspending*`를 구현한다. ⇒ suspend handler가 앱이 지정하지 않은 dispatcher에서 돈다. ShoppingMall은 그 위에 `suspend` 함수 안에서 **carrier 스레드를 park한다** — `Server/CommerceApi/.../OrderWorkflowRouter.kt:38,49`의 `private suspend fun request(...)`가 재시도 사이에 `LockSupport.parkNanos(...)`를, `.../StartOrderUseCase.kt:87,98`의 `private suspend fun forwardToOwner(...)`도 같은 호출을 쓴다(`delay(...)`가 아니다) |
| **SMP-KT-06** | `common/sample/README.ko.md:365-371` — push message 대기는 **sample-local polling 함수가 아니라 stream connector의 public wait interface**를 사용한다. "notification 수집용 inbox나 로그 queue는 결과 출력과 추가 검증을 위해 둘 수 있지만, push 도착을 기다리는 기준 경로가 되어서는 안 된다." `common/sample/deliverydispatch/README.ko.md:689-691`도 같은 규칙 | `DeliveryDispatch/Client/.../Program.kt:46-56` — `--stream-runtime` 플래그가 **없으면 기본 경로가 `runScaffold(options)`**다. `:359-372`의 `waitNotifications`가 `/notifications?deliveryId=` HTTP GET을 **50ms 간격으로 5초까지 반복**하고(`:375-393` `readNotifications`, 원시 `java.net.http.HttpClient`), 그렇게 모은 목록으로 상태 순서를 판정한다(`:71,78`). connector 경로(`runStreamRuntime`, `:110-139`)는 opt-in이며 릴리스 runner만 그 플래그를 넘긴다(`samples/kotlin/DeliveryDispatch/run_sample.sh:131`). ⇒ 릴리스 게이트는 connector를 타지만, **샘플 client가 규약이 금지한 polling 루프를 기본 모드로 들고 있고** 같은 시나리오를 검증하는 client가 두 갈래로 갈려 있다 |
| **SMP-KT-08** | `common/sample/deliverydispatch/README.ko.md:300,478-482` — 배송 생성 요청의 `CustomerId`가 해당 배송의 고객을 정하며, Tracking의 상태 변경은 `CustomerEntry`와 `CustomerActor`를 거쳐 **그 고객의 stream client**로 push된다 | `DeliveryStatusChangedHandler.kt:25-35` — 요청의 배송과 무관하게 actor directory에서 항상 **`customer-1`**을 찾고 `DeliveryStatusUpdatedMsg.customerId`도 같은 상수로 채운다. `SubscribeDeliverySessionHandler.kt:28-45`도 모든 stream session을 `customer-1` actor에 bind한다. ⇒ `CreateDeliveryReq.customerId`가 다른 배송의 상태도 `customer-1`에게 가고, 실제 고객은 push를 받지 못한다 |
| **SMP-KT-09** | `common/sample/deliverydispatch/README.ko.md:671-687` — 성공 배송은 `Assigned → Accepted → PickedUp → Delivered`, 재배정 배송은 `Assigned → Reassigned → Accepted → Delivered`가 **도착한 순서대로** 검증되어야 한다 | `DeliveryDispatch/Client/Program.kt:151-154,181-184,205-208,237-240` — 상태별 독립 `waitFor` 네 개를 먼저 걸고, 나중에 기대 순서로 `await()`한다. 네 future가 어떤 순서로 완료됐는지는 기록하지 않는다. ⇒ `Delivered`가 `Assigned`보다 먼저 도착해도 네 종류가 모두 있기만 하면 통과한다 |
| **SMP-KT-10** | `common/sample/bingo/README.ko.md:567-584` — join push의 전적, **두 client의** game-start, card 제출 응답의 두 9칸 card, draw 양쪽 state 일치, reward의 `RoomId`·`DrawSeq`까지 단계별로 직접 확인한다 | `BingoClientScenario.kt:75-78`은 join actor id와 **client 1의 start만** 보고 전적과 client 2 start를 읽지 않는다. `:80-105`는 두 card 응답에서 status만 보고 card를 검사하지 않는다. `:107-119`는 draw의 seq·number만 비교하고 양쪽 state가 같은지 확인하지 않는다. `:138-144`는 reward의 `DrawSeq`를 확인하지 않는다. ⇒ 이 필드들이 비거나 서로 달라도 release gate가 통과한다 |
| **SMP-KT-11** | `common/sample/tictactoe/README.ko.md:523-557` — `PlayEndpoints` 두 개와 전체 `PlayNodes` 매핑, host·guest의 display name/level, join push의 `DisplayName`·`Level`·`RoomId`, milestone의 `DisplayName`·`RoomId`를 확인한다 | `TicTacToeClientScenario.kt:48-64`는 endpoint 수·중복과 전체 `PlayNodes` 매핑을 단언하지 않고 guest의 display name·level도 읽지 않는다. `:86-90`은 join push에서 actor id·mark·status만 확인한다. `:172-175`는 milestone에서 wins와 receiving rid만 확인한다. ⇒ topology 매핑이나 사용자 필드가 잘못되어도, 비-owner endpoint 하나와 rid 하나만 남아 있으면 통과한다 |

## 라운드 4 상세 — 샘플 · E2E (뒤늦게 채운 근거)

**이 절은 라운드 4 체크리스트(SMP-KT-01~06 · E2E-KT-01~04)의 근거를 채운 것이다.**
당시 체크리스트만 적고 계약↔구현 대조를 남기지 않아 작업자가 집어서 고칠 수 없었다.

### 샘플

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-KT-04** (미구현) | [shoppingmall](../../common/sample/event/shoppingmall.ko.md)·[gamequest](../../common/sample/event/gamequest.ko.md): 두 샘플의 **존재 이유가 owner spot**이다 — ShoppingMall은 `OrderWorkflow`가 주문 owner spot을, GameQuest는 `PlayerQuestSpot`을 spot-mesh로 분산해 event-sourced aggregate를 소유한다 | `samples/kotlin/ShoppingMall`·`samples/kotlin/GameQuest` — **`addSpotMesh` 호출 0건, Spot 클래스 0건.** owner spot이 **아예 없다.** ⇒ 두 샘플이 보여 주기로 한 축(owner routing · spot-mesh 분산 · aggregate 소유)이 **하나도 구현되지 않았다.** 이름만 남고 내용이 없는 샘플이다 |
| **SMP-KT-03** (미구현) | [shoppingmall](../../common/sample/event/shoppingmall.ko.md): `CommerceApi`가 **HTTP edge**이고, `ContinueOrderWorkflowReq`는 **owner spot이 자기 재개를 예약하는 내부 메시지**다([§9.3](../../common/sample/event/shoppingmall.ko.md)) | `samples/kotlin/ShoppingMall/Server/CommerceApi/**` — HTTP endpoint가 **0건**이다(`OrderWorkflowRouter.kt`·`CommerceApiApplication.kt`·`Program.kt` 전부 route 정의 없음). 대신 **client가 내부 메시지를 직접 쏜다** — `Client/.../ShoppingMallClientScenario.kt:10,137`이 `ContinueOrderWorkflowReq`를 import해 보낸다. ⇒ **edge가 없어서 client가 서버 내부 배선을 대신한다.** 샘플이 사용자에게 보여 줄 공개 표면이 없다 |
| **SMP-KT-05** (결함) | [kotlin guide](../../kotlin/README.ko.md): Kotlin 표면은 `suspend`/`Flow`가 정본이다. [04](../04-async-execution-policy.ko.md): 실행 줄을 블로킹하지 않는다 | `useCoroutineHandlers` 호출이 **DeliveryDispatch 0건 · ShoppingMall 0건**이다(Bingo 3 · SupportChat 3 · TicTacToe 2 · GameQuest 2). ⇒ 6개 중 2개가 **Kotlin의 정본 표면을 아예 켜지 않는다.** 더 나쁜 건 ShoppingMall이 **`suspend` 안에서 스레드를 park한다**는 것이다 — `Server/Configuration/.../CommerceStore.kt:523`과 `Server/CommerceApi/.../StartOrderUseCase.kt:98`이 `LockSupport.parkNanos(...)`를 부른다. **coroutine 디스패처 스레드를 통째로 붙잡는다** |
| **SMP-KT-01** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md):135-143 — **절대 규칙: TicTacToe만 수동 연결을 쓸 수 있다.** *"위반이 하나라도 있으면 해당 샘플 변경은 완료된 것으로 판단하지 않는다"* | `samples/kotlin/**`(빌드 산출물 `bin/` 제외, TicTacToe 제외)에 `connectRouter`/`connectPeerPub`가 **9곳** — Bingo(`Server/Session/.../SessionServerApplication.kt:49`, `Server/Play/.../PlayServerApplication.kt:70`)와 DeliveryDispatch(`CourierSession` ×2, `Dispatch` ×2, `CourierGateway` ×2, `CourierSpotNode` ×1). **`.NET`·Node·C++는 0건이다.** 그리고 **Kotlin SupportChat·ShoppingMall은 깨끗하다** — 같은 언어·같은 framework에서 가능하다는 걸 스스로 증명한다. ⇒ "언어별 구현 차이"라는 변명이 성립하지 않는다. (체크리스트는 12곳이라 적었으나 실측은 9곳이다) |
| **SMP-KT-02** (결함) | [shoppingmall §9.2](../../common/sample/event/shoppingmall.ko.md): 이 샘플의 논지는 **saga 오케스트레이터가 사라지고 순차 코드로 접힌다**는 것이다. 그리고 continuation은 **유실되면 안 된다** | Kotlin이 **in-process 큐로 saga 오케스트레이터를 되살렸다.** 큐가 **비내구**라 프로세스가 죽으면 **예약된 continuation이 사라진다** — 무손실 요구 위반이고, 샘플이 없애 보이겠다던 바로 그 구조를 도로 만들었다 |
| **SMP-KT-06** (결함) | [샘플 규약 §Client self-check](../../common/sample/README.ko.md):365-371 — push 대기는 **connector의 public wait API**를 쓴다. sample-local polling 함수를 **기준 경로로 삼지 않는다** | `samples/kotlin/DeliveryDispatch/Client/.../Program.kt:365` — `while (System.nanoTime() < deadline) { … }` HTTP 폴링 루프가 기본 대기 경로다 |

### E2E

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-KT-01** (결함) | [e2e §2](../../common/e2e/README.ko.md):47-50 — *"client 코드에서 channel/fanout/spot framework client, framework host 구성, test-only helper를 직접 사용하지 않는다"*. framework 호출은 **역할 server 안**에 둔다 | `e2e-kotlin/SpotService/Client/.../ClientApplication.kt:19,22` — client가 **`@EnableZLinkFramework`로 framework host를 띄운다.** 그리고 `Client/.../support/SpotHttpDriver.kt:30`이 **`currentOutbound.requestToSpot(target, …)`을 client 코드에서 직접 호출한다.** ⇒ 이 config의 client는 **사용자가 아니라 두 번째 서버**다. 실사용 흐름을 흉내 내라는 §2의 전제가 무너진다. **같은 config의 Java 쪽은 깨끗하다** — Kotlin만 이렇다 |
| **E2E-KT-02** (결함) | [config-10](../../common/e2e/config-10-spot-actor-transfer.ko.md): ST 시나리오 **20개**. [e2e §2.2·§2.8](../../common/e2e/README.ko.md): config마다 `Shared/`와 `feature-map.ko.md`를 둔다 | Kotlin Config 10이 **20개 중 2개만** 구현하고 **나머지 18개를 Java 클라이언트에 위임**한다. `Shared/`도 `feature-map.ko.md`도 **없고**, Redis 컨테이너 접두사도 규약(`zlink-redis-kotlin-e2e…`)과 다르다. ⇒ **Kotlin이 Config 10을 검증한다고 말할 수 없다** |
| **E2E-KT-03** (결함) | [config-2](../../common/e2e/config-2-spot-service.ko.md)의 SM 시나리오. [config-4:103-107](../../common/e2e/config-4-registration-codec.ko.md): **`RC-A6`은 `P0`**다. [e2e §2.5](../../common/e2e/README.ko.md): 시나리오 ID 하나 = client scenario 파일 하나 | Config 2에서 시나리오 **6개가 누락**됐다. `RC-A6`(**P0**)은 **client scenario 없이 셸 `grep`으로** 판정하는데 **feature-map은 "구현 완료"로 적는다.** ⇒ feature-map이 근거가 아니라는 것을 다시 보여 준다 |
| **E2E-KT-04** (결함) | [e2e §2.5](../../common/e2e/README.ko.md):328-329 — *"여러 시나리오를 하나의 `AllScenario`, `ScenarioSet`, `DriverScenario` 파일로 묶어 driver에 위임하지 않는다"* | `e2e-kotlin/DiscoveryRegistryHa/`에 **`Client/Scenarios/`가 없다.** 32줄짜리 `when` 분기가 **477줄 god-context**로 흘려보낸다 — 규약이 이름을 짚어 금지한 `AllScenario` 형태 그대로다 |
