<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ STREAM](./cpp-stream.ko.md) | [다음: Draft -- ZLink Framework C++ SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md) | [Monitoring](./cpp-monitoring.ko.md) | [Registry](./cpp-registry.ko.md)

# Draft -- ZLink Framework C++ Interface Alignment

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 기존 `C++` adapter interface catalog를 standalone
> framework 정책에 맞춰 정렬하기 위한 문서다.
> 전체 public interface 설계의 기준은
> [Framework 인터페이스](./cpp-framework-interfaces.ko.md)다.

## 1. 역할

이 문서는 예전 `C++` adapter 초안에서 사용하던 낮은 수준의 handler class와 client
surface를 새 framework 정책으로 옮길 때 지켜야 할 기준을 정리한다.

새 `C++` framework의 canonical 표면은 아래 문서에 둔다.

- [C++ 정책](./cpp-framework-policy.ko.md)
- [Framework 인터페이스](./cpp-framework-interfaces.ko.md)

따라서 이 문서는 별도의 API 계약을 추가하지 않는다. 기존 문서나 샘플에 남아 있던
낡은 이름을 어떤 새 표면으로 맞출지 설명하는 정렬 문서로만 사용한다.

## 2. 정렬 기준

기존 초안의 표면은 아래 기준으로 바꾼다.

| 기존 초안 표현 | 새 framework 기준 |
|----------------|-------------------|
| `app_t` builder chain 직접 조립 | `app_t::create()` 후 `services()`, `handlers()`, `use_zlink()`로 구성 |
| raw request/send/event handler class | `handler_registry_t`의 typed member function 등록 |
| channel client 직접 주입 | `message_bus_t`, `request_client_t`, `publisher_t` DI 주입 |
| event publisher 전용 타입 | `publisher_t::publish(channel, topic, event)` |
| channel 전체 연결 설정 | capability builder의 `bind`, `connect`, `use_discovery` |
| spot 전용 publisher client | `spot_context_t` 또는 `publisher_t`의 channel/topic 표면 |
| target Spot 직접 호출 public client | actor 생성 또는 Entry Spot join 뒤 actor/session handle 사용 |
| session actor relay용 route mesh channel | `stream.attach_actor_gateway(...)`와 `session_actor_t::relay(...)` |
| raw timer callback | `spot_context_t::add_timer(...)`와 `timer_tick_t` metadata |

handler owner는 service collection에 등록된 타입이어야 한다. 등록되지 않은 owner를
framework가 암묵적으로 생성하지 않는다. 이 규칙은 handler lifecycle과 shutdown 중
resolve 금지 같은 host 정책을 한곳에서 닫기 위해 필요하다.

## 3. Handler 등록 기준

일반 사용자는 raw `message_t` handler class를 상속하지 않고, typed payload와 member
function pointer를 등록한다.

```cpp
app.services()
  .add_transient<order_handler_t>();

app.handlers()
  .subscribe<order_created_t, order_handler_t>(
    "orders",
    "orders.created",
    &order_handler_t::on_created);

app.handlers()
  .request<get_order_status_t, order_status_reply_t, order_handler_t>(
    "orders",
    "orders.status",
    &order_handler_t::get_status);
```

raw payload가 필요한 경우에만 `send_raw(...)` 같은 고급 extension을 사용한다. STREAM은
framework core에서 Header 기반 packet 방식만 지원하므로 raw stream session은 공개
표면에 두지 않는다. 일반 샘플은 typed handler registry를 먼저 보여 준다.

request handler는 `TReply`를 바로 반환하거나 `task_t<TReply>`를 반환할 수 있다.
후자는 `.NET`의 `async Task<TReply>` handler와 같은 의미다. handler 안에서 다른
request/relay를 기다려야 하면 blocking wait를 쓰지 않고 `co_await call.submit()`을
사용한다.

CPU-bound 또는 blocking 가능성이 있는 handler는 framework core의 offload 실행 정책을
명시한다. 일반 handler 등록 표면은 그대로 유지하고, 실행 정책만 option으로 바꾼다.

```cpp
app.handlers()
  .request<match_request_t, match_reply_t, match_handler_t>(
    "match",
    "match.allocate",
    &match_handler_t::allocate,
    zlink::framework::handler_options_t{
      .execution = zlink::framework::handler_execution_t::offload,
    });
```

## 4. Messaging 주입 기준

handler나 service가 outbound messaging을 해야 하면 framework가 기본 등록한 messaging
service를 DI로 받는다.

```cpp
class order_service_t final {
public:
    explicit order_service_t(zlink::framework::request_client_t &client)
      : client_(client)
    {
    }

    zlink::framework::request_call_t<order_status_reply_t> get_status(
      order_status_query_t query)
    {
        return client_
          .request<order_status_reply_t>("orders", query)
          .timeout(std::chrono::seconds(2));
    }

private:
    zlink::framework::request_client_t &client_;
};
```

event publish는 channel name과 topic을 함께 받는다.

```cpp
publisher.publish(
  "orders",
  "orders.created",
  event,
  zlink::framework::send_options_t{
    .packet_name = "orders.created",
  });
```

## 5. Host 구성 기준

runtime 구성은 `use_zlink(...)` 하나로 들어간다. channel 연결 설정은 channel 전체가
아니라 capability builder에 둔다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("order-node")
      .discovery([](auto &discovery) {
          discovery.connect_registry("tcp://registry:5551");
      })
      .channel("orders", [](auto &channel) {
          channel.enable_server([](auto &server) {
              server.bind("tcp://0.0.0.0:7001");
          });
          channel.enable_client([](auto &client) {
              client.use_discovery();
          });
          channel.enable_publisher([](auto &publisher) {
              publisher.bind("tcp://0.0.0.0:7002");
          });
          channel.enable_subscriber([](auto &subscriber) {
              subscriber.use_discovery();
          });
      });
});
```

수동 연결은 capability 안에서 endpoint 기준으로 설정한다. 같은 capability 안에서
수동 연결과 Discovery 연결을 섞지 않는다.

## 6. SPOT 기준

`SPOT`은 binding의 `zlink::service::spot_node_t`와 `zlink::service::spot_t`를
framework builder와 `spot_context_t`로 감싸서 제공한다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("stage-node")
      .channel("game.stage", [](auto &channel) {
          channel.enable_publisher();
          channel.enable_subscriber([](auto &subscriber) {
              subscriber.use_discovery();
          });
      })
      .spot_node("stage-spot-node", [](auto &spot_node) {
          spot_node.bind("tcp://0.0.0.0:9000");
          spot_node.enable_actor_gateway();
          spot_node.use_discovery("game.stage");
          spot_node.attach_channel_client("profile");
          spot_node.attach_publisher("game.stage");
          spot_node.add_entry_spot<player_entry_spot_t>();
          spot_node.add_actor_factory<player_actor_factory_t>("player");
          spot_node.add_spot<stage_spot_t>("stage");
      });
});
```

직접 `routing_id_t`를 받는 API는 spot-to-spot send/request와 Entry Spot join 경로에
제한한다. 일반 application handler와 publisher는 channel name과 topic을 먼저 사용한다.
current Spot 밖에서 target Spot을 직접 호출하는 별도 public client는 기본 표면에 두지
않는다.

SPOT timer는 CAPI timer 등록을 감싼 framework timer handle과 `timer_tick_t` metadata로
설명한다. user Spot timer는 core SPOT dispatch boundary에서 순서 정책을 따르고,
Entry Spot timer는 Entry Spot 전체를 전역 직렬화하지 않는다.

Session actor relay는 application route mesh channel을 쓰지 않는다. STREAM session은
`attach_actor_gateway(...)`로 local SpotNode에 붙고, packet relay는
`session_actor_t::relay(...)`로 표현한다.

## 7. 중요한 규칙

- `C++` framework 문서는 공통 framework 정책과 C++ binding public spec을 함께 따른다.
- 구현 전 설계는 이 디렉토리의 draft 문서에만 둔다.
- public surface는 native socket, poller, callback userdata를 직접 노출하지 않는다.
- 같은 capability는 자동 연결과 수동 연결 중 하나만 선택한다.
- 일반 channel messaging의 handler dispatch는 local server capability ingress 기준이다.
- outbound client capability의 receive path는 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
- session actor relay는 ActorGateway attach와 logical actor handle 기준으로 설명한다.
- Registry는 Spot remote address 조회 기본값으로 쓰고 actor-session binding 저장소로
  쓰지 않는다.
