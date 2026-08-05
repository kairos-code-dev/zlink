한국어 | [English](02-messaging.en.md)

[레퍼런스 목차](README.ko.md)

# 02. Messaging

이 category는 message 소유권, receive envelope 타입(`received_t`, `topic_message_t`,
`subscription_event_t`), 그리고 모든 socket type의
`send`/`publish`/`request`/`reply`가 반환하는 공유 send/request/reply move-only
builder family를 다룬다. 정확한 signature는
[`Contracts/Messaging/`](../../../../bindings/cpp/include/zlink/Contracts/Messaging/)가
소유한다. `lazy_message_parts.hpp`와 `operation_builder_base.hpp`는
`namespace zlink::detail`에 있어 public contract 항목이 없다.

---

## `message_t`

zlink message payload 하나를 소유한다 — 모든 send·request·reply·receive API가 옮기는
단위다.

```cpp
zlink::message_t empty;
zlink::message_t sized (4096);
zlink::message_t copy = zlink::message_t::from (std::string ("payload"));
```

**Options.** 생성자: `message_t()`(빈 메시지), `explicit message_t(size_t size_)`
(쓰기 가능 storage). Static factory: `allocate(size_t)`, `from(const
std::vector<uint8_t>&)`, `from(std::span<const std::byte>)`, `from(std::span<const
uint8_t>)`, `from(const std::string&)`(UTF-8 인코딩). JSON/MessagePack/protobuf용
template factory·parser도 있다(`from_json`/`from_messagepack`/`from_protobuf`,
`parse_json`/`parse_messagepack`/`parse_protobuf`) — codec helper 자체는 C++
binding 패키지의 일부가 아니다. 이들은 framework codec extension에 위임한다.
Instance member: `data()`/`bytes()`(mutable·`const` overload), `size()`,
`is_empty()`, `ref_count()`, `to_bytes()`, `copy_to(std::span<std::byte>)`/
`copy_to(std::span<uint8_t>)`, `to_string()`, `close()`. 복사 생성·대입은 payload를
깊은 복사한다.

**Completion result.** 모든 member는 동기다. 메시지를 보내면 payload가 소비된다 —
native frame이 성공적인 send에서 transport로 옮겨지고 instance는 invalid 상태로
남는다. 보내지 않을 메시지를 해제하려면 `close()`를 호출한다. `data()`/`bytes()`가
반환하는 pointer/span은 메시지가 valid한 동안만(전송·close되지 않은 동안만)
유효하다.

**선택 기준.** caller가 raw 소유권을 유지할 필요가 없는 데이터로 outbound
payload를 만들 땐 크기 지정 생성자나 복사하는 `from(...)` factory를 쓴다.
`zlink::advanced::external_message_t::from(span, free_fn, hint)`(`message_t` 옆에
선언된 advanced no-copy overload)는 caller 소유 buffer를 복사 없이 메시지에 넘겨야
할 때만 쓴다 — buffer를 메시지에 위탁하며, 메시지가 해제될 때 `free_fn(data,
hint)`를 정확히 한 번 호출한다.

---

## `received_t`

수신된 message envelope 하나 — routing 메타데이터, part, 선택적 reply context를
담는다.

```cpp
zlink::received_t received;
if (dealer.recv (received) == 0) { /* ... */ }
if (received.request_seq ()) {
    received.reply ().message (reply_msg).submit ();
}
```

**Options.** 기본 생성 가능; 복사·이동 가능. 읽기 전용 accessor: `routing_id()`
(`const std::optional<routing_id_t>&`), `request_seq()`(`const
std::optional<uint64_t>&`), `parts()`(`const std::vector<message_t>&`/mutable
overload), `is_single_part()`. Method: `first_part()`, `single_part_or_throw()`,
`send()`(이 envelope이 포착한 routing id로 향하는 공유 `send_operation_t` builder
시작), `reply()`(공유 `reply_operation_t` builder 시작 — 유효한 reply context가
없으면 `submit()`에서 예외), `close()`.

**Completion result.** 모든 member는 동기다. `send()`/`reply()`는 저장된 routing
id와 request sequence로부터 submit 시점에 지연 생성되는 send/reply context를
재구성한다 — server hot path에서 receive마다 `std::function` closure와 그
heap 할당을 피한다.

**선택 기준.** message마다 새로 생성하는 대신 receive loop 전체에서 `received_t`
하나를 재사용한다. `reply()`를 호출하기 전에 `request_seq()`로 envelope이 실제로
reply 가능한지 확인한다.

---

## `topic_message_t`

수신된 publish 하나 — topic과 message part를 담는다.

```cpp
zlink::topic_message_t published;
if (sub.subscribe (published) == 0) {
    const std::string &topic = published.topic ();
}
```

**Options.** 기본 생성 가능, 그리고 routing id/topic/parts를 직접 받는 생성자.
읽기 전용 accessor: `routing_id()`(`const std::optional<routing_id_t>&`),
`topic()`(`const std::string&`), `parts()`, `is_single_part()`, `first_part()`,
`single_part_or_throw()`, `close()`.

**Completion result.** 동기다.

**선택 기준.** `received_t`와 같은 방식으로 subscribe-receive loop 전체에서
instance 하나를 재사용한다.

---

## `subscription_event_t` / `subscription_filter_t`

XPUB socket이 관찰한 구독자 한 명의 subscribe·unsubscribe를 보고하고, 활성 구독
항목 하나를 기술한다.

```cpp
zlink::subscription_event_t evt;
if (xpub.receive_subscription_event (evt) == 0) { /* ... */ }
```

**Options.** `subscription_event_t`는 순수 struct다: `routing_id`
(`std::optional<routing_id_t>`), `topic`(`std::string`), `subscribed`(`bool`).
`subscription_filter_t`: `filter`(`std::string`), `is_pattern`(`bool`, 기본값
`false`).

**Completion result.** 둘 다 dispose나 async 동작이 없는 순수 데이터 struct다.

**선택 기준.** XPUB socket의 subscription-event receive 경로(Sockets category)에서
구독자 변동을 관찰할 때 쓴다. socket의 `subscription_at(index)` 값 반환 overload의
반환 타입으로 `subscription_filter_t`를 쓴다.

---

## Send / request / reply operation-builder 형태

모든 `send`, routed `send`, `publish`, `request`, `reply` 진입점(전부 Sockets
category)이 part·flag·terminal submit을 누적하기 위해 반환하는 move-only fluent
builder. 이 family의 모든 builder는 move-only이며 공유
`detail::operation_builder_base_t`를 private 상속한다 — 그 자체는 public
contract가 아니다.

```cpp
std::move (dealer.send ()).message (part1).message (part2).submit ();

auto result = std::move (dealer.request ())
    .message (request_msg)
    .timeout (std::chrono::seconds (5))
    .async ();
std::vector<zlink::message_t> reply = result.get ();

std::move (received.reply ()).message (reply_msg).submit ();
```

**Options.** `send_operation_t::message(message_t&)`/`message(message_t&&)`(`&&`
ref-qualified이라 각 호출마다 builder가 소비된다 — `std::move(...)`로 chain)가
chain을 시작해 `send_submit_operation_t`를 반환하고, 그 `.message(...)`/
`.flags(int)`/`.submit()`이 part를 더 추가하고, flag를 설정하고, 종료한다.
`request_operation_t`/`request_submit_operation_t`는 같은 형태에
`.timeout(std::chrono::milliseconds)`를 더한 것이다. `request_submit_operation_t`
에서 `.flags(int)`를 호출하면 builder가 `request_callback_submit_operation_t`로
좁혀지고, awaitable `.async()` 경로가 사라진다 — 그 지점 이후엔
`.submit(request_callback_t)`만 도달 가능하다. `reply_operation_t`/
`reply_submit_operation_t`는 `send_operation_t`/`send_submit_operation_t`와 같은
형태지만 `.flags(...)` 호출은 `send_flags_t::none` 외의 값을 주면
`submit_error_t{not_supported}`를 던진다 — core reply 함수가 send-flag 인자를
받지 않기 때문이다.

**Completion result.** `send_submit_operation_t::submit()`/
`reply_submit_operation_t::submit()`은 동기다 — send의 반환값은 `bool`
(`send_flags_t::dontwait`가 설정되고 send가 block됐을 때만 `false` — 그 외 모든
실패는 `submit_error_t`를 던진다), reply의 반환값은 `void`다.
`request_submit_operation_t::async()`는 `async_result_t<std::vector<message_t>>`를
반환한다 — reply를 기다리려면(caller가 소유) `.get()`을, timeout과 함께
poll하려면 `.wait_for(...)`/`.wait_until(...)`을 호출한다 — 이 결과 타입은 OS
스레드를 그대로 block하는 대신 대기하는 동안 내부적으로 request progress를
pump한다. `request_submit_operation_t`/
`request_callback_submit_operation_t.submit(request_callback_t)`는 `bool`을
반환하고(같은 `dontwait` 관례) `(request_result_t, std::vector<message_t>)`를
나중에 전달한다 — 결과가 `request_result_t::ok`일 때만 벡터가 채워지며, 콜백이
각 메시지를 소유하고 반드시 `close()`해야 한다. 모든 builder는 성공적인
submit에서만 누적된 `message_t` part를 소비한다 — 실패 시 소유권은 caller에게
복원된다.

**선택 기준.** caller가 future/`async_result_t`를 기다릴 수 있을 땐 `.async()`를,
대신 callback 기반 완료가 필요할 땐 `.flags(...).submit(callback)`을 쓴다.
목적지를 손으로 재구성하는 대신 `received_t::reply()`/`send()`를 쓴다.
`message()` overload가 `&&`-qualified이므로 항상 rvalue에서 chain한다(위에 보인
`std::move(socket.send())...` 또는 직접 chain) — lvalue builder는 `.message(...)`를
직접 호출할 수 없다.

---

[`Contracts/Messaging/`](../../../../bindings/cpp/include/zlink/Contracts/Messaging/)와
[C++ 바인딩 스펙](../../spec/cpp/README.ko.md)에서 전체 근거를 확인한다.
