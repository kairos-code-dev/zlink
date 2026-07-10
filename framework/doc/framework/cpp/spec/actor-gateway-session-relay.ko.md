<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ SPOT](cpp-spot.ko.md) | [다음: Spec -- ZLink Framework C++ STREAM](cpp-stream.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [Runtime Architecture](../internals/runtime-architecture.ko.md) | [Framework 인터페이스](cpp-framework-interfaces.ko.md) | [SPOT](cpp-spot.ko.md) | [STREAM](cpp-stream.ko.md) | [Registry](cpp-registry.ko.md)

# Spec -- ZLink Framework C++ ActorGateway Session Relay

> 이 문서는 **구현 완료된 설계 계약**이다.
> `.NET` framework의 ActorGateway session relay와 같은
> 개념을 `C++` standalone framework에서 어떻게 자체 host/runtime 기능으로 만들지
> 정리한다.

## 인터페이스 경계

ActorGateway session relay의 public contract는 `contracts/streams/*`와
`contracts/actors/*`에 나누어 둔다. session, actor reference, bound session, actor
factory, actor context는 public contract가 될 수 있다. actor mailbox, join coordinator,
relay packet dispatcher, stream-to-spot binding table, session actor lifecycle state는
`src/runtime/actors/*`와 `src/runtime/streams/*`에 둔다.

이 문서의 relay 흐름은 사용자가 구현할 session/actor contract를 설명하기 위한 것이다.
ActorGateway frame codec이나 내부 relay packet 종류를 public API로 노출한다는 뜻이 아니다.

## 1. 방향

`ZLink Framework for C++`는 `ASP.NET Core`처럼 application host, DI, HTTP hosting,
handler dispatch, zlink channel runtime을 한 곳에서 제공하는 framework가 된다. 따라서
session server, actor host, DI, handler dispatch, session relay도 이 host 모델
안에서 직접 제공해야 한다.

핵심 결정은 다음과 같다.

- session에서 actor로 보내는 packet은 application route mesh channel로 보내지 않는다.
- session은 `actor_ref_t` 또는 logical actor handle을 bind하고, 이후 packet은
  `session_actor_t::relay(...)`로 넘긴다.
- actor에서 bound client로 push할 때는 `bound_session_t`를 사용한다.
- Registry는 Spot remote address 조회에는 쓰지만, session actor binding을 저장하는
  actor route store로 쓰지 않는다.

## 2. Host 구성

Session 서버는 STREAM endpoint와 relay 대상 SpotNode를 함께 구성한다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_spot_mesh("session-actors")
      .bind("tcp://0.0.0.0:7101")
      .add_entry_spot<player_entry_spot_t>();
    options.add_stream_node("client-stream")
      .bind("tcp://0.0.0.0:9200")
      .register_session<client_session_t>()
});
```

Play 서버도 actor를 호스팅하는 SpotNode를 routed-capable로 구성한다. application
route mesh channel이 따로 필요한 경우에도 그 channel은 일반 SPOT egress 용도다.
session actor relay 설정으로 해석하지 않는다.

## 3. Session 표면

session callback은 인증 뒤 actor를 bind하고, 이후 packet을 actor로 relay한다.

```cpp
class game_session_t final : public zlink::framework::packet_stream_session_t {
public:
    explicit game_session_t(zlink::framework::session_actor_manager_t &actors)
      : actors_(actors)
    {
    }

    zlink::framework::task_t<void> on_packet(zlink::framework::stream_t &stream,
      const zlink::framework::stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload) override
    {
        if (is_login(dispatch)) {
            actor_ = co_await actors_
              .bind(find_or_create_actor_ref(payload))
              .async();
            co_return;
        }

        co_await actor_
          .relay(payload)
          .async();
    }

private:
    zlink::framework::session_actor_manager_t &actors_;
    zlink::framework::session_actor_t actor_;
};
```

`payload`는 `zlink::message_t`다. session은 이 값을 decode 하거나 `relay(...)` 같은
framework API에 그대로 넘긴다. header 값은 runtime의 현재 dispatch state가 보존하므로
application이 header 객체를 다시 넘기지 않는다.

## 4. Actor 표면

actor는 ID와 type으로 식별한다. actor factory는 DI에 등록하고, SpotNode가 그 factory를
actor type 이름으로 매핑한다.

```cpp
class player_actor_t final {
public:
    player_actor_t(std::string actor_id,
      zlink::framework::actor_context_t &context);

    std::string_view actor_id() const;
    zlink::framework::actor_context_t &context();
};

class player_actor_factory_t final {
public:
    std::unique_ptr<player_actor_t> create(std::string actor_id);
};
```

actor handler는 actor 클래스가 아니라 Entry Spot 또는 user Spot 등록 표면에 붙인다.
Entry Spot actor packet은 core actor ordering을 따르고, user Spot actor packet은 core
SPOT dispatch boundary에서 처리한다.

## 5. Bound Session

actor가 자기 client로 push할 때는 `bound_session_t`를 사용한다.

```cpp
class player_actor_t final {
public:
    void notify_turn_changed(turn_changed_t event)
    {
        context_.bound_session().send(event).submit(); // client push는 대기하지 않는다.
    }

private:
    zlink::framework::actor_context_t &context_;
};
```

`bound_session_t`는 server-to-client request API를 기본 제공하지 않는다. client request
에 대한 응답은 actor request handler의 반환값과 원래 request correlation으로 처리한다.
actor가 client 연결을 끊어야 할 때는 `bound_session_t::disconnect().submit()`을 사용한다.
이 호출은 session binding만 닫고 actor의 현재 Spot 소속은 바꾸지 않는다.

## 6. Runtime Mapping

| 영역 | C++ framework 책임 |
|------|--------------------|
| stream initialization | STREAM은 별도 attach 호출 없이 bind한다. framework는 등록된 SpotNode와 route channel 설정을 보고 relay bridge를 자동 구성한다 |
| session bind | local actor handle 또는 remote actor ref를 backend stream binding으로 넘긴다 |
| session relay | route mesh packet을 만들지 않고 ActorGateway bound actor send 경로를 사용한다 |
| actor push | `bound_session_t`가 ActorGateway bound session send wrapper로 내려간다 |
| actor disconnect | bound session close는 stream close로 이어지되 actor current Spot을 바꾸지 않는다 |
| execution | framework handler 등록 표면을 통해 application handler를 호출한다 |
| cleanup | stream close는 session binding cleanup만 수행한다 |

## 7. 결정된 세부 정책

`.NET` framework와 같은 사용성으로 맞추기 위해 아래 정책을 기준으로 둔다.

| 항목 | 결정 |
|------|------|
| `actor_ref_t` public 형태 | node routing id, actor id, generation을 담는 C++ 값 타입으로 둔다. native 내부 ref를 그대로 노출하지 않는다 |
| session class 생성 방식 | `packet_stream_session_t` 구현체는 DI에서 resolve한다. handler registry callback은 낮은 수준 확장 표면으로만 둔다 |
| remote ActorGateway locator codec | wire metadata는 runtime 내부 frame으로 숨기고, application에는 `actor_ref_t`와 `session_actor_t`만 보인다 |
| actor factory duplicate 정책 | 같은 actor id 중복은 `actor_already_exists`, actor id/type 불일치는 `actor_type_mismatch`로 보고한다 |

## 8. 회귀 테스트

ActorGateway 회귀 테스트는 `.NET` framework의 session relay와 같은 기능 기대값을 C++
framework에서 고정한다. 특히 session actor relay가 application route mesh channel로
우회되지 않는지 확인해야 한다.

필수 항목:

  수행한다.
- `actor_ref_t`의 `node_rid`, `actor_id`, `generation`은 bind, relay, push round-trip에서
  유지된다.
- local actor relay request/reply와 remote actor relay request/reply가 같은 public
  `session_actor_t::relay(...)` 표면으로 동작한다.
- actor에서 client로 push할 때 `bound_session_t`를 사용하고, disconnected session에는
  disconnected 계열 error를 반환한다.
- duplicate actor는 `actor_already_exists`, actor type mismatch는 `actor_type_mismatch`,
  missing actor는 actor not found 계열 error로 보고한다.
- relay timeout은 caller result, monitoring event, server-side file log에 같은 correlation
  id로 남는다.
- session disconnect cleanup 후 bound session push와 relay는 실패해야 한다.
- Registry는 Spot remote address 조회에는 쓰지만 session actor binding store로 쓰지 않는다.
- TicTacToe e2e는 HTTP `POST /games`, Play channel request, STREAM connector connect,
  ActorGateway bind, player move relay, game ended push를 server/client log로 검증한다.

CTest label은 `framework-zlink-actor-gateway`를 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ SPOT](cpp-spot.ko.md) | [다음: Spec -- ZLink Framework C++ STREAM](cpp-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
