<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Registry messaging](config-1-registry-messaging.ko.md) | [다음: Pub/Sub](config-3-pubsub.ko.md)
<!-- framework-adapter-nav:end -->

# Config 2 — Spot 기반 서비스 배포

stateful 서비스 형상(entry spot이 user spot으로 라우팅하고, actor와 bound session이 붙는
배포)을 한 번 띄우고, 그 위에서 spot messaging과 session push를 실 사용자처럼 검증한다.
정본 샘플 Bingo·TicTacToe의 구조를 그대로 닮되, 검증 전용 흐름으로 좁힌다.

## 1. 목적과 범위

- 다룬다: channel↔spot, spot↔spot messaging(send/request/publish), entry/user spot 생성과 상태, actor join과 remote actor, bound session push, stream session.
- 범위 밖(다른 config로): codec 변주, registry scale/failover(Config 1), resilience(Config 5).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| registry | 1 | discovery server. |
| play(actor) 노드 | 2 (`play-a`, `play-b`) | entry spot + user spot + actor mailbox 호스트. SpotNode(`EnableRouter`)에 entry/user spot·actor handler·spot timer. registry에 광고. `/evidence`·`/health`. |
| session(gateway) 노드 | 2 (`session-a`, `session-b`) | stream session 호스트. 로컬 SpotNode + `AddStreamNode(...).AttachActorGateway("session-node")`로 actor gateway. 각자 stream endpoint. **연결 서버**(로직은 play 노드). |
| consumer | 시나리오별 | channel client + stream client. entry spot은 registry로 resolve. |

연결/로직 분리: client는 session(gateway) 노드에 stream으로 붙고 actor는 play 노드에 산다
(session 노드는 STREAM/auth/relay 전용 — actor를 직접 호스팅하지 않는다). 각 session 노드는
**preferred play 노드**를 갖는다(`session-a`→`play-a`, `session-b`→`play-b`, Bingo
`PreferredPlayNodeRid` 식). session handler는 stream header metadata의 `actor-id`
(`header.Metadata.Get("actor-id")`)로 `Context.Actors.Find(actorId)?.RelayAsync(...)`로 해당
bound actor에 relay한다(단일 bound면 기본 relay, 다중이면 `actor-id` 필수). actor push는 session을
거쳐 client로 relay된다.

owner routing 매핑(고정): 앱이 entity key를 결정적 규칙으로 `RoutingId`에 매핑한다(예:
`RoutingId.From(key)` 또는 고정 해시 → play 노드 owner). 같은 key는 항상 같은 RoutingId·owner.

handler 동작(공유): entry spot은 `JoinReq`로 user spot/actor를 만들고, user spot은
`StateReq(op)`로 상태를 바꾼다. **session disconnect는 자동으로 actor membership을 바꾸지
않는다** — session handler가 `OnDisconnectedAsync`에서 선택 actor에 `NotifyDisconnectedAsync(...)`를
호출해야 actor `Disconnected`가 발생한다. user spot은 명시적 `CloseAsync`로 닫히며(joined actor가
있으면 거부) close 시 `OnClosingAsync` 콜백이 돈다. 미등록 spot route/actor packet은 dispatch
error로 처리되고 observer evidence(`ZLinkMessageDispatchErrorEvent`: `Surface`=`SpotRoute`/`SpotActor`,
`Reason`=`HandlerMissing`, `Action`=`ReplyError`/`Drop`)에 남는다.

## 3. 실행 모델

`run_e2e.sh`가 registry → spot 노드 → session 노드 순으로 띄우고 client 시나리오를 순차
실행한다. stream 시나리오는 consumer가 stream client로 접속한다.

## 4. 시나리오

### Track A — Spot 생성·상태·routing

#### SM-A1 entry spot 생성과 request

우선순위: `P0`

- 절차: consumer가 registry로 entry spot을 resolve하고 `JoinReq`를 보낸다.
- 검증: entry spot이 user spot을 생성하고 reply에 spot id가 담긴다. spot evidence에 생성 기록.
- 세부 동작: entry spot dispatch + spot 생성.

#### SM-A2 user spot request와 state mutation

우선순위: `P0`

- 절차: 생성된 user spot에 `StateReq(op)`를 연속으로 보낸다.
- 검증: 각 reply가 누적 상태를 정확히 반영한다(순서 보존). 동시 요청에도 상태가 깨지지 않는다.
- 세부 동작: spot 단위 직렬 처리 + 상태 일관성.

#### SM-A3 route resolver

우선순위: `P1`

- 절차: 특정 spot id를 가진 user spot으로 직접 라우팅되는 request를 보낸다.
- 검증: 해당 spot이 있는 노드에서만 처리(다른 노드 evidence엔 없음).
- 세부 동작: spot route resolve의 정확성.

#### SM-A4 owner routing — key → RoutingId 매핑

우선순위: `P0`

- 절차: 앱이 entity key(예: order id, player id)를 결정적 규칙으로 `RoutingId`에 매핑하고(§2의 고정 매핑 규칙), 그 RoutingId의 owner spot으로 request를 보낸다.
- 검증: 같은 key는 항상 같은 RoutingId → 같은 owner spot/노드로 간다. cross-node spot lookup은 RoutingId로 resolve된다(key→RoutingId 매핑·노드 owner 규칙은 앱이 정의). 매핑이 고정인 한 owner도 고정이다.
- 세부 동작: 결정적 key→RoutingId 매핑 기반 owner routing. (scale-out 중 owner 이동은 Config 5에서 다룬다.)

#### SM-A5 Stage wrapper

우선순위: `P2`

- 절차: SPOT 위에 Stage wrapper(playhouse Stage 류)를 얹은 spot에 request/timer를 보낸다.
- 검증: Stage wrapper를 통해도 spot 메시징·timer·lifecycle이 같은 의미로 동작한다.
- 세부 동작: SPOT 위 Stage wrapper 계층.

#### SM-A6 spot lifecycle (initialize·close)

우선순위: `P1`

- 절차: user spot을 생성해 `OnInitializeAsync` 시점을 evidence에 남기고, joined actor가 없는 상태에서 명시적 `CloseAsync`로 닫는다.
- 검증: 생성 시 `OnInitializeAsync`, close 시 `OnClosingAsync`가 직렬화된 close 경로로 1회씩 발화해 evidence에 기록된다. joined actor가 있으면 `CloseAsync`가 거부된다.
- 세부 동작: spot 생성·종료 lifecycle 콜백.

actor join은 actor가 어느 노드의 mailbox에서 실행되느냐로 local과 remote가 나뉜다. 두
경우를 모두 검증한다.

#### SM-B1 local actor join

우선순위: `P0`

- 절차: consumer가 자신이 붙은 노드(`play-a`)의 entry spot에 actor join을 요청한다. join 대상이 같은 노드로 resolve된다.
- 검증: actor가 `play-a`의 local mailbox에 생성된다. 후속 actor request가 같은 노드의 actor로 dispatch된다.
- 검증(callback): actor lifecycle callback `Created` → `Joined`가 `play-a`에서 순서대로 발화해 그 노드 evidence에 기록되고, `play-b`에는 남지 않는다. (callback 계약 자체는 기존 in-process 테스트가 고정 — 여기선 발화 노드만 확인)
- 세부 동작: local actor join + local mailbox 실행 + lifecycle callback.

#### SM-B2 remote actor join

우선순위: `P0`

- 절차: consumer가 entry spot에 actor join을 요청하되, 대상이 원격 노드(`play-b`)로 resolve되도록 한다(또는 `play-a` consumer가 `play-b` 소유 actor를 join).
- 검증: actor가 `play-b`의 remote mailbox에 생성된다. join이 노드 경계를 넘어 라우팅되고 후속 actor request가 cross-node로 그 actor에 dispatch된다.
- 검증(callback): `Created` → `Joined` callback이 원격 노드 `play-b`에서 발화해 그 노드 evidence에 기록된다. join을 트리거한 `play-a`에는 actor 생성 callback이 남지 않는다.
- 세부 동작: remote actor join + cross-node mailbox 실행 + lifecycle callback.

#### SM-B3 요청 message 객체 충실도

우선순위: `P0`

- 절차: actor join과 후속 actor request를 **여러 필드·중첩 객체·컬렉션**을 가진 message 객체로 보낸다(예: `JoinReq{ actorId, displayName, level, attributes:{...}, tags:[...] }`).
- 검증: actor/handler가 받은 객체의 모든 필드가 보낸 값과 정확히 일치한다 — 필드 누락·타입 손상·null 치환·컬렉션 순서 변형이 없다. reply도 핵심 필드를 그대로 반영하고, evidence에 수신 필드가 기록된다.
- 세부 동작: 요청 payload 객체 round-trip 충실도.

#### SM-B4 remote actor request

우선순위: `P1`

- 절차: `play-a`의 consumer가 `play-b`에 있는 actor로 request를 보낸다.
- 검증: 노드 경계를 넘어 대상 actor에서 처리되고 reply가 돌아온다.
- 세부 동작: cross-node actor routing.

#### SM-B5 actor 미등록 request

우선순위: `P0`

- 절차: handler 없는 actor packet 이름으로 request를 보낸다.
- 검증: error reply로 끝나고 observer evidence(`ZLinkMessageDispatchErrorEvent`: `Surface`=`SpotActor`, `Reason`=`HandlerMissing`, `Action`=`ReplyError`/`Drop`)가 남는다.
- 세부 동작: actor negative path + 관측(enum 필드).

#### SM-B6 actor leave vs disconnect callback

우선순위: `P0`

- 절차: join한 actor에 대해 (a) 앱이 명시적으로 `leaveActor(...)`로 떠나보내거나, (b) stream 연결을 비정상 종료한 뒤 session handler가 `NotifyDisconnectedAsync(...)`를 호출한다. (bound session `DisconnectAsync`는 actor membership을 자동으로 바꾸지 않는다.)
- 검증: (a)는 actor 소유 노드의 spot에서 `OnLeaveActorAsync`가, (b)는 `OnDisconnectActorAsync`가 1회 발화해 evidence에 남는다. leave/통지하지 않은 actor에는 발화하지 않는다. 발화는 actor당 1회로 중복·누락이 없다.
- 세부 동작: 명시적 leave(`leaveActor`→`OnLeaveActorAsync`) vs 비정상 disconnect(`NotifyDisconnectedAsync`→`OnDisconnectActorAsync`).

#### SM-B7 actor handler 실행 순서

우선순위: `P1`

- 절차: 한 actor에 lifecycle(`Created`·`Joined`)과 packet handler를 함께 등록하고, join 직후 여러 packet을 연속으로 보낸다.
- 검증: lifecycle callback과 packet handler가 문서가 정한 순서(예: `Created` → `Joined` → packet dispatch)대로 실행되고, 같은 actor의 packet은 직렬로 순서 보존되어 처리된다. evidence에 실행 순서가 남는다.
- 세부 동작: actor handler 실행 순서 보장.

### Track C — messaging 방향

각 방향은 한 시나리오 안에서 send·request·publish verb와 timeout·미등록 negative를 모두
검증한다(같은 세부 동작 매트릭스를 방향만 바꿔 적용).

#### SM-C1 channel → spot messaging

우선순위: `P0`

- request: channel client가 spot으로 request → 정확한 reply, spot evidence에 기록.
- send: one-way send → reply 없이 spot evidence에 command 기록.
- publish: channel이 publish → 구독한 spot이 수신(미구독 spot은 미수신).
- timeout: 느린 spot handler에 짧은 timeout → client timeout 예외, 이후 같은 연결의 정상 messaging 비오염.
- 미등록: handler 없는 spot packet → request는 error reply, send는 drop. observer evidence(`ZLinkMessageDispatchErrorEvent`: `Surface`=`SpotRoute`, `Reason`=`HandlerMissing`, `Action`=`ReplyError`/`Drop`).
- 세부 동작: 외부 channel에서 spot으로 들어오는 방향 전체 verb + negative.

#### SM-C2 spot → channel messaging

우선순위: `P0`

- request: spot handler가 처리 중 외부 channel로 request → reply를 받아 처리에 반영.
- send: spot → channel one-way → 대상 channel server evidence에 기록.
- publish: spot이 SPOT mesh로 publish(`IZLinkSpotOutbound.Publish(topic, msg)`) → 같은 SPOT mesh의 구독 spot 전원 수신(미구독은 미수신). (외부 channel로의 publish는 public API에 없음 — 외부로는 `SendToChannel`/`RequestToChannel`만.)
- timeout: 느린 channel handler에 짧은 timeout → spot 쪽 timeout 처리, spot 상태 비오염.
- 미등록: 대상 channel에 handler 없음 → request는 error reply, send는 drop + observer marker.
- 세부 동작: spot에서 외부 channel로 나가는 send/request + SPOT mesh publish + negative.

#### SM-C3 spot → spot messaging

우선순위: `P1`

- request: 한 user spot이 다른 user spot으로 request → reply, 양쪽 evidence 일치.
- send: spot → spot one-way → 대상 spot evidence 기록.
- publish: spot 이벤트를 다른 spot이 구독 수신.
- timeout: 느린 대상 spot에 timeout → 소스 spot 정상 유지.
- 미등록: 대상 spot에 handler 없음 → error/drop + observer marker.
- 세부 동작: spot 간 직접 messaging 전체 verb + negative.

### Track D — session bind·relay·push와 stream

stream session은 actor에 bind되어 양방향으로 메시지를 relay한다. bind 위치(local/remote),
actor가 사는 spot 종류(entry/user), 한 session에 bind되는 actor 수(단일/다중)를 나눠서
검증한다.

#### SM-D1 actor session bind & relay — local

우선순위: `P0`

- 절차: consumer가 `session-a` gateway에 stream으로 접속·auth하고, `session-a`의 **preferred play 노드(`play-a`)**의 actor에 bind한다. client → actor request(`actor-id` metadata 포함)를 보내고, actor → client push를 트리거한다.
- 검증: bind 성립 후 client packet이 `header.Metadata.Get("actor-id")`로 bound actor에 relay되어 처리되고, actor push가 같은 session으로 relay되어 client가 받는다(양방향). bind 안 한 client는 받지 않는다.
- 세부 동작: gateway → preferred play 노드 actor relay.

#### SM-D2 actor session bind & relay — remote

우선순위: `P0`

- 절차: consumer가 `session-a` gateway에 붙고, bind 대상 actor는 `session-a`의 **preferred가 아닌 원격 play 노드**(`play-b`)에 있도록 한다. 같은 양방향 relay를 수행한다.
- 검증: client packet이 gateway → 원격 play 노드로 노드 경계를 넘어 actor에 relay되고, 원격 actor의 push가 gateway를 거쳐 같은 session으로 돌아온다.
- 세부 동작: gateway↔원격 play 노드 cross-node relay.

#### SM-D3 entry spot vs user spot actor bind

우선순위: `P1`

- 절차: bind 대상 actor가 (1) entry spot에 있는 경우와 (2) user spot에 있는 경우 각각 session을 bind한다.
- 검증: 두 경우 모두 bind·양방향 relay가 정상이며, push·dispatch가 actor가 사는 spot 종류와 무관하게 같은 의미로 동작한다.
- 세부 동작: spot 종류별 bind 동등성.

#### SM-D4 한 stream에 여러 actor bind

우선순위: `P0`

- 절차: 한 stream session에 여러 actor(예: `actor-x`, `actor-y`)를 bind한다. stream header metadata `actor-id`(`header.Metadata.Get("actor-id")`)에 대상 actor id를 실어 보내고, session handler가 `Context.Actors.Find(actorId)?.RelayAsync(...)`로 해당 actor에 relay한다. 각 actor가 push를 낸다.
- 검증: 각 packet이 `actor-id`로 지정한 actor로만 relay되고(교차 오배달 없음), `actor-id` 없이 다중 bound 상태로 보내면 `ActorRouteNotFound`로 실패한다(기본 relay는 단일 bound일 때만). 각 actor push가 같은 session으로 relay되어 client가 actor별로 구분해 받는다.
- 세부 동작: 다중 actor bind + `actor-id` metadata 선택 relay (단일 bound만 기본 relay).

#### SM-D5 session disconnect → 명시적 actor 통지

우선순위: `P0`

- 절차: SM-D1~D4로 bind한 상태에서 stream 연결을 비정상 종료(disconnect)한다. session handler의 `OnDisconnectedAsync`에서 선택한 bound actor에 `NotifyDisconnectedAsync(...)`를 호출한다. 단일·다중 bind, local·remote를 모두 시도한다.
- 검증: session disconnect 자체는 actor membership을 자동으로 바꾸지 않는다. handler가 `NotifyDisconnectedAsync`를 호출한 actor에 한해, 그 actor가 사는 노드의 spot에서 `OnDisconnectActorAsync` callback이 1회 발화해 evidence에 남는다. 통지하지 않은 actor에는 발화하지 않는다.
- 세부 동작: session `OnDisconnectedAsync` → 선택 actor `NotifyDisconnectedAsync` → spot `OnDisconnectActorAsync` (자동 fanout 아님).

#### SM-D6 bound session push 타깃팅

우선순위: `P0`

- 절차: consumer가 session에 bind한 뒤, 다른 경로로 그 actor 상태를 바꾼다.
- 검증: 상태 변경이 bound session으로만 push되어 해당 consumer가 수신한다. bind 안 한 consumer는 받지 않는다.
- 세부 동작: bound session push 타깃팅.

#### SM-D7 stream session auth와 packet dispatch

우선순위: `P0`

- 절차: consumer가 stream client로 접속·auth하고 packet을 주고받는다.
- 검증: auth 성공 후 request/notify가 정상 dispatch된다. auth 실패는 공개 오류.
- 세부 동작: stream session 수명 + dispatch.

#### SM-D8 stream reconnect

우선순위: `P1`

- 절차: stream 연결을 끊었다가 재접속한다. 끊김 시점의 pending request 결과와 재접속 후 동작을 본다.
- 검증: 끊김 시 pending request는 `Disconnected`로 실패하고 자동 재전송되지 않는다. 재접속은 새 session이므로 app이 다시 auth·rebind하고, 필요한 상태는 replay/snapshot packet으로 복구한다. rebind 후 messaging이 정상 재개된다.
- 세부 동작: 재접속 = 재auth·rebind + app replay (자동 재전송 없음).

#### SM-D9 inbound observer

우선순위: `P1`

- 절차: stream inbound observer를 등록하고 메시지를 받는다.
- 검증: observer가 inbound 종류·이름·seq를 관측 evidence로 남긴다.
- 세부 동작: inbound 관측.

#### SM-D10 stream backpressure

우선순위: `P1`

- 절차: stream client가 처리 속도보다 빠르게 메시지를 주고받아 backpressure를 유발한다.
- 검증: 정해진 흐름 제어 규칙(버퍼/대기/drop)대로 동작하고, session이 깨지지 않으며 다른 session에 영향이 없다.
- 세부 동작: stream 흐름 제어.

#### SM-D11 stream request와 channel request 혼합

우선순위: `P1`

- 절차: 같은 consumer가 stream session request와 일반 channel request를 섞어 보낸다.
- 검증: 두 경로가 서로 간섭 없이 각자 정상 동작하고, reply가 올바른 경로로 돌아온다.
- 세부 동작: stream·channel 혼합 경로 격리.

#### SM-D12 session 재접속 이전성 (다른 연결 서버로)

우선순위: `P0`

- 절차: client가 연결 서버 `session-a`에 붙어 play 노드 actor와 messaging하다가 연결을 끊고 **다른 연결 서버 `session-b`**로 재접속한다.
- 검증: 로직(play 노드)의 actor 상태는 연결 서버와 무관하게 유지된다. client는 `session-b`에서 다시 auth하고 같은 actor id로 rebind한 뒤 snapshot + 이후 event로 상태를 복구한다(자동 이전·재전송 아님). rebind 후 messaging이 정상 재개된다.
- 세부 동작: 연결/로직 분리 — 다른 gateway로 재auth·rebind 이전성.

#### SM-D13 stream heartbeat

우선순위: `P1`

- 절차: stream 연결을 유지하며 heartbeat 주기를 지나친다. 한쪽이 heartbeat를 멈춘다.
- 검증: 정상 heartbeat 동안 session이 살아 있고, heartbeat 중단은 connector에서 `Disconnected` 상태/오류로 감지되며 server stream session은 `OnDisconnectedAsync`가 돈다. bound actor의 `Disconnected`는 session handler가 명시적으로 `NotifyDisconnectedAsync`를 호출한 경우에만 발생한다(자동 아님).
- 세부 동작: heartbeat 기반 liveness (session disconnect와 actor 통지 분리).

#### SM-D14 stream TLS

우선순위: `P2`

- 절차: stream 연결을 TLS로 수립해 auth·messaging을 수행한다.
- 검증: TLS 위에서 bind·relay·push가 평문과 같은 의미로 동작한다. 잘못된 인증서는 연결 거부.
- 세부 동작: TLS stream 전송.

### Track E — negatives와 timer

#### SM-E1 spot route 미등록 request

우선순위: `P0`

- 절차: handler 없는 spot route packet으로 request를 보낸다.
- 검증: error reply + observer evidence(`ZLinkMessageDispatchErrorEvent`: `Surface`=`SpotRoute`, `Reason`=`HandlerMissing`, `Action`=`ReplyError`/`Drop`).
- 세부 동작: spot route negative path(enum 필드).

#### SM-E2 spot timer

우선순위: `P1`

- 절차: spot이 timer를 건다.
- 검증: timer가 한 번/주기대로 발화하고 그 효과(상태 변화·push)가 관측된다.
- 세부 동작: spot timer 발화.

#### SM-E3 idle timer 기반 명시적 close

우선순위: `P1`

- 절차: user spot이 `AddTimer<THandler>`로 주기 timer를 돌리며 마지막 활동 시각을 기록한다. idle 임계를 넘으면 timer handler가 (joined actor가 모두 떠난 뒤) `CloseAsync`를 호출한다. 활동이 오면 마지막 활동 시각을 갱신한다.
- 검증: idle 초과 시 spot이 `CloseAsync`로 닫히고 `OnClosingAsync` 콜백이 evidence에 기록된다. joined actor가 남아 있으면 close가 거부된다(먼저 actor leave 필요). 활동 중엔 닫히지 않는다.
- 세부 동작: 애플리케이션 idle timer + 명시적 `CloseAsync` + `OnClosingAsync` (자동 actor callback 아님).

## 5. 완료 기준

- Track A~E의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.
- 실패 시 registry/spot/session/consumer 로그와 evidence로 원인 레이어를 분리한다.
