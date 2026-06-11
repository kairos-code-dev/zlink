<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Channel Messaging Samples](../internals/channel-messaging-samples.ko.md) | [다음: Spec -- ZLink Framework C++ Interface Design](./cpp-framework-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](../internals/cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [channel 샘플](../internals/channel-messaging-samples.ko.md)

# Spec -- ZLink Framework C++ Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 channel messaging을 어떤 표면으로
> 드러낼지 정리한다.

## 인터페이스 경계

channel public contract는 `contracts/channels/*`가 소유한다. 사용자가 보는 타입은
channel builder, capability option, `message_bus_t`, `request_client_t`, `publisher_t`,
call object, reliability event다. `ROUTER`/`DEALER` socket owner, recv pump, reply
correlation table, send-ready queue, discovery watch state는 `src/runtime/channels/*`에
둔다.

channel 문서의 예시는 public contract 모양을 보여 주기 위한 것이다. 내부 dispatch 순서나
pending queue 구조를 사용자가 호출해야 하는 API처럼 해석하지 않는다.

## 1. Channel 의미

channel은 framework에서 request/reply와 pub/sub capability를 묶는 이름이다.
사용자는 raw socket을 직접 만들지 않고, channel 이름과 capability 설정으로 runtime을
구성한다.

channel 자체는 연결 주체가 아니다. 실제 endpoint, Discovery, timeout 같은 연결 설정은
아래 capability builder에 둔다.

- server
- client
- publisher
- subscriber

이렇게 나누면 같은 `orders` channel 안에서도 request/reply와 pub/sub 연결 정책을
분리할 수 있다.

## 2. Host bootstrap

```cpp
using namespace zlink::framework;

auto app = app_t::create();

app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry1:5551");
    options.use_discovery().add_registry_endpoint ("tcp://registry2:5551");
    options.add_client_server_channel("api")
      .enable_server("tcp://0.0.0.0:7100")
      .use_handler_group("api");
    options.add_client_server_channel("profile")
      .enable_client();
    options.add_client_server_channel("account")
      .enable_client();
});
```

수동 연결은 아래처럼 둔다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_client_server_channel("profile")
      .enable_client("tcp://10.0.10.15:7101")
      .enable_client("tcp://10.0.10.16:7101");
});
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.
같은 `profile` channel이라도 `profile.subscriber`는 별도 연결 집합으로 본다.
같은 capability 안에서는 수동 연결과 Discovery 연결을 섞지 않는다.
`client()`는 registry discovery로 peer를 찾는 선언이므로 같은 설정에
`options.use_discovery().add_registry_endpoint (...)`가 필요하다. discovery를 쓰지 않는 경우에는
`client(endpoint)`로 manual connection을 명시한다. `client(endpoint)`와 fanout
`subscriber(endpoint)`는 반복 호출할 수 있고, 호출 순서대로 manual endpoint를 capability
snapshot에 추가한다.

## 3. Handler 등록

handler는 `options.handlers()` 아래 typed registry로 등록한다. handler 타입은
`request_type`, `reply_type`, `topic_name`, `handle(...)`로 메시지 계약을 설명한다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_client_server_channel("api")
      .enable_server("tcp://0.0.0.0:7100")
      .use_handler_group("api");
    options.handlers()
      .add<get_user_handler_t>("api")
      .add<refresh_profile_cache_handler_t>("api");
});
```

기본 packet key는 payload 타입 이름에서 얻는다. 별도 이름이 필요하면
`handler_options_t::packet_name`이나 등록 인자를 사용한다.

일반 event publish는 `publisher_t::publish(channel, topic, event)` 표면으로 설명한다.
current Spot 밖에서 target Spot으로 직접 send/request 하는 public client는 channel
messaging 표면에 넣지 않는다. 그런 흐름은 actor 생성 또는 Entry Spot join으로 actor
handle을 얻은 뒤 ActorGateway/session actor 경로로 연결한다.

request/send 같은 outbound 호출은 call object를 반환하고, 마지막 `async()`에서
실행한다. 반환된 call object에서 `packet_name(...)`, `metadata(key, value)`,
`timeout(...)`을 설정할 수 있고, 이 값은 submit 시점에 framework envelope 정책으로
넘어간다. 서버 framework public API는 blocking 동기 호출을 제공하지 않으며,
handler에서는 `co_await call.async()`를 사용한다.
handler 안에서 blocking wait를 쓰지 않는다.

## 4. Dispatch 기준

- 일반 request/send dispatch는 local server capability ingress 기준이다.
- outbound client capability 수신은 pending request의 reply correlation 경로다.
- pending request correlation은 `channel_pending_requests_t`가 맡는다. request sequence와
  pending table은 public call object에 노출하지 않는다.
- server ingress envelope dispatch는 `channel_packet_dispatcher_t`가 맡는다. request는
  reply writer를 통해 response/error envelope로 변환하고 command/send는 reply 없이
  handler dispatch만 수행한다.
- channel capability runtime bundle은 `channel_runtime_bundle_t`가 맡는다. manual
  connection set, dealer-mesh pending request owner, receive gate는 한 capability의 내부
  상태로 묶고 public builder나 call object에 노출하지 않는다.
- channel capability 생성과 조회는 `channel_bundle_factory_t`와
  `channel_runtime_manager_t`가 맡는다. manager는 `.NET`처럼 client/publisher bundle을
  lazy creation으로 만들고 inbound, client, publisher, route channel 초기화를 runtime
  state 안에서 정리한다.
- server receive pump는 `channel_receive_loop_t`와 `channel_message_pump_t`가 맡는다.
  receive loop는 수신 queue를 drain하고 receive gate로 재진입을 막으며, message pump는
  packet dispatcher에 envelope dispatch를 위임한다. 이 구조는 `.NET`의
  `ZLinkChannelRuntimeBundle`, `ZLinkChannelReceiveLoop`,
  `ZLinkChannelMessagePump`와 같은 책임 분리다.
- route channel은 `route_channel_runtime_t`와 `route_connection_set_t`가 맡는다.
  route channel id, manual connection snapshot, target node/Spot routing id, outbound
  envelope parts, request sequence correlation을 runtime 내부에 둔다. public API는 route
  channel 이름과 typed send/request 표면만 드러내고 native router socket과 receive pump는
  노출하지 않는다.
- route channel handler 등록은 `route_channel_registration_t`와
  `route_channel_initializer_t`가 맡는다. `.NET`은 reflection scanner와 assembly marker로
  descriptor를 수집하지만, C++는 typed handler installer를 registration에 저장한 뒤
  initializer가 `route_handler_registry_t`로 변환한다. 프레임워크 사용자는
  `options.add_route_mesh_channel(name)`으로 bind, routing id, manual connection, handler group을
  설정한다. route mesh channel은 local route endpoint를 열기 위해 `bind(...)`가 필요하다.
  SPOT route ingress에서 `accept_routes_from_channel(name)`으로 참조할 수 있는 channel은
  client/server channel 또는 route mesh channel뿐이다. 참조한 node는 router capability와
  registry discovery 또는 accepted route manual endpoint를 가져야 한다.
  `zlink_builder_t::route_channel(name, configure)`와 `route_channel_builder_t`는 framework
  내부와 고급 확장용 낮은 수준 표면으로 남긴다.
- dealer mesh channel은 `options.add_dealer_mesh_channel(name)`으로 선언하되 `bind(...)`
  또는 `connect(...)` 중 하나 이상을 함께 둔다. peer 획득 경로가 없으면 framework
  options 적용 시점에 실패한다.
- client/server channel은 server 또는 client capability 중 하나 이상이 필요하고, fanout
  channel은 publisher 또는 subscriber capability 중 하나 이상이 필요하다. 아무 역할도 없는
  channel 선언은 framework options 적용 시점에 실패한다.
- route receive path는 `route_receive_pump_t`와 `route_packet_dispatcher_t`가 맡는다.
  route handler가 있으면 `route_handler_registry_t`와 `route_handler_invoker_t`를 통해
  typed payload를 호출하고, handler가 없으면 request에 `route_handler_not_found` error
  envelope를 반환한다. framework 내부 routed packet은
  `route_internal_packet_dispatcher_t`와 composite dispatcher가 먼저 처리한다.
- 같은 capability에서 Discovery와 manual 연결을 같이 섞지 않는다. endpoint 인자 없는
  `client()` 또는 `subscriber()`는 discovery mode를 뜻하고, endpoint를 받는 overload는
  manual endpoint를 추가한다.
- runtime 연결 제어가 필요하면 framework core의 capability 단위 connection manager가
  담당한다. 사용자는 raw socket이 아니라 channel capability 표면으로 연결을 다룬다.

## 5. Outbound-only host

local handler 없이 outbound client만 쓰는 host도 가능해야 한다.
이 경우 local server capability는 열지 않고 outbound client capability만 만든다.

## 6. 회귀 테스트

channel messaging 회귀 테스트는 `.NET` framework의 channel 동작과 같은 의미를 C++ 표면으로
고정한다. C++에서는 `co_await call.async()`가 완료 결과와 오류 의미를 보존해야 한다.

필수 항목:

- client/server request가 typed request DTO를 전달하고 typed reply DTO를 받는다.
- handler가 없으면 caller는 handler not found 계열 error를 받고 runtime은 계속 동작한다.
- request timeout은 `co_await call.async()`에서 같은 error kind로 돌아온다.
- payload decode failure와 reply serialization failure는 server log, monitoring event,
  caller failure result에 모두 반영된다. 특히 request envelope header를 읽은 뒤 body가
  없거나 payload를 읽을 수 없으면 dispatcher 자체 실패로 끝내지 않고 error envelope로
  caller에게 돌려준다.
- send/event는 reply 없이 handler dispatch만 수행하고, handler exception은 runtime을 죽이지
  않는다.
- pub/sub은 단일 subscriber, 여러 subscriber, topic mismatch, unsubscribe, subscriber
  disconnect를 모두 검증한다.
- slow subscriber나 disconnected subscriber가 publisher와 다른 subscriber를 같이 실패시키지
  않는다.
- pending request limit과 outbound queue limit을 넘으면 `request_rejected` 계열 결과를
  반환한다.
- manual connection과 Discovery connection을 같은 capability에 섞으면 startup validation이
  실패한다.
- route channel은 routing id 선택, routed request/reply, route handler not found, ambiguous
  route validation을 검증한다.
- shutdown 중 pending request는 drain 정책에 따라 완료되거나 shutdown error로 닫히며,
  shutdown 이후 새 submit은 실패한다.

CTest label은 `framework-zlink-channel`을 사용한다. 전체 zlink 회귀 묶음에서는
`framework-zlink` label에도 포함한다.
