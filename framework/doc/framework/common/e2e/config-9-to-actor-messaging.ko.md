<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: 실행 turn과 terminator](config-8-execution-turn.ko.md) | [다음: Spot actor join/transfer](config-10-spot-actor-transfer.ko.md)
<!-- framework-adapter-nav:end -->

# Config 9 — To-actor 메시징 배포

서버 측 caller가 session을 통하지 않고 `ActorRef`로 actor에게 message를 보내거나 request를 보낼 때,
actor의 현재 bind 상태와 무관하게 같은 의미로 처리되는지 본다. 이 config는 모든 framework 언어가
같은 의미로 통과해야 하는 공통 검증이다. 구현 스위트와 API 이름은 언어별 idiom을 따르지만, 검증
조건과 evidence marker는 네 언어에서 같은 의미로 유지한다.

이 문서는 e2e 시나리오 정의만 둔다. actor client의 공개 계약은
[actor model 6.1절](../../spec/server/22-actor-model.ko.md)을 기준으로 하며 여기서 다시 정의하지 않는다.
언어별 구현은 public API만 사용하고, 내부 helper나 raw frame 조작으로 이 config를 통과시키면 안 된다.

## 1. 목적과 범위

- 다룬다: bind된 actor, bind되지 않은 actor, no-bind 전달 뒤 session bind가 생기는 actor,
  bind가 사라진 actor에 대한 to-actor send/request, bind 비오염, mailbox 인계, handler reply,
  actor 부재·stale location·route 미연결 실패 분류.
- 여기서 다루지 않는다: actor 생성·join 자체의 기본 동작(Config 2), 비동기 handler와 actor mailbox 격리(Config 8),
  일반 channel location resolve(Config 1), store 장애·복구(Config 6), actor client API 설계.
- 계약 근거: send-to-actor/request-to-actor는 `ActorRef`를 받고, send 완료는 actor owner의 로컬
  mailbox 인계 성공을 뜻한다. 실패 분류는 `ActorRouteNotFound`, `ActorLocationStale`,
  `RouteNotConnected`를 참조한다.
- protocol 근거: no-bind 전달은 session binding을 만들거나 갱신하지 않고, request reply는 bound
  session이 아니라 caller에게 돌아와야 한다. actor mailbox 인계와 request reply correlation은
  core의 server-to-actor no-bind 경로를 따른다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. actor location row와 owner lease는 framework lifecycle이 관리한다. |
| actor 노드 | 2 (`actor-a`, `actor-b`) | Entry Spot + actor mailbox host. actor handler는 no-bind send/request 수신 evidence와 reply를 남긴다. actor→bound session push를 발생시키는 endpoint 또는 handler를 제공한다. |
| session gateway | 2 (`session-a`, `session-b`) | stream session을 받고 actor bind를 만드는 실제 연결 서버. client connector와 actor bound-session push 경로를 검증한다. |
| 외부 caller 서버 | 1 | session을 만들지 않는 서버 측 caller 역할. app endpoint 안에서 언어별 public actor client로 `SendToActor`와 `RequestToActor` 의미의 호출을 실행한다. |
| consumer | 시나리오별 | stream connector로 session gateway에 연결해 bind와 push를 관찰하고, HTTP client wrapper로 외부 caller 서버의 app endpoint를 호출한다. framework actor client를 직접 들고 호출하지 않는다. |

actor 노드는 아래 evidence를 공통으로 남긴다.

- actor id, actor generation 또는 ref snapshot, 처리 노드, handler packet 이름, request id.
- no-bind send가 actor mailbox에 인계되었는지 나타내는 marker.
- no-bind request handler reply가 caller 서버로 돌아갔는지 나타내는 marker.
- actor의 bound-session 대상이 바뀌었는지 확인할 수 있는 bind snapshot marker.
- actor가 자기 bound session으로 push를 보냈을 때 어느 session gateway와 client connector가 받았는지
  나타내는 marker.

외부 caller 서버는 시나리오 실행 전용 driver가 아니다. consumer는 caller 서버의 app endpoint를 호출해
실제 사용자 동작을 트리거하고, caller 서버 endpoint 내부에서 public actor client 호출이 실행되어야 한다.
session bind와 push 검증은 client stream connector가 받은 payload와 actor/session gateway evidence를
함께 대조한다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → actor 노드 → session gateway → 외부 caller 서버 순으로
띄운 뒤 client 시나리오를 순차 실행한다. 각 시나리오는 필요한 actor를 만들고, 필요한 경우 stream
connector로 session을 bind한 뒤, caller 서버 endpoint를 호출해 to-actor send/request를 발생시킨다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 지원하는 언어는 최소 `key_transitions`로 켠다. 실패 시에는
caller 서버의 request id, actor 노드의 mailbox marker, session gateway의 push marker, client connector가
받은 payload를 함께 남긴다.

## 4. 시나리오

### Track A — bind 상태 매트릭스

#### TA-A1 bind된 actor에게 no-bind send/request

우선순위: `P0`

**한마디로:** 이미 session에 bind된 actor에게 외부 caller 서버가 to-actor send/request를 보내도, actor는 처리하고 기존 bound session은 바뀌지 않는가.

- 절차: consumer가 `session-a`에 stream connector로 연결하고 actor `actor-bound`를 bind한다. actor가 자기 bound session으로 `BeforeNotify`를 push해 원래 client가 받는지 확인한다. 그 뒤 외부 caller 서버 endpoint를 호출해 해당 `ActorRef`로 send와 request를 각각 보낸다. 마지막으로 actor가 다시 `AfterNotify`를 bound session으로 push한다.
- 검증: send는 actor handler evidence에 기록되고 request는 caller 서버가 handler reply를 받는다. `BeforeNotify`와 `AfterNotify`는 모두 처음 bind한 client connector로만 도착한다. no-bind 전달 전후 actor의 bound-session snapshot은 같은 session을 가리킨다. caller 서버가 session으로 새 bind를 만들거나 기존 bind를 갱신한 evidence가 없어야 한다.
- 세부 동작: bind된 actor에 대한 no-bind 전달 + bound-session 비오염.

#### TA-A2 bind 안 된 actor에게 no-bind send/request

우선순위: `P0`

**한마디로:** bound session이 없는 actor에게도 외부 caller 서버가 actor mailbox로 메시지를 넘기고 request reply를 받을 수 있는가.

- 절차: actor `actor-unbound`를 만들되 stream session bind는 만들지 않는다. 외부 caller 서버 endpoint를 호출해 생성 결과의 `ActorRef`로 send와 request를 각각 보낸다.
- 검증: actor handler가 send를 처리했다는 evidence를 남긴다. request reply는 caller 서버로 돌아온다. actor의 bound-session snapshot은 비어 있는 상태로 유지된다. session gateway나 client connector에는 해당 actor의 push 또는 bind 갱신 marker가 생기지 않는다.
- 세부 동작: bound session 없는 actor의 mailbox 인계와 handler reply.

#### TA-A3 no-bind 전달 뒤 이후 bind

우선순위: `P0`

**한마디로:** bind되지 않은 actor가 no-bind send/request를 받은 뒤 나중에 session bind를 만들어도, 두 경로가 서로 오염되지 않는가.

- 절차: `actor-late-bind`를 bind 없이 만든다. 외부 caller 서버에서 send와 request를 보낸다. 이후 consumer가 `session-b`에 stream connector로 연결하고 같은 actor를 bind한다. bind 뒤 caller 서버에서 다시 send와 request를 보내고, actor가 자기 bound session으로 `LateBindNotify`를 push한다.
- 검증: bind 전 send/request와 bind 후 send/request가 모두 actor handler evidence와 caller 서버 reply로 확인된다. bind 전에는 bound-session snapshot이 비어 있고, bind 후에는 새 session을 가리킨다. `LateBindNotify`는 새로 bind한 client connector로 도착한다. no-bind 호출이 bind 생성을 대신했다는 marker가 없어야 한다.
- 세부 동작: no-bind 전달과 이후 session bind의 독립성.

#### TA-A4 unbind/disconnect 뒤 actor 생존과 destroy 분리

우선순위: `P0`

**한마디로:** actor의 session이 끊겨도 actor가 살아 있으면 to-actor 전달은 계속 성공하고, actor가 사라진 뒤에는 actor 부재 실패로 분류되는가.

- 절차: consumer가 actor `actor-disconnected`를 bind한 뒤 stream connection을 정상 unbind 또는 disconnect한다. actor lifecycle 정책상 actor는 살아 있게 둔다. 외부 caller 서버가 보관한 `ActorRef`로 send와 request를 보낸다. 그 뒤 actor를 명시적으로 destroy하거나 actor owner에서 제거한 뒤 같은 ref로 request를 다시 보낸다.
- 검증: disconnect 뒤 actor가 살아 있는 동안 send/request는 actor handler evidence와 caller 서버 reply로 성공한다. bound-session snapshot은 비어 있거나 끊긴 session을 더 이상 유효 대상으로 쓰지 않는 상태로 관측된다. actor destroy 뒤 같은 ref 호출은 `ActorRouteNotFound`로 분류된다. destroy 뒤에는 handler evidence가 새로 생기지 않는다.
- 세부 동작: session 생명주기와 actor 생명주기 분리 + actor 부재 실패 분류.

### Track B — 실패 분류

#### TA-B1 없는 actor

우선순위: `P0`

**한마디로:** live actor와 일치하지 않는 `ActorRef`로 호출하면 actor가 자동 생성되거나 메시지가 보관되지 않고 `ActorRouteNotFound`로 실패하는가.

- 절차: 알려진 actor node/type/id 형식을 사용하되 live actor와 일치하지 않는 ref로 외부 caller 서버가 request와 send를 시도한다.
- 검증: request는 caller 서버에서 `ActorRouteNotFound`로 실패한다. reply가 없는 send의 submit은 로컬 전송 접수까지만 나타내며 원격 actor의 존재 여부를 확인하는 수단으로 사용하지 않는다. send 뒤 actor 노드에는 해당 actor id의 handler evidence가 없고 actor location row도 새로 만들어지지 않는다. auto-create나 메시지 파킹이 없어야 하며, 호출자가 actor 부재를 확인해야 하는 흐름은 request를 사용한다.
- 세부 동작: actor row 없음 실패 분류.

#### TA-B2 stale actor ref

우선순위: `P0`

**한마디로:** 이전 generation의 actor ref를 쓰면 `ActorLocationStale`로 분류되는가.

- 절차: actor를 만들고 caller 서버가 정상 `ActorRef`를 얻는다. 이후 actor owner를 교체하거나 generation을 바꾸어 이전 ref가 stale이 되게 한 뒤 그 ref로 호출한다.
- 검증: stale 호출은 `ActorLocationStale`로 분류된다. 이전 owner와 새 owner의 evidence를 비교해 어느 쪽에서도 잘못된 generation의 handler가 실행되지 않았음을 확인한다. caller가 resolver에서 새 live ref를 다시 얻어 호출한 경우는 별도 성공 경로로 기록한다.
- 세부 동작: stale location 분류와 stale owner로 잘못 보내지 않는 경로.

#### TA-B3 route 미연결

우선순위: `P0`

**한마디로:** 대상 actor node rid는 알려졌지만 routed plane으로 보낼 수 없으면 `RouteNotConnected`로 분류되고 재시도 가능한 실패로 남는가.

- 절차: actor의 `ActorRef`를 얻은 뒤 caller 서버에서 actor owner node로 가는 route 연결을 끊는다. 외부 caller 서버가 해당 ref로 request를 보낸다. 연결 복구 뒤 같은 ref로 다시 request를 보낸다.
- 검증: 단절 구간의 실패는 `RouteNotConnected`로 분류되고, actor handler evidence는 남지 않는다. 복구 뒤 follow-up request는 같은 actor handler에서 처리되고 reply가 caller 서버로 돌아온다. 이 시나리오는 actor row 없음이나 stale location으로 판정하면 안 된다.
- 세부 동작: route 미연결 실패 분류와 복구 뒤 성공.

## 5. 완료 기준

- Track A와 Track B의 모든 `P0` 시나리오가 각 언어에서 public API만으로 구현되어야 한다.
- send와 request는 모두 검증한다. send는 actor owner의 mailbox 인계 evidence로, request는 actor handler
  reply가 caller 서버로 돌아온 evidence로 판정한다.
- bind 비오염은 client connector가 받은 push payload, actor bound-session snapshot, session gateway
  evidence를 함께 대조한다.
- 실패 분류는 caller 서버가 받은 public error kind와 역할 서버 evidence를 함께 확인한다. 오류 이름은
  `ActorRouteNotFound`, `ActorLocationStale`, `RouteNotConnected`를 그대로 사용한다.
- actor client가 있는 모든 언어는 이 시나리오를 skip 없이 구현한다.
