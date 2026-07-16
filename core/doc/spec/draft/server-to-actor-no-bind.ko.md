# Server-to-Actor No-Bind Gateway Protocol 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, `core/include/zlink.h` 또는
> `core/include/zlink/service/spot.h`에 반영된 정식 API 계약이 아니다. 구현과 회귀 테스트가 끝난 뒤에
> 실제 공개 header와 맞는 내용만 정식 spec으로 승격한다.

## 목적

framework의 actor client는 세션이 아닌 서버 측 호출자가 actor id로 message를 보내거나 request를 보낼 수
있어야 한다. 이 호출은 session binding을 만들거나 갱신하지 않아야 하며, request는 세션이 없어도 응답을
호출자에게 돌려줘야 한다.

공통 framework 계획은 L13에서 actor client 계약을 보류 상태로 두고 있다. 계약 요지는 actor id 단독
send/request, await 완료 의미를 "resolve 성공과 actor owner의 로컬 mailbox 인계 성공"으로 두는 것, 실패를
`ActorRouteNotFound` / `ActorLocationStale` / `RouteNotConnected`로 분류하는 것이다
(`framework/doc/plan/framework-public-contract-posd-redesign.ko.md:72`). 같은 문서의 Q1은 core gateway
protocol 확장을 선택했고, `server_to_actor_no_bind` packet kind 또는 skip-bind flag와 reply correlation을
설계하라고 결정했다(`framework/doc/plan/framework-public-contract-posd-redesign.ko.md:246`,
`framework/doc/plan/framework-public-contract-posd-redesign.ko.md:254`,
`framework/doc/plan/framework-public-contract-posd-redesign.ko.md:257`).

## 현재 문제

현재 actor gateway의 세션 → actor 경로는 source session을 항상 actor의 bound session으로 기록한다.
core 수신 경로는 `packet_session_to_actor`를 받으면 actor를 찾은 뒤 `bound_session_node_rid`와
`bound_session_rid`를 source 값으로 갱신한다
(`core/src/api/actor/spot/service_spot_actor_api.cpp:1606`,
`core/src/api/actor/spot/service_spot_actor_api.cpp:1609`). .NET framework 수신 dispatcher도 actor packet을
처리할 때 bound-session scope에 들어가고 binding을 갱신한다
(`framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkEntrySpotActorDispatcher.cs:119`,
`framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkEntrySpotActorDispatcher.cs:120`,
`framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActivationDispatcher.cs:320`,
`framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActivationDispatcher.cs:321`).

이 경로를 세션이 아닌 서버 발신자에게 그대로 쓰면 두 문제가 생긴다.

1. 발신자가 실제 session이 아니어도 actor의 bound session이 가짜 값으로 오염된다.
2. request reply가 bound session으로 돌아가야 한다고 해석되므로, 세션 없는 호출자에게 응답을 반환할 경로가
   없다.

현재 gateway protocol에는 no-bind 의미를 나타내는 kind나 flag가 없다. packet kind는
`packet_session_to_actor`, `packet_actor_to_session`, join request/reply 계열만 정의되어 있다
(`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:15`,
`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:17`,
`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:22`). control frame은
`request_id` 필드를 이미 갖고 있지만, parser가 `session_size == 0`을 protocol error로 거부한다
(`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:36`,
`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:90`,
`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:92`).

## Protocol 확장 판정

새 packet kind를 추가한다. 이름은 `packet_server_to_actor_no_bind`로 둔다. skip-bind flag는 채택하지 않는다.

근거:

- 현재 actor gateway control frame은 byte 4에 `kind`, byte 5에 `part_flag`를 둔다
  (`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:60`,
  `core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:61`). `part_flag`는
  multipart 경계 의미가 이미 있으므로 skip-bind bit를 섞으면 한 필드가 두 책임을 갖는다.
- frame header는 고정 크기 30바이트이고, 기존 `request_id` 필드가 byte 18부터 기록된다
  (`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:20`,
  `core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:66`). 새 kind는 이
  layout을 바꾸지 않는다.
- 기존 parser는 알 수 없는 kind를 frame parse 단계에서 거부하지 않고, dispatch 단계에서 알려진 kind만
  처리한다(`core/src/api/actor/spot/service_spot_actor_api.cpp:1874`,
  `core/src/api/actor/spot/service_spot_actor_api.cpp:1882`,
  `core/src/api/actor/spot/service_spot_actor_api.cpp:1889`). 새 kind는 기존 노드가 새 의미를 잘못 session
  경로로 처리하는 위험을 줄인다.
- skip-bind flag는 기존 `packet_session_to_actor`를 받는 구버전 노드가 flag를 무시하고 bind를 갱신할 수
  있다. 새 kind는 구버전 노드에서 처리 실패가 되므로 wire compatibility 관점에서 더 안전하다.

wire layout은 `ZAG1` control frame을 유지한다. `packet_server_to_actor_no_bind`의 `session_rid` 자리에는
caller endpoint rid를 넣는다. 이 값은 session rid가 아니며, actor mailbox로 넘기는 source metadata나
reply address로만 쓴다. parser의 `session_size == 0` 금지는 유지한다. 세션이 없다는 뜻은 "rid가 비어
있다"가 아니라 새 kind가 표현한다.

## 라우팅 경로

no-bind packet도 기존 actor gateway endpoint class와 endpoint name을 사용한다. 현재 전송 helper는
actor gateway source/destination class와 `__zlink.actor-gateway` endpoint name으로 routed envelope를 만든다
(`core/src/api/actor/spot/service_spot_actor_api.cpp:549`,
`core/src/api/actor/spot/service_spot_actor_api.cpp:550`,
`core/src/api/actor/spot/service_spot_actor_api.cpp:552`,
`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.cpp:23`).
수신 node는 routed envelope의 destination class가 actor gateway이면 `process_gateway_delivery()`로 넘긴다
(`core/src/api/spot/request_reply/service_spot_request_reply_ingress_api.cpp:112`,
`core/src/api/spot/request_reply/service_spot_request_reply_ingress_api.cpp:114`,
`core/src/api/spot/request_reply/service_spot_request_reply_routed_delivery.cpp:381`,
`core/src/api/spot/request_reply/service_spot_request_reply_routed_delivery.cpp:385`).

설계상 경로는 다음과 같다.

```text
caller SpotNode
  -> SPOT routed envelope (actor-gateway endpoint)
  -> actor owner SpotNode
  -> actor gateway no-bind control frame
  -> actor mailbox
```

actor owner가 같은 node이면 기존 routed local delivery 최적화 안에서 같은 의미를 유지한다. owner가 remote
node이면 routed router를 통해 owner node까지 간 뒤 actor gateway 수신 경로에서 mailbox에 넣는다.

## Reply Correlation

기존 SPOT routed request는 `request_seq`를 할당하고 pending map에 등록한 뒤, routed request header에 같은
값을 넣어 reply와 대조한다
(`core/src/api/spot/request_reply/service_spot_request_reply_submit_api.cpp:126`,
`core/src/api/spot/request_reply/service_spot_request_reply_submit_api.cpp:130`,
`core/src/api/spot/request_reply/service_spot_request_reply_submit_api.cpp:138`,
`core/src/api/spot/request_reply/service_spot_request_reply_submit_api.cpp:140`,
`core/src/api/spot/request_reply/service_spot_request_reply_submit_api.cpp:158`,
`core/src/api/spot/request_reply/service_spot_request_reply_submit_api.cpp:161`). pending key는 source class, source
rid, source spot rid, request sequence로 구성된다
(`core/src/api/spot/request_reply/service_spot_request_reply_internal.hpp:35`,
`core/src/api/spot/request_reply/service_spot_request_reply_internal.hpp:40`).

actor gateway no-bind request는 이 구조를 그대로 복사하지 않는다. SPOT routed envelope header에는 별도
message type과 request sequence가 없고, actor gateway control frame에는 이미 `request_id`가 있다. 따라서
actor no-bind request의 correlation은 actor gateway control frame의 `request_id`를 사용한다.

규칙:

- caller node는 no-bind request를 보낼 때 actor gateway request pending table에 `request_id`를 등록한다.
  key는 `(target actor node rid, actor id, generation, caller endpoint rid, request_id)`로 구성한다.
- payload의 첫 part는 actor gateway control frame이고, `request_id != 0`이면 request다. `request_id == 0`이면
  one-way send다.
- actor owner는 handler reply를 만들 때 `packet_actor_to_server_no_bind_reply` kind와 같은 `request_id`를
  control frame에 넣어 caller endpoint로 돌려보낸다.
- caller node는 reply frame의 `request_id`로 pending table을 찾고 callback 또는 completion queue를 완료한다.
- timeout과 close 동작은 기존 routed request의 pending sequence 관리와 같은 정책을 따른다. 기존 구현은
  pending sequence를 등록하고 timeout 시 제거한다
  (`core/src/api/spot/request_reply/service_spot_request_reply_timeout.cpp:191`,
  `core/src/api/spot/request_reply/service_spot_request_reply_timeout.cpp:198`,
  `core/src/api/spot/request_reply/service_spot_request_reply_timeout.cpp:217`,
  `core/src/api/spot/request_reply/service_spot_request_reply_timeout.cpp:264`,
  `core/src/api/spot/request_reply/service_spot_request_reply_timeout.cpp:271`).

one-way send도 framework await 의미를 만족하려면 actor owner의 mailbox 인계 결과가 필요하다. 따라서 C API
one-way send는 내부적으로 `request_id`가 있는 delivery-ack packet을 사용한다. actor handler reply payload는
없지만, actor owner는 mailbox enqueue 성공 또는 실패를 ack로 돌려준다. 이 ack는 application reply가 아니며,
framework의 "전달 접수" 완료에만 쓴다.

## 수신측 의미론

`packet_server_to_actor_no_bind` 수신은 actor를 찾은 뒤 mailbox에 message part를 넣는다. 이때 bound-session
상태는 갱신하지 않는다. 기존 session 경로가 수행하는 `bound_session_node_rid`와 `bound_session_rid` 갱신은
새 kind에서 실행하지 않는다
(`core/src/api/actor/spot/service_spot_actor_api.cpp:1606`,
`core/src/api/actor/spot/service_spot_actor_api.cpp:1609`).

mailbox에 들어가는 `zlink_actor_recv_info_t`의 source에는 caller endpoint rid를 넣는다. 이 값은 session rid가
아니므로 session-bound reply API가 이 값을 bound session으로 해석하면 안 된다. no-bind request reply는
위의 actor gateway reply correlation 경로로만 반환한다.

actor가 없거나 generation이 맞지 않으면 actor owner는 오류 reply를 보낸다. framework 실패 분류에서는 actor
row 없음이 `ActorRouteNotFound`이고, stale location은 `ActorLocationStale`로 분리된다
(`framework/doc/plan/framework-dotnet-location-contract-posd-redesign-plan.ko.md:1221`,
`framework/doc/plan/framework-dotnet-location-contract-posd-redesign-plan.ko.md:1222`,
`framework/doc/plan/framework-dotnet-location-contract-posd-redesign-plan.ko.md:1226`). core는 native result를
구분해서 넘겨야 하며, framework binding은 이를 해당 public error kind로 매핑한다.

## C API 표면

기존 `zlink.h` 표면만으로는 충분하지 않다. 기존 actor send 계열은 이름과 인자 모두 bound session을 전제로
한다. 예를 들어 `zlink_spot_node_actor_send_bound_session_msg()`는 actor에서 bound session으로 보내고,
`zlink_spot_node_actor_forward_bound_session_part()`는 source node/session rid를 요구한다
(`core/include/zlink/service/spot.h:147`, `core/include/zlink/service/spot.h:150`,
`core/include/zlink/service/spot.h:153`, `core/include/zlink/service/spot.h:154`). 정식 spec도 이 함수를
session -> actor ingress relay로 설명한다(`core/doc/spec/core/service/03-spot.ko.md:1397`,
`core/doc/spec/core/service/03-spot.ko.md:1400`, `core/doc/spec/core/service/03-spot.ko.md:1402`).

따라서 새 public C API가 필요하다. `actor_*` 계열 이름은 actor가 주어처럼 읽히므로, 공개 함수는
node에서 actor로 보내는 방향이 드러나는 이름을 사용한다. wire kind의 `no_bind` 이름은 protocol 구분을
위해 유지한다.

```c
zlink_submit_result_t zlink_spot_node_send_to_actor(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn completion,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_spot_node_request_to_actor(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn callback,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

`send_no_bind`의 completion은 actor owner mailbox 인계 ack를 나타내며 application payload를 반환하지 않는다.
`request_no_bind`의 callback은 actor handler reply를 반환한다. 두 함수 모두 caller가 이미 resolve한
`zlink_actor_ref_t`를 받는다. actor id 단독 lookup과 stale 재조회 정책은 framework location 계층의 책임으로
남긴다.
send도 actor mailbox에 envelope header와 payload를 함께 넣어야 하므로 request와 같은 multipart 인자를 받는다.

새 flag로 기존 bound-session 함수를 확장하지 않는다. 같은 함수에서 source session rid가 실제 session인지
caller endpoint인지 옵션으로 갈라지면 public C API가 얕아지고, binding과 framework가 session 의미를 매번
해석해야 한다.

binding 영향은 있다. `doc/site/docs/api/bindings.md`는 `core/include/zlink.h`의 모든 `ZLINK_EXPORT` 함수가
언어별 binding spec에서 빠지면 안 되고, 새 C API가 추가되면 모든 언어 spec도 함께 갱신해야 한다고 정한다
(`doc/site/docs/api/bindings.md:1022`, `doc/site/docs/api/bindings.md:1026`). 따라서 CORE-2에서 `zlink.h`에
새 함수가 들어가면 native package 업데이트 뒤 bindings spec과 언어별 binding wrapper 갱신이 필요하다.

## 회귀 테스트 계획

기존 core actor gateway 경로 위에 다음 테스트를 추가한다.

- no-bind 전달: `packet_server_to_actor_no_bind`가 actor owner node에서 actor mailbox로 들어가고,
  `zlink_spot_node_actor_recv_part()`가 payload를 읽는다.
- bind 비오염: no-bind 전달 전후 actor의 bound-session 상태가 바뀌지 않는다. 기존 session-bound
  `packet_session_to_actor`는 계속 bound session을 갱신해야 한다.
- send ack correlation: one-way send가 actor owner enqueue 성공 ack로 completion을 완료하고, `request_id`가
  잘못된 ack는 pending request를 완료하지 않는다.
- request reply correlation: no-bind request가 actor handler reply를 `request_id`로 caller pending table에
  돌려주고, 동시에 여러 request가 섞여도 각 callback이 자기 reply만 받는다.
- actor 부재: actor owner가 actor id 또는 generation mismatch를 만나면 오류 reply를 보내고, binding/framework
  매핑층이 `ActorRouteNotFound` 또는 `ActorLocationStale`로 구분할 수 있는 native result를 받는다.
- route 미연결: target node rid가 알려졌지만 routed plane으로 보낼 수 없으면 submit 단계에서
  not-connected 계열 result가 나오며, framework는 `RouteNotConnected`로 매핑할 수 있다.
- 기존 session 경로 무회귀: `packet_session_to_actor`, `packet_actor_to_session`, entry/user spot join
  request/reply가 기존 kind 값과 처리 경로를 유지한다
  (`core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:17`,
  `core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:18`,
  `core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:19`,
  `core/src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp:22`).

테스트 위치는 후속 구현에서 현재 core test 구조에 맞춰 정한다. `core/tests/CMakeLists.txt`에는 e2e SPOT
route bridge 테스트 target이 이미 있다(`core/tests/CMakeLists.txt:109`). actor gateway no-bind는 protocol
parser 단위 테스트와 routed delivery 통합 테스트를 나누어 추가한다.

## 버전과 릴리즈 영향

wire에 새 packet kind와 reply correlation 의미가 추가된다. 기존 layout은 유지하지만, 새 kind를 이해하지
못하는 이전 runtime과 섞으면 no-bind actor call은 실패한다. 기존 session-bound actor gateway traffic은
kind 값과 layout이 그대로라 하위 호환이다.

판정:

- 기존 기능 wire 호환: 유지.
- 새 기능 wire 호환: 양쪽 node가 새 core runtime이어야 한다.
- 버전: protocol 기능 추가이므로 `8.4.3`에서 `8.5.0`으로 minor를 올린다. 현재 값은 `VERSION`의
  `LIBZLINK_VERSION=8.4.3`과 `core/include/zlink.h`의 `ZLINK_VERSION_MINOR 4`,
  `ZLINK_VERSION_PATCH 3`에 있다(`VERSION:4`, `core/include/zlink.h:9`, `core/include/zlink.h:10`).

## 구현 후 정식 반영 위치

구현과 회귀 테스트가 끝난 뒤에는 다음 위치를 실제 header와 맞춰 갱신한다.

- `core/doc/spec/core/service/03-spot.ko.md`: 새 C API 계약, 오류 결과, ownership 규칙.
- `core/doc/spec/core/04-errno-map.ko.md`: 새 API가 반환하는 errno/result 매핑.
- bindings spec과 언어별 binding 문서: 새 `ZLINK_EXPORT` 함수가 들어갈 경우 전 언어 커버리지.

정식 문서에는 이 초안의 대안이나 구현 전 가정을 그대로 옮기지 않는다.
