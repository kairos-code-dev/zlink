<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Location messaging](config-1-location-messaging.ko.md) | [다음: Pub/Sub](config-3-pubsub.ko.md)
<!-- framework-adapter-nav:end -->

# Config 2 — Spot 기반 서비스 배포

> 현재 .NET framework E2E는 `framework/languages/dotnet/e2e/SpotService` 아래에서 이
> 시나리오를 구현한다. 이 문서는 언어별 구현이 유지해야 할 검증 의도, 서버 역할, marker
> 기준을 설명한다.

상태를 소유하는 서비스 구성을 시작한다. entry Spot이 user Spot으로 라우팅하고, 해당 Spot에 actor와
bound session이 연결되는 배포다. 이 구성을 한 번 시작한 뒤 spot messaging과 session push가 실제
사용자 흐름에서 동작하는지 확인한다. 구조는 정본 샘플 Bingo·TicTacToe를 따르되, 검증에 필요한 흐름으로만
좁혔다.

## 1. 목적과 범위

- 다룬다: ChannelName↔Spot, Spot↔Spot messaging(send/request/publish), Logical Multicast,
  entry/user Spot 생성과 상태, actor join과 remote actor, bound session push, stream session,
  MeshNode direct·Spot direct 혼재 트래픽의 에러 계약과 소유권 독립, MeshNode scale-out 때 기존 owner
  유지와 신규 배치.
- 여기서 다루지 않는 것(다른 config로): codec 변주, channel provider의 scale·failover(Config 1), resilience(Config 5), location store 장애/복구(Config 6), 기존 actor owner의 명시적 transfer(Config 10), drain handoff(Config 11).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. 각 노드는 `AddLocationStore(new ZLinkRedisLocationStore(...))`로 등록하고, peer/spot location row는 framework lifecycle이 자동 갱신한다. |
| play(actor) 노드 | 2 (`play-a`, `play-b`) | Entry Spot + user Spot + actor mailbox를 호스팅한다. MeshNode의 단일 ROUTER endpoint에 Entry Spot·user Spot·actor handler·Spot timer를 등록하고 peer descriptor와 Spot location row를 자동 게시한다. `/evidence`·`/health`를 제공한다. |
| session(gateway) 노드 | 2 (`session-a`, `session-b`) | stream session을 호스팅한다. `AddStreamNode(...)`의 Actor dispatch 대상 MeshName을 같은 프로세스의 MeshNode로 지정한다. 각자 stream endpoint를 사용하며 업무 로직은 play 노드가 처리한다. |
| consumer | 시나리오별 | ChannelName client + stream client. entry spot은 location store 기반으로 resolve(자기도 같은 store를 등록). |

이 배포의 핵심은 **연결과 로직을 나눠 둔 것**이다. client는 session(gateway) 노드에 stream으로
연결되지만 actor는 play 노드에 존재한다(session 노드는 STREAM/auth/relay 전용이라 actor를 직접
호스팅하지 않는다). 각 session 노드는 **preferred play 노드**를 하나씩 갖는다(`session-a`→`play-a`,
`session-b`→`play-b`, Bingo의 `PreferredPlayNodeRid` 방식). session handler는 stream header
metadata의 `actor-id`(`header.Metadata.Get("actor-id")`)를 읽어
`Context.Actors.Find(actorId)?.RelayAsync(...)`로 해당 bound actor에 relay한다(bind가 하나면
기본 relay, 여럿이면 `actor-id` 필수). actor가 내보내는 push도 session을 거쳐 client로 relay된다.

owner routing 매핑은 고정이다. 앱이 entity key를 결정적 규칙으로 `RoutingId`에 매핑한다(예:
`RoutingId.From(key)` 또는 고정 해시 → play 노드 owner). 그래서 같은 key는 언제나 같은
RoutingId·owner로 간다.

handler 동작(공유):

- entry spot은 `JoinReq`로 user spot/actor를 만들고, user spot은 `StateReq(op)`로 상태를 바꾼다.
- **session이 끊겨도 actor membership은 자동으로 안 바뀐다.** session handler가
  `OnDisconnectedAsync`에서 원하는 actor에 `NotifyDisconnectedAsync(...)`를 직접 호출해야
  actor `Disconnected`가 발생한다.
- user spot은 명시적 `CloseAsync`로만 닫힌다(joined actor가 남아 있으면 거부). close 시
  `OnClosingAsync` callback이 실행된다.
- 미등록 spot route/actor packet은 dispatch error로 처리되고 message-flow error evidence
  (`surface`=`spot_route`/`spot_actor`, `reason`=`no_handler`, `action`=`reply_error`/`drop`)에 남는다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → spot 노드 → session 노드 순으로 시작하고 client
시나리오를 순차 실행한다. stream 시나리오는 consumer가 stream client로 접속한다. 실행이 끝나면
전용 prefix의 key를 정리하거나 disposable Redis instance를 버린다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — Spot 생성·상태·routing

#### SM-A1 entry spot 생성과 request

우선순위: `P0`

**검증 질문:** entry spot에 join을 보내면, user spot이 새로 만들어지고 그 id가 reply로 돌아오는가.

- 절차: consumer가 location store 기반 resolve로 entry spot을 찾아 `JoinReq`를 보낸다.
- 검증: entry Spot이 user Spot을 생성하고 reply에 Spot ID가 포함된다. Spot evidence에 생성 기록이
  남고, store의 `ResolveSpotAsync(...)`가 생성된 Spot의 owner RID와 generation을 반환한다.
- 세부 동작: entry spot dispatch + spot 생성 + spot location row 자동 등록.

#### SM-A2 user spot request와 state mutation

우선순위: `P0`

**검증 질문:** 한 spot에 상태 변경을 연달아 보내도, 순서대로 누적되고 동시 요청에도 상태가 깨지지 않는가.

- 절차: 생성된 user spot에 `StateReq(op)`를 연속으로 보낸다.
- 검증: 각 reply가 누적 상태를 정확히 반영한다(순서 보존). 동시 요청에도 상태가 깨지지 않는다.
- 세부 동작: spot 단위 직렬 처리 + 상태 일관성.

#### SM-A3 route resolver

우선순위: `P1`

**검증 질문:** 특정 spot id로 보낸 request가 정확히 그 spot이 있는 노드에서만 처리되는가.

- 절차: 특정 spot id를 가진 user spot으로 직접 라우팅되는 request를 보낸다.
- 검증: 해당 spot이 있는 노드에서만 처리(다른 노드 evidence엔 없음).
- 세부 동작: spot route resolve의 정확성.

#### SM-A4 owner routing — key → RoutingId 매핑

우선순위: `P0`

**검증 질문:** 같은 key는 언제나 같은 owner spot/노드로 가는가(앱이 정한 key→RoutingId 매핑이 고정인 한).

- 절차: 앱이 entity key(예: order id, player id)를 결정적 규칙으로 `RoutingId`에 매핑하고(§2의 고정 매핑 규칙), 그 RoutingId의 owner spot으로 request를 보낸다.
- 검증: 같은 key는 항상 같은 RoutingId → 같은 owner spot/노드로 간다. cross-node spot lookup은 RoutingId로 resolve된다(key→RoutingId 매핑·노드 owner 규칙은 앱이 정의). 매핑이 고정인 한 owner도 고정이다.
- 세부 동작: 결정적 key→RoutingId 매핑 기반 owner routing. (scale-out 뒤 기존 owner 유지와 신규 대상
  배치는 Track G의 SM-G2에서 다룬다.)

#### SM-A5 Stage wrapper

우선순위: `P2`

**검증 질문:** Spot에 Stage wrapper(playhouse Stage 류)를 추가해도 Spot messaging·timer·lifecycle이
같은 의미로 동작하는가.

- 절차: Stage wrapper(playhouse Stage 류)를 추가한 Spot에 request와 timer command를 보낸다.
- 검증: Stage wrapper를 통해도 spot 메시징·timer·lifecycle이 같은 의미로 동작한다.
- 세부 동작: SPOT 위 Stage wrapper 계층.

#### SM-A6 spot lifecycle (initialize·close)

우선순위: `P1`

**검증 질문:** spot을 만들 때 `OnInitializeAsync`, 닫을 때 `OnClosingAsync`가 정확히 한 번씩 실행되는가(actor가 남아 있으면 close는 거부되는가).

- 절차: user spot을 생성해 `OnInitializeAsync` 시점을 evidence에 남기고, joined actor가 없는 상태에서 명시적 `CloseAsync`로 닫는다.
- 검증: 생성 시 `OnInitializeAsync`, close 시 `OnClosingAsync`가 직렬화된 close 경로로 1회씩 발화해 evidence에 기록된다. joined actor가 있으면 `CloseAsync`가 거부된다.
- 세부 동작: spot 생성·종료 lifecycle 콜백.

#### SM-A7 spot 타입 불일치 (SpotTypeMismatch)

우선순위: `P1`

**검증 질문:** 이미 만든 spotRid를 다른 spot 타입으로 다시 `GetOrCreate`하면, `SpotTypeMismatch`로 명확히 거부되는가.

- 절차: `GetOrCreateAsync<TSpotA>(rid)`로 spot을 만든 뒤, 같은 `rid`를 `GetOrCreateAsync<TSpotB>(rid)`(다른 타입)로 다시 요청한다.
- 검증: 두 번째 요청은 `SpotTypeMismatch` public error로 실패한다(`ZLinkFrameworkException`). 처음 만든 spot과 그 상태는 영향받지 않는다.
- 세부 동작: 같은 rid 재사용 시 타입 일치 강제.

#### SM-A8 worker offload (`Context.RunCpuWorker`)

우선순위: `P2`

**검증 질문:** 무거운 CPU 작업을 `RunCpuWorker`로 spot의 직렬 스레드 밖에서 실행해도, 결과는 spot context로 안전하게 전달되어 반영되는가.

- 절차: spot handler가 무거운 CPU 작업을 `Context.RunCpuWorker(...)`로 offload하고 `.Yield(...)`로 기다린 뒤, 결과를 받아 spot 상태에 반영한다. 같은 시간대에 그 spot/노드로 다른 request도 보낸다.
- 검증: worker가 spot 직렬 스레드 밖(bounded worker pool)에서 실행된다. `.Yield(...)`가 turn을 반납하므로 그동안 같은 spot의 다른 처리가 진행되고, worker 결과는 spot 직렬 컨텍스트로 돌아와 상태에 안전히 반영된다(경합 없음).
- 세부 동작: **worker 축(어느 스레드에서 실행되는가)과 turn 축(같은 spot이 진행하는가)은 별개다** ([04 §1.2](../../spec/04-async-execution-policy.ko.md)). `.Async(...)`로 기다리면 같은 offload라도 turn은 유지된다 — 그 대비는 [config-8 TD-C4](config-8-execution-turn.ko.md)가 소유한다.
- 세부 동작: spot 직렬성 유지 + 무거운 작업 offload.

### Track B — actor join과 lifecycle

actor join은 actor가 어느 노드의 mailbox에서 실행되느냐에 따라 local과 remote로 나뉜다. 두
경우를 모두 본다.

#### SM-B1 local actor join

우선순위: `P0`

**검증 질문:** client가 연결된 노드에 actor를 join하면, actor가 그 노드의 local mailbox에 만들어지고 lifecycle callback도 그 노드에서만 실행되는가.

- 절차: consumer가 자신이 붙은 노드(`play-a`)의 entry spot에 actor join을 요청한다. join 대상이 같은 노드로 resolve된다.
- 검증: actor가 `play-a`의 local mailbox에 생성된다. 후속 actor request가 같은 노드의 actor로 dispatch된다.
- 검증(callback): actor lifecycle callback `Created` → `Joined`가 `play-a`에서 순서대로 발화해 그 노드 evidence에 기록되고, `play-b`에는 남지 않는다. (callback 계약 자체는 기존 in-process 테스트가 고정 — 여기선 발화 노드만 확인)
- 세부 동작: local actor join + local mailbox 실행 + lifecycle callback.

#### SM-B2 remote actor join

우선순위: `P0`

**검증 질문:** 다른 노드가 소유한 actor를 join해도, 노드 경계를 넘어 해당 mailbox에 actor가 만들어지고 callback도 그 노드에서 실행되는가.

- 절차: consumer가 entry spot에 actor join을 요청하되, 대상이 원격 노드(`play-b`)로 resolve되도록 한다(또는 `play-a` consumer가 `play-b` 소유 actor를 join).
- 검증: actor가 `play-b`의 remote mailbox에 생성된다. join이 노드 경계를 넘어 라우팅되고 후속 actor request가 cross-node로 그 actor에 dispatch된다.
- 검증(callback): `Created` → `Joined` callback이 원격 노드 `play-b`에서 발화해 그 노드 evidence에 기록된다. join을 트리거한 `play-a`에는 actor 생성 callback이 남지 않는다.
- 세부 동작: remote actor join + cross-node mailbox 실행 + lifecycle callback.

#### SM-B3 요청 message 객체 충실도

우선순위: `P0`

**검증 질문:** 여러 필드·중첩 객체·배열이 든 메시지를 보냈을 때, 받은 쪽에서 한 글자도 빠지거나 변형되지 않고 그대로 도착하는가.

- 절차: actor join과 후속 actor request를 **여러 필드·중첩 객체·컬렉션**을 가진 message 객체로 보낸다(예: `JoinReq{ actorId, displayName, level, attributes:{...}, tags:[...] }`).
- 검증: actor/handler가 받은 객체의 모든 필드가 보낸 값과 정확히 일치한다 — 필드 누락·타입 손상·null 치환·컬렉션 순서 변형이 없다. reply도 핵심 필드를 그대로 반영하고, evidence에 수신 필드가 기록된다.
- 세부 동작: 요청 payload 객체 round-trip 충실도.

#### SM-B4 remote actor request

우선순위: `P1`

**검증 질문:** 다른 노드에 있는 actor로 request를 보내도, 노드 경계를 넘어 처리되고 reply가 돌아오는가.

- 절차: `play-a`의 consumer가 `play-b`에 있는 actor로 request를 보낸다.
- 검증: 노드 경계를 넘어 대상 actor에서 처리되고 reply가 돌아온다.
- 세부 동작: cross-node actor routing.

#### SM-B5 actor 미등록 request

우선순위: `P0`

**검증 질문:** handler 없는 actor packet을 보내면 error로 명확히 실패하고, 그 이유가 observer에 남는가.

- 절차: handler 없는 actor packet 이름으로 request를 보낸다.
- 검증: error reply로 끝나고 message-flow error evidence(`surface`=`spot_actor`, `reason`=`no_handler`, `action`=`reply_error`)가 남는다.
- 세부 동작: actor negative path + 관측(enum 필드).

#### SM-B6 actor leave vs disconnect callback

우선순위: `P0`

**검증 질문:** 명시적 leave는 `OnLeaveActorAsync`, 비정상 disconnect 통지는 `OnDisconnectActorAsync` — 두 경로가 각각 맞는 콜백을 actor당 한 번씩만 부르는가.

- 절차: join한 actor에 대해 (a) 앱이 명시적으로 `leaveActor(...)`로 떠나보내거나, (b) stream 연결을 비정상 종료한 뒤 session handler가 `NotifyDisconnectedAsync(...)`를 호출한다. (bound session `DisconnectAsync`는 actor membership을 자동으로 바꾸지 않는다.)
- 검증: (a)는 actor 소유 노드의 spot에서 `OnLeaveActorAsync`가, (b)는 `OnDisconnectActorAsync`가 1회 발화해 evidence에 남는다. leave/통지하지 않은 actor에는 발화하지 않는다. 발화는 actor당 1회로 중복·누락이 없다.
- 세부 동작: 명시적 leave(`leaveActor`→`OnLeaveActorAsync`) vs 비정상 disconnect(`NotifyDisconnectedAsync`→`OnDisconnectActorAsync`).

#### SM-B7 actor handler 실행 순서

우선순위: `P1`

**검증 질문:** lifecycle 콜백과 packet handler가 정해진 순서(`Created`→`Joined`→packet)대로 실행되고, 같은 actor의 packet은 순서가 보존되는가.

- 절차: 한 actor에 lifecycle(`Created`·`Joined`)과 packet handler를 함께 등록하고, join 직후 여러 packet을 연속으로 보낸다.
- 검증: lifecycle callback과 packet handler가 문서가 정한 순서(예: `Created` → `Joined` → packet dispatch)대로 실행되고, 같은 actor의 packet은 직렬로 순서 보존되어 처리된다. evidence에 실행 순서가 남는다.
- 세부 동작: actor handler 실행 순서 보장.

#### SM-B8 actor 명시 파괴 (`DestroyActorAsync`)

우선순위: `P1`

**검증 질문:** join한 actor를 `DestroyActorAsync`로 명시 파괴하면 mailbox에서 정리되고, 이후 그 actor로의 request는 정해진 error로 끝나는가.

- 절차: actor를 join한 뒤(예: entry spot 복귀 흐름에서) 그 actor를 `DestroyActorAsync(actorId)`로 파괴한다.
- 검증: actor가 mailbox에서 제거되고, 파괴 후 그 actor로의 request는 정해진 public error로 끝난다. 파괴 lifecycle callback이 정해진 순서로 1회 발화해 evidence에 남고, 다른 actor는 영향받지 않는다.
- 세부 동작: actor 명시 파괴 + 정리.

#### SM-B9 entry spot join admission (허용·거부)

우선순위: `P1`

**검증 질문:** entry spot의 join admission 훅(`onActorJoin`류)이 join을 심사해, 거부하면 actor가 생기지 않고 caller가 분류된 실패를 받는가.

- 절차: entry spot에 admission 훅을 두고, 허용 조건과 거부 조건(예: 정원 초과, 잘못된 자격)의 join을 각각 보낸다. 거부는 local join과 remote join(원격 노드 대상) 양쪽에서 확인한다.
- 검증: 허용 join은 SM-B1/B2와 동일하게 완주한다. 거부 join은 actor가 생성되지 않고(evidence에 생성 callback 없음), caller가 timeout이 아닌 분류된 거부 응답을 받는다. 거부가 노드 경계를 넘어도 같은 의미다.
- 세부 동작: join admission 심사 + 거부의 fail-fast 전파. (언어 간 구현 격차가 실제로 있었던 표면 — parity 검증 대상.)

### Track C — messaging 방향

여기서는 메시지가 흐르는 방향(channel→spot, spot→channel, spot→spot)별로, 한 시나리오 안에서
send·request·publish verb와 timeout·미등록 negative를 모두 본다(같은 점검 매트릭스를 방향만 바꿔
적용).

#### SM-C1 channel → spot messaging

우선순위: `P0`

**검증 질문:** 외부 channel에서 spot으로 들어오는 방향에서 request·send·publish·timeout·미등록 처리가 모두 올바르게 동작하는가.

- request: ChannelName client가 spot으로 request → 정확한 reply, spot evidence에 기록.
- send: one-way send → reply 없이 spot evidence에 command 기록.
- publish: channel이 publish → 구독한 spot이 수신(미구독 spot은 미수신).
- timeout: 느린 spot handler에 짧은 timeout → client timeout 예외, 이후 같은 연결의 정상 messaging 비오염.
- 미등록: handler 없는 spot packet → request는 error reply, send는 drop. message-flow error evidence(`surface`=`spot_route`, `reason`=`no_handler`, `action`=`reply_error`/`drop`)가 남는다.
- 세부 동작: 외부 channel에서 spot으로 들어오는 방향 전체 verb + negative.

#### SM-C2 spot → channel messaging

우선순위: `P0`

**검증 질문:** Spot이 ChannelName으로 내보내는 send/request와 Logical Multicast가 제대로 동작하는가.

- request: spot handler가 처리 중 외부 channel로 request → reply를 받아 처리에 반영.
- send: spot → channel one-way → 대상 ChannelName handler evidence에 기록.
- Logical Multicast: Spot이 target ChannelName과 topic으로 publish하면 target channel의 remote node마다 한 번 도달하고, 각 node의 구독 Spot만 수신한다.
- timeout: 느린 channel handler에 짧은 timeout → spot 쪽 timeout 처리, spot 상태 비오염.
- 미등록: 대상 channel에 handler 없음 → request는 error reply, send는 drop + observer marker.
- 세부 동작: Spot에서 ChannelName으로 나가는 send/request + Logical Multicast + negative.

#### SM-C3 spot → spot messaging

우선순위: `P1`

**검증 질문:** spot 사이에서 직접 주고받는 request·send·publish가 양쪽 evidence와 일치하게 동작하는가.

- request: 한 user spot이 다른 user spot으로 request → reply, 양쪽 evidence 일치.
- send: spot → spot one-way → 대상 spot evidence 기록.
- publish: spot 이벤트를 다른 spot이 구독 수신.
- timeout: 느린 대상 spot에 timeout → 소스 spot 정상 유지.
- 미등록: 대상 spot에 handler 없음 → error/drop + observer marker.
- 세부 동작: spot 간 직접 messaging 전체 verb + negative.

#### SM-C4 local Spot이 없는 MeshNode의 Logical Multicast

우선순위: `P1`

**검증 질문:** local Spot을 하나도 호스팅하지 않는 MeshNode도 ChannelName을 대상으로 Logical Multicast를 제출할 수 있고, 그 channel의 구독 Spot이 받는가.

- 절차: local Spot이 없는 MeshNode가 target ChannelName과 topic으로 Logical Multicast를 제출한다.
  전송에는 같은 MeshNode ROUTER 연결을 사용한다.
- 검증: target channel에 참여한 원격 node마다 routed message가 한 번 도달하고, 각 수신 node에서는 topic이 일치하는 local Spot만 이벤트를 받는다. 미구독 Spot은 받지 않는다.
- 세부 동작: Spot을 호스팅하지 않는 origin MeshNode의 Logical Multicast. SM-C2와 달리 publisher가 Spot context가 아닌 node client다.

#### SM-C5 Logical Multicast의 node 간 도달 (제출 성공 ≠ 수신)

우선순위: `P0`

**검증 질문:** 한 node의 Spot이 제출한 Logical Multicast를 **다른 node**의 구독 Spot이 실제로 받는가. origin의 제출 성공만으로 통과시키지 않는다.

- 절차: `play-a`의 Spot이 target ChannelName과 topic으로 Logical Multicast를 제출하고, 구독자는 `play-b`의 Spot으로 둔다. 두 MeshNode는 같은 RouteMesh peer다.
- 검증: 성공 기준은 **수신 측 evidence**다 — `play-b` 구독 spot이 이벤트를 받았다는 기록. 발행 측의 publish 성공 로그는 보조 증거일 뿐 단독으로는 통과가 아니다. 연결 미성립이면 발행이 성공으로 보여도 시나리오는 실패해야 한다.
- 세부 동작: origin node가 target channel의 remote node마다 routed message를 한 번 제출하고, 수신 node가 topic과 일치하는 local Spot에 message ref를 공유한다.

#### SM-C6 Logical Multicast ROUTER backpressure

우선순위: `P0`

- 절차: 수락 가능한 remote peer와 ROUTER 송신 HWM에 도달한 remote peer를 함께 둔 상태에서 blocking
  publish와 non-blocking submit을 각각 실행한다.
- 검증: blocking publish는 막힌 remote target에 대해 MeshNode send timeout 범위에서 기다리고,
  non-blocking submit은 즉시 backpressure 결과를 반환한다. 앞에서 수락된 peer의 전달은 뒤 target의
  실패 때문에 취소되지 않으며, publish detail의 remote snapshot·admitted·dropped 수가 실제 수신
  evidence와 일치해야 한다.
- 별도 회귀: local matching Spot queue 하나를 가득 채우고 다른 local target은 수락 가능하게 두어,
  가득 찬 target만 drop 수에 기록되고 다른 target은 수신하는지 확인한다.

### Track D — session bind·relay·push와 stream

stream session은 actor에 bind되어 양방향으로 메시지를 relay한다. 여기서는 bind 위치(local/remote),
actor가 존재하는 Spot 종류(entry/user), 한 session에 bind된 actor 수(단일/다중)를 나눠서 본다.

#### SM-D1 actor session bind & relay — local

우선순위: `P0`

**검증 질문:** gateway에 연결된 client가 같은 play 노드의 actor에 bind했을 때, 양방향 relay(client→actor, actor→client push)가 동작하는가.

- 절차: consumer가 `session-a` gateway에 stream으로 접속·auth하고, `session-a`의 **preferred play 노드(`play-a`)**의 actor에 bind한다. client → actor request(`actor-id` metadata 포함)를 보내고, actor → client push를 트리거한다.
- 검증: bind 성립 후 client packet이 `header.Metadata.Get("actor-id")`로 bound actor에 relay되어 처리되고, actor push가 같은 session으로 relay되어 client가 받는다(양방향). bind 안 한 client는 받지 않는다.
- 세부 동작: gateway → preferred play 노드 actor relay.

#### SM-D2 actor session bind & relay — remote

우선순위: `P0`

**검증 질문:** bind 대상 actor가 gateway와 다른 원격 play 노드에 있어도, 노드 경계를 넘는 양방향 relay가 동작하는가.

- 절차: consumer가 `session-a` gateway에 연결하고, bind 대상 actor는 `session-a`의 **preferred가 아닌
  원격 play 노드**(`play-b`)에 배치한다. 같은 양방향 relay를 수행한다.
- 검증: client packet이 gateway → 원격 play 노드로 노드 경계를 넘어 actor에 relay되고, 원격 actor의 push가 gateway를 거쳐 같은 session으로 돌아온다.
- 세부 동작: gateway↔원격 play 노드 cross-node relay.

#### SM-D3 entry spot vs user spot actor bind

우선순위: `P1`

**검증 질문:** bind 대상 actor가 entry spot이나 user spot에 있을 때 bind·relay·push가 같은 의미로 동작하는가.

- 절차: bind 대상 actor가 (1) entry spot에 있는 경우와 (2) user spot에 있는 경우 각각 session을 bind한다.
- 검증: 두 경우 모두 bind·양방향 relay가 정상이며, push·dispatch가 actor가 존재하는 Spot 종류와 무관하게 같은 의미로 동작한다.
- 세부 동작: spot 종류별 bind 동등성.

#### SM-D4 한 stream에 여러 actor bind

우선순위: `P0`

**검증 질문:** 한 stream에 actor를 여럿 bind했을 때, `actor-id`로 지정한 actor에게만 정확히 가고(오배달 없이), id 없이 보내면 실패하는가.

- 절차: 한 stream session에 여러 actor(예: `actor-x`, `actor-y`)를 bind한다. stream header metadata `actor-id`(`header.Metadata.Get("actor-id")`)에 대상 actor id를 실어 보내고, session handler가 `Context.Actors.Find(actorId)?.RelayAsync(...)`로 해당 actor에 relay한다. 각 actor가 push를 낸다.
- 검증: 각 packet이 `actor-id`로 지정한 actor로만 relay되고(교차 오배달 없음), 각 actor push가 같은 session으로 relay되어 client가 actor별로 구분해 받는다. **relay 대상 선택은 application 책임이다** — framework에는 `actor-id` metadata 기반 자동 라우팅이나 단일 bound 기본 relay가 없으므로, session이 대상 actor를 찾지 못하면 그 실패 처리도 application이 정의한다([31 §6](../../spec/server/31-session-actor-dispatch.ko.md#6-binding과-bound-session)).
- 세부 동작: 다중 actor bind + `actor-id` metadata 선택 relay (단일 bound만 기본 relay).

#### SM-D5 session disconnect → 명시적 actor 통지

우선순위: `P0`

**검증 질문:** 연결이 끊겨도 actor membership이 자동으로 해제되지 않고, handler가 `NotifyDisconnectedAsync`를 호출한 actor에 한해서만 `OnDisconnectActorAsync`가 실행되는가.

- 절차: SM-D1~D4로 bind한 상태에서 stream 연결을 비정상 종료(disconnect)한다. session handler의 `OnDisconnectedAsync`에서 선택한 bound actor에 `NotifyDisconnectedAsync(...)`를 호출한다. 단일·다중 bind, local·remote를 모두 시도한다.
- 검증: session disconnect 자체는 actor membership을 자동으로 바꾸지 않는다. handler가 `NotifyDisconnectedAsync`를 호출한 actor에 한해, 그 actor가 존재하는 노드의 Spot에서 `OnDisconnectActorAsync` callback이 1회 발화해 evidence에 남는다. 통지하지 않은 actor에는 발화하지 않는다.
- 세부 동작: session `OnDisconnectedAsync` → 선택 actor `NotifyDisconnectedAsync` → spot `OnDisconnectActorAsync` (자동 fanout 아님).

#### SM-D6 bound session push 타깃팅

우선순위: `P0`

**검증 질문:** actor 상태가 바뀌면 그 변화가 bind한 session에게만 push되고, bind 안 한 consumer는 못 받는가.

- 절차: consumer가 session에 bind한 뒤, 다른 경로로 그 actor 상태를 바꾼다.
- 검증: 상태 변경이 bound session으로만 push되어 해당 consumer가 수신한다. bind 안 한 consumer는 받지 않는다.
- 세부 동작: bound session push 타깃팅.

#### SM-D7 stream session auth와 packet dispatch

우선순위: `P0`

**검증 질문:** stream 연결과 auth가 성공해야 messaging이 동작하는가(auth 실패는 명확한 오류인가).

- 절차: consumer가 stream client로 접속·auth하고 packet을 주고받는다.
- 검증: auth 성공 후 request/notify가 정상 dispatch된다. auth 실패는 공개 오류.
- 세부 동작: stream session 수명 + dispatch.

#### SM-D8 stream reconnect

우선순위: `P1`

**검증 질문:** stream 연결을 끊었다가 다시 연결하면, 연결이 끊어진 시점의 pending은 자동 재전송 없이
실패하고 재접속 뒤 auth·rebind를 수행했을 때 messaging이 정상적으로 재개되는가.

- 절차: stream 연결을 끊었다가 재접속한다. 끊김 시점의 pending request 결과와 재접속 후 동작을 본다.
- 검증: 끊김 시 pending request는 `Disconnected`로 실패하고 자동 재전송되지 않는다. 재접속은 새 session이므로 app이 다시 auth·rebind하고, 필요한 상태는 replay/snapshot packet으로 복구한다. rebind 후 messaging이 정상 재개된다.
- 세부 동작: 재접속 = 재auth·rebind + app replay (자동 재전송 없음).

#### SM-D9 inbound observer

우선순위: `P1`

**검증 질문:** stream inbound observer가 들어오는 메시지의 종류·이름·seq를 관측 evidence로 남기는가.

- 절차: stream inbound observer를 등록하고 메시지를 받는다.
- 검증: observer가 inbound 종류·이름·seq를 관측 evidence로 남긴다.
- 세부 동작: inbound 관측.

#### SM-D10 stream backpressure

우선순위: `P1`

**검증 질문:** 처리 속도보다 빠르게 메시지를 입력해도 정해진 흐름 제어대로 동작하고, session 상태가
손상되거나 다른 session에 영향을 주지 않는가.

- 절차: stream client가 처리 속도보다 빠르게 메시지를 주고받아 backpressure를 유발한다.
- 검증: 정해진 흐름 제어 규칙(버퍼/대기/drop)대로 동작하고, session 상태가 유지되며 다른 session에 영향이 없다.
- 세부 동작: stream 흐름 제어.

#### SM-D11 stream request와 channel request 혼합

우선순위: `P1`

**검증 질문:** 같은 consumer가 stream과 channel을 함께 사용해도, 두 경로가 서로 간섭하지 않고 각 reply를 원래 경로로 반환하는가.

- 절차: 같은 consumer가 stream session request와 일반 channel request를 섞어 보낸다.
- 검증: 두 경로가 서로 간섭 없이 각자 정상 동작하고, reply가 올바른 경로로 돌아온다.
- 세부 동작: stream·channel 혼합 경로 격리.

#### SM-D12 session 재접속 이전성 (다른 연결 서버로)

우선순위: `P0`

**검증 질문:** client가 다른 gateway로 갈아타 재접속해도, play 노드의 actor 상태는 그대로 유지되고 rebind 후 messaging이 이어지는가.

- 절차: client가 연결 서버 `session-a`에 연결해 play 노드 actor와 messaging한 뒤 연결을 끊고
  **다른 연결 서버 `session-b`**로 재접속한다.
- 검증: 로직(play 노드)의 actor 상태는 연결 서버와 무관하게 유지된다. client는 `session-b`에서 다시 auth하고 같은 actor id로 rebind한 뒤 snapshot + 이후 event로 상태를 복구한다(자동 이전·재전송 아님). rebind 후 messaging이 정상 재개된다.
- 세부 동작: 연결/로직 분리 — 다른 gateway로 재auth·rebind 이전성.

#### SM-D13 stream heartbeat

우선순위: `P1`

**검증 질문:** heartbeat가 정상이면 session 연결이 유지되고, heartbeat가 중단되면 `Disconnected`로
감지되는가(actor 통지는 수동인가).

- 절차: stream 연결을 유지하며 heartbeat 주기를 지나친다. 한쪽이 heartbeat를 멈춘다.
- 검증: 정상 heartbeat 동안 session 연결이 유지된다. heartbeat 중단은 connector에서
  `Disconnected` 상태나 오류로 감지되며 server stream session의 `OnDisconnectedAsync`가 실행된다.
  bound actor의 `Disconnected`는 session handler가 `NotifyDisconnectedAsync`를 호출한 경우에만 발생한다.
- 세부 동작: heartbeat 기반 liveness (session disconnect와 actor 통지 분리).

#### SM-D14 stream TLS

우선순위: `P2`

**검증 질문:** TLS 위에서도 bind·relay·push가 평문과 똑같이 동작하고, 잘못된 인증서는 거부되는가.

- 절차: stream 연결을 TLS로 수립해 auth·messaging을 수행한다.
- 검증: TLS 위에서 bind·relay·push가 평문과 같은 의미로 동작한다. 잘못된 인증서는 연결 거부.
- 세부 동작: TLS stream 전송.

#### SM-D15 cross-role 다단 push 사슬

우선순위: `P0`

**검증 질문:** 다른 role이 시작한 상태 변화가 actor send를 거쳐 bound session push로 client stream까지 끝까지 도달하는가.

- 절차: client와 무관한 별도 role(예: tracking)이 channel request를 받아 `SendToActor`로 대상 actor에 상태 변경 메시지를 보내고, 그 actor의 핸들러가 bound session으로 notify를 push한다. client는 stream에서 그 notify를 기다린다.
- 검증: client가 notify를 실제 수신한다 — 이것만이 성공 기준. 중간 각 hop(channel 수신, actor send 도달, push 발신)의 flow trace가 남아 단절 시 지점을 특정할 수 있다. actor send의 대상 핸들러가 미등록이면 silent drop이 아니라 관측 가능한 실패가 남는다.
- 세부 동작: role 경계 2회 이상을 넘는 push 사슬의 end-to-end 도달 + hop별 관측성. (발신 role들의 로그는 전부 정상인데 client만 timeout인 결함 — 중간 hop의 핸들러 미등록 — 이 실제로 있었다.)

### Track E — negatives와 timer

#### SM-E1 spot route 미등록 request

우선순위: `P0`

**검증 질문:** handler 없는 spot route packet은 error로 실패하고, 그 이유가 observer에 남는가.

- 절차: handler 없는 spot route packet으로 request를 보낸다.
- 검증: error reply + message-flow error evidence(`surface`=`spot_route`, `reason`=`no_handler`, `action`=`reply_error`).
- 세부 동작: spot route negative path(enum 필드).

#### SM-E2 spot timer

우선순위: `P1`

**검증 질문:** spot이 등록한 timer가 정해진 주기대로 발화하고, 그 효과(상태 변화·push)가 관측되는가.

- 절차: spot이 timer를 건다.
- 검증: timer가 한 번/주기대로 발화하고 그 효과(상태 변화·push)가 관측된다.
- 세부 동작: spot timer 발화.

#### SM-E3 idle timer 기반 명시적 close

우선순위: `P1`

**검증 질문:** 일정 시간 활동이 없으면 timer handler가 spot을 `CloseAsync`로 닫고, 활동 중이거나 actor가 남아 있으면 안 닫는가.

- 절차: user spot이 `AddTimer<THandler>`로 주기 timer를 돌리며 마지막 활동 시각을 기록한다. idle 임계를 넘으면 timer handler가 (joined actor가 모두 떠난 뒤) `CloseAsync`를 호출한다. 활동이 오면 마지막 활동 시각을 갱신한다.
- 검증: idle 초과 시 spot이 `CloseAsync`로 닫히고 `OnClosingAsync` 콜백이 evidence에 기록된다. joined actor가 남아 있으면 close가 거부된다(먼저 actor leave 필요). 활동 중엔 닫히지 않는다.
- 세부 동작: 애플리케이션 idle timer + 명시적 `CloseAsync` + `OnClosingAsync` (자동 actor callback 아님).

#### SM-E4 spot timer overrun 정책

우선순위: `P1`

**검증 질문:** timer handler가 주기보다 느려 tick이 밀릴 때, 설정한 `OverrunPolicy`(SkipLateTicks / CatchUpBounded / DelayNextTick)대로 동작하는가.

- 절차: 주기보다 처리가 느린 timer handler를 각 `ZLinkTimerOverrunPolicy`로 설정해 실행한다.
- 검증: 정책별로 관측이 일치한다 — `SkipLateTicks`는 밀린 tick을 건너뛰고, `CatchUpBounded`는 정해진 한도까지만 따라잡으며, `DelayNextTick`은 다음 tick을 미룬다. evidence의 발화 패턴이 정책과 맞는다.
- 세부 동작: timer overrun 정책별 tick 처리.

### Track F — MeshNode ROUTER에서 Channel·Spot routed path 공존

ChannelName handler, RID direct handler와 Spot direct handler가 같은 MeshNode ROUTER를 사용할 때 namespace,
대상 선택과 lifecycle이 서로 섞이지 않는지 검증한다. 별도 중계 계층이나 Spot 전용 socket은 사용하지
않는다. 외부 코드는 resolver가 반환한 `SpotHandle`로 target Spot을 지정하며 owner RID, generation과
전송 MeshName은 framework가 관리한다.

#### SM-F1 route client → local target Spot

- 절차: consumer가 resolver로 같은 node가 소유한 `SpotHandle`을 얻어 request와 send를 제출한다.
- 검증: request는 target Spot의 reply 하나로 완료되고 send는 target Spot evidence에만 기록된다.

#### SM-F2 route client → remote target Spot

- 절차: consumer가 다른 MeshNode가 소유한 `SpotHandle`을 얻어 request와 send를 제출한다. caller는 owner
  RID와 endpoint를 조립하지 않는다.
- 검증: remote owner의 target Spot만 처리하며 reply의 source RID와 generation이 handle snapshot과 맞는다.

#### SM-F3 ChannelName·RID direct·Spot direct namespace 분리

- 절차: 같은 MeshNode에 같은 packet name을 사용하는 ChannelName request, RID direct request와 Spot direct
  request를 섞어 제출한다.
- 검증: 각 메시지는 channel handler, node handler, target Spot handler에 정확히 한 번 도달하고 각각의
  reply context가 유지된다.

#### SM-F4 target Spot 없음과 stale handle

- 절차: 존재하지 않는 Spot과 이전 generation의 handle로 request와 send를 제출한다.
- 검증: request는 정식 no-route 또는 stale 결과로 완료되고 send failure counter가 증가한다. 정상
  ChannelName과 RID direct messaging은 영향을 받지 않는다.

#### SM-F5 Spot lifecycle과 MeshNode lifecycle 분리

- 절차: ChannelName request와 Spot direct request를 처리한 뒤 target user Spot을 닫고 ChannelName request를
  다시 제출한다.
- 검증: 닫힌 Spot 경로만 실패하고 MeshNode peer와 ChannelName handler는 ready 상태를 유지한다.

#### SM-F6 같은 MeshName의 다중 MeshNode에서 원격 Spot·Actor 도달

- 절차: 같은 MeshName에 속한 source와 target MeshNode를 서로 다른 프로세스로 시작한다. Source Spot은
  public resolver가 반환한 `SpotHandle`로 target Spot에 request와 send를 제출하고, source Entry Spot은
  public actor join 표면으로 target user Spot에 Actor를 join한다. 별도 Spot 전용 ROUTER나 PUB/SUB socket은
  구성하지 않는다.
- 검증: 시작 순서와 무관하게 두 MeshNode가 ready가 된 뒤 request reply, send evidence와 Actor join
  evidence가 target Spot에만 기록된다. 호출자는 owner RID, endpoint, generation 또는 내부 route frame을
  조립하지 않는다.

### Track G — MeshNode 증설, 장애와 복구

여기서는 MeshNode가 추가되거나 spot/actor/session 노드가 비정상 종료되고, 동시 트래픽이 경합할 때
stateful 경로가 어떻게 동작하는지 본다. Config 5(resilience)는 channel provider를 다루므로,
spot 배포(play/session 노드)를 쓰는 시나리오는 Config 2에 둔다. 프로세스를
비정상 종료시키는 시나리오는 harness의 `kill`/`stop`/`restart` 연산을 전제한다(없으면 "미구현(하네스
대기)").

#### SM-G1 play 노드 crash와 복구

우선순위: `P0`

**검증 질문:** actor·bound session이 설정된 play 노드가 비정상 종료되면 그 노드의 상태만 영향을
받고, 같은 노드의 restart와 다른 노드에서의 application 복구가 각각 명시적인 재join·rebind로
정상화되는가.

- 절차: 먼저 `play-a`에 actor join + session bind 상태를 만들고 `play-b`에도 독립 기준 actor와
  session을 준비한 뒤 `play-a`를 `SIGKILL`한다. crash 직전 처리 중인 actor request와 crash 뒤 old
  `ActorRef`로 보낸 request의 실패를 각각 관찰한다. 다음 두 복구 경로를 **모두** 실행한다.
  1. old MeshNode descriptor·actor location이 owner lease 만료로 성공 조회에서 제외될 때까지 기다린다. 그 뒤
     `play-a`를 같은 RID·endpoint로 restart하고 새 owner generation의 MeshNode descriptor가 조회되는지 확인한다.
     `play-a`가 게시한 Entry Spot rid의 `SpotHandle` resolve가 성공할 때까지 역할 server의 bounded wait로
     기다린다. application은 준비된 handle로 `JoinReq`를 한 번 보내고,
     해당 handler가 로컬 actor를 다시 만든 뒤 join·rebind하고 snapshot/replay를 적용한다.
  2. 다시 같은 장애를 만든 뒤 old actor location이 성공 조회에서 제외될 때까지 기다리고,
     `play-b`가 게시한 Entry Spot rid의 `SpotHandle` resolve가 성공할 때까지 bounded wait로 기다린 뒤
     application이 준비된 handle로 `JoinReq`를 한 번 보낸다.
     해당 Entry Spot handler가 로컬 actor를 명시적으로 만든 뒤 join·rebind하고 snapshot/replay를
     적용한다.
- 검증: crash 직전 처리 중인 request는 연결 단절이 먼저 확정되면 retriable `RouteNotConnected`,
  handler 완료 여부를 caller가 확정할 수 없으면 설정한 request timeout 안의 timeout으로 끝난다.
  owner lease 만료로 old actor location이 성공 조회에서 제외된 뒤, actor를 다시 만들기 전에 old
  `ActorRef`로 보낸 request는 `ActorRouteNotFound`로 끝난다. actor를 같은 id와 새 generation으로 다시
  만든 뒤 old `ActorRef`로 보낸 request는 `ActorLocationStale`로 끝나고, 새 live `ActorRef`로 보낸
  follow-up request는 성공한다. 각 실패는 설정한 timeout 안에 종료되고 어느 단계에서도 old handler가
  실행되지 않아야 한다. stream 연결 종료는 framework request 오류와 섞지 않고
  connector/session의 `Disconnected`로 별도 확인한다. `play-b`의 기준 actor·session은 영향받지 않는다.
  두 복구 경로 모두 재join·rebind 뒤 messaging이 정상화되고 필요한 상태는 application snapshot/replay로
  복원된다. 자동 상태 이전이나 실패 request의 자동 재전송은 단언하지 않는다.
- 세부 동작: stateful 노드 crash 격리 + 같은 rid·endpoint restart 복구 + 선택한 노드의 Entry Spot
  handle을 사용한 application 복구.

#### SM-G2 MeshNode scale-out과 신규 배치

우선순위: `P1`

**검증 질문:** MeshNode를 추가해도 기존 owner는 유지되고, 새 actor는 선택한 node의 Entry Spot에
application `JoinReq`를 보내 만들 수 있으며 새 Spot은 선택한 node의 로컬 manager에서 만들 수 있는가.

- 절차: `play-a`의 로컬 SpotManager에서 기존 Spot을 만들고 actor도 만든 뒤 owner evidence를
  기록한다 → `play-b`를 추가한다 → peer/capability 정보에 `play-b`가 반영되고 `play-b`가 게시한 Entry
  Spot rid의 `SpotHandle` resolve가 성공할 때까지 각각 bounded wait로 기다린다 → 기존 대상에 다시
  request한다 → 준비된 handle로 application `JoinReq`를 한 번 보내고 해당 Entry Spot handler가 로컬
  actor를 만든다 → 새 Spot은 `play-b` 프로세스의
  로컬 SpotManager `GetOrCreate`/`getOrCreate`로 만든다.
- 검증: node 추가 전 만든 대상의 owner는 `play-a`로 유지되고 후속 request도 같은 owner가 처리한다.
  새 actor는 `play-b`가 게시한 Entry Spot에서 만들어지고, 새 Spot은 호출한 `play-b`의 로컬
  MeshNode에서 만들어진다. 두 node의 대상이 동시에 정상 처리된다. spot `GetOrCreate`를 클러스터
  배치 API로 표현하거나 node 추가만으로 기존 owner가 이동·자동 재분배되었다고 단언하지 않는다.
- 세부 동작: MeshNode 증설 발견 + 기존 owner 유지 + 선택한 Entry Spot의 application handler actor 생성 +
  선택한 node에서의 로컬 Spot 생성. 기존 actor owner를 바꾸는 동작은 Config 10의 actor transfer 또는
  Config 11의 drain handoff에서 검증한다.

#### SM-G3 동시 join/leave 경합

우선순위: `P1`

**검증 질문:** 같은 spot/actor에 다수 client가 동시에 join·leave·request를 보내도, membership과 상태가 일관되고 lifecycle callback이 중복·누락 없이 실행되는가.

- 절차: 같은 entry spot/actor 대상으로 다수 client가 동시에 join, leave, request를 섞어 보낸다.
- 검증: 경합 상황에서도 actor membership과 spot 상태가 일관되게 유지되고, `Created`/`Joined`/`OnLeaveActorAsync` 등 lifecycle callback이 actor당 정해진 횟수만 발화한다(중복·누락 없음). 같은 spot의 처리는 직렬 순서가 보존된다.
- 세부 동작: 동시 lifecycle 경합 일관성.

#### SM-G4 다수 bound session push 부하

우선순위: `P2`

**검증 질문:** 많은 session이 bind된 상태에서 동시에 push가 쏟아져도, 각 push가 제 session으로만 가고 누락·오배달 없이 격리되는가.

- 절차: 다수 actor에 다수 session을 bind한 뒤, 동시에 actor push를 대량으로 트리거한다.
- 검증: 각 push가 해당 bound session으로만 relay되고(교차 오배달 없음), 부하 중에도 session 간 격리가 유지된다. (전량 무손실은 공개 계약이 아니므로 단언하지 않는다 — 오배달 없음과 격리 유지에 초점.)
- 세부 동작: 대규모 bound session push 타깃팅(오배달 없음·격리).

## 5. 완료 기준

- Track A~G의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다(raw transport frame 조립·내부 helper 금지).
- 실패 시 store 연결 상태와 spot/session/consumer 로그·evidence로 원인 레이어를 분리한다.
