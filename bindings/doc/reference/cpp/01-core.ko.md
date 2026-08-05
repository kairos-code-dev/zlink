한국어 | [English](01-core.en.md)

[레퍼런스 목차](README.ko.md)

# 01. Core

이 category는 context 수명주기, context option, routing identity, 그리고 자유
utility/capability 함수를 다룬다. socket 생성은 여기 factory 메서드가 아니라 각 구체
socket type 자신의 생성자로 이뤄진다(Sockets category) — dotnet의
`IContext.CreateXxx()` 메서드와 다르다. 정확한 signature는
[`Contracts/Core/`](../../../../bindings/cpp/include/zlink/Contracts/Core/)가 소유한다.

---

## `context_t`

메시징 context — socket의 factory이자 소유자이며, 어떤 socket type을 생성하든
전제조건이다.

```cpp
zlink::context_t ctx;
zlink::context_t ctx_with_threads (zlink::io_thread_count_t::value (4));
```

**Options.** `context_t()`(기본), `explicit context_t(io_thread_count_t io_threads_)`.
이동 생성·대입 가능; 복사는 delete. `valid()`(`bool`). `shutdown()`(이 context 하위
socket의 blocking operation을 닫지 않고 인터럽트). `term()`(context를 종료하고
파괴). `options()`는 `context_options_t`(아래)를 반환. `recalculate_auto_hwm()`.

**Completion result.** `valid()`/`options()`를 제외한 모든 member는 반환값 없이
동기다. 소멸자는 아직 종료되지 않았으면 `term()`을 호출한다.

**선택 기준.** application이 필요로 하는 context마다 `context_t` 하나를 생성한다 —
대부분의 application은 정확히 하나가 필요하다. 여러 스레드에서 socket을 쓰는 중에
소멸시키기 전엔 `shutdown()`을 호출해 socket 호출을 기다리는 스레드를 풀어준다.
모든 socket은 살아있는 `context_t`의 참조로 생성돼야 한다.

---

## `context_options_t`

`ctx.options()`로 도달하는 typed option facade.

```cpp
ctx.options ().io_threads (zlink::io_thread_count_t::value (8));
ctx.options ().auto_hwm_profile (zlink::auto_hwm_profile::low_latency);
ctx.options ().add_thread_affinity (zlink::cpu_index_t::value (2));
```

**Options.** 짝을 이루는 getter/setter 메서드(getter는 접미사 없음, setter는 새
값을 받음): `io_threads()`(`io_thread_count_t`), `max_sockets()`(`socket_count_t`),
`max_msg_size()`(`byte_size_t`), `thread_priority()`
(`std::optional<thread_priority_t>`), `thread_scheduling_policy()`
(`thread_scheduling_policy_t`), `thread_name_prefix()`(`std::string`),
`blocky()`(`bool`), `auto_hwm_enabled()`(`bool`), `auto_hwm_recalc_debounce()`
(`std::chrono::milliseconds`), `auto_hwm_profile()`(`zlink::auto_hwm_profile`),
`auto_hwm_msg_unit_bytes()`(`byte_count_t`). 읽기 전용: `socket_limit()`
(`socket_count_t`), `msg_t_size()`(`byte_size_t`). setter만: `add_thread_affinity
(cpu_index_t)`/`remove_thread_affinity(cpu_index_t)`.

**Completion result.** 모든 getter/setter는 동기다.

**선택 기준.** 기본값이 배포 환경에 맞지 않을 때 socket 생성 전에 조정한다.
`auto_hwm_profile`/`auto_hwm_enabled` 변경은 `context_t::recalculate_auto_hwm()`과
짝지어 즉시 적용한다.

---

## Strongly-typed option 값 wrapper

raw `int`/`uint32_t` 대신 `context_options_t`와 socket option 전반에서 쓰이는 작은
value-type wrapper들로, 각각 static `value(...)` factory로 생성한다.

**Options.** `io_thread_count_t`, `socket_count_t`, `worker_count_t`,
`thread_priority_t`, `cpu_index_t`, `socket_backlog_t`는 `::value(int)`/`.value()`로
`int`를 감싼다. `byte_size_t`는 `::bytes(int64_t)`/`.bytes()`로 `int64_t`를 감싼다.
`byte_count_t`(Core)는 `::bytes(uint64_t)`/`.bytes()`로 `uint64_t`를 감싼다 — HWM과
byte-budget option이 쓰는 무손실 byte count다. `peer_weight_t::value(uint32_t)`는
0-100 범위를 벗어나면 `std::invalid_argument`를 던진다.

**Completion result.** `peer_weight_t::value`를 제외한 모든 factory·accessor는
`noexcept`다 — 이건 범위를 검증한다.

**선택 기준.** 맨 정수를 넘기는 대신 호출 지점에서 이런 wrapper를 생성한다
(`io_thread_count_t::value(4)`) — wrapper는 단위가 어긋나면 컴파일이 안 되게 하려고
존재한다.

---

## `routing_id_t`

메시징 peer나 route를 식별하는 1~255바이트의 binary-safe value type.

```cpp
auto from_string = zlink::routing_id_t::from (std::string ("worker-3"));
auto from_bytes = zlink::routing_id_t::from (raw_bytes);
auto from_uint = zlink::routing_id_t::from (uint32_t{42});
auto restored = zlink::routing_id_t::from_hex (previously_printed.to_hex ());
```

**Options.** 생성자 `routing_id_t(const uint8_t *bytes_, size_t size_)`. Static
factory: `from(const uint8_t*, size_t)`, `from(const std::vector<uint8_t>&)`,
`from(const std::string&)`(raw byte, UTF-8 검증 없음), `from(uint32_t)`(4-byte
big-endian), `from(const std::array<uint8_t, 16>&)`(16-byte 값, 예: GUID의 raw
byte), `from_hex(const std::string&)`. Instance member: `data()`/`size()`,
`to_bytes()`, `to_string()`(printable UTF-8, 그 다음 4-byte를 uint32로, 그 다음
16-byte를 GUID 포맷으로, 마지막 `hex:` 접두 fallback), `to_hex()`, 값 동등성
(`operator==`/`!=`), unordered container용 `std::hash<routing_id_t>` 특수화.

**Completion result.** 모든 factory·accessor는 동기다. 빈 입력, 255바이트 초과,
크기는 0이 아닌데 null pointer면 `std::invalid_argument`를 던진다. `from_hex`에
잘못된 hex 문자열을 주면 마찬가지다.

**선택 기준.** 사람이 부여한 identity엔 `from(const std::string&)`를, 숫자나
GUID 형태 identity엔 `from(uint32_t)`/16-byte 배열 overload를, identity가 이미
binary면 raw byte overload를 쓴다. 내구성 있는 raw-byte round trip 전용으로
`to_hex()`/`from_hex()`를 쓴다 — `to_string()`은 표시 전용이며 가역성이 보장되지
않는다.

---

## `zlink::version` / `zlink::error_text` / `zlink::has`

native library의 빌드 버전을 읽거나, native error code를 메시지로 변환하거나,
선택적 빌드 역할을 확인한다.

```cpp
int major, minor, patch;
zlink::version (major, minor, patch);
const char *message = zlink::error_text (errnum);
bool has_tls = zlink::has ("tls");
```

**Options.** `version(int &major_, int &minor_, int &patch_)`(세 개의 출력
참조). `error_text(int errnum_) noexcept`(`const char*` 반환; caller가 수정·해제하면
안 됨). `has(const std::string &capability_)` — 인식하는 이름은 `"tcp"`, `"ipc"`,
`"tls"`, `"ws"`, `"wss"`; 인식하지 못하는 이름은 `false`를 반환한다.

**Completion result.** 셋 다 동기이며 예외를 던지지 않는다.

**선택 기준.** 특히 동적으로 로드할 때, 링크된 native library 버전이 application이
기대하는 버전과 일치하는지 확인하려면 `version()`을 쓴다. 모든 transport가
컴파일에 포함됐다고 가정하는 대신 기동 시점에 `has(...)`로 분기한다.

---

## `stopwatch_t` / `atomic_counter_t` / `thread_t`

고해상도 stopwatch, thread-safe 정수 counter, 실행 중인 background thread — 같은
RAII 형태를 가진 세 개의 독립된 utility resource.

```cpp
zlink::stopwatch_t watch;
uint64_t partial_us = watch.intermediate ();
uint64_t total_us = watch.stop ();

zlink::atomic_counter_t counter;
int new_value = counter.increment ();

zlink::thread_t worker ([] { do_work (); });
worker.join ();
```

**Options.** 셋 다: 기본 생성 가능, move-only, `valid() const noexcept`, `close()`.
`stopwatch_t`는 `intermediate()`/`stop()`을 더한다(둘 다 `uint64_t` 마이크로초).
`atomic_counter_t`는 `set(int)`, `increment()`/`decrement()`(*새* 값을 반환),
`value() const`를 더한다. `thread_t(std::function<void()> task_)`는 생성 즉시
task를 실행한다. `join()`(task가 끝날 때까지 block)을 더한다.

**Completion result.** 모든 member는 동기다. 소멸자는 아직 닫히지 않았으면
`close()`를 호출한다.

**선택 기준.** 스레드 전체에서 안전한 공유 count엔 `atomic_counter_t`를 쓴다.
벤치마킹엔 `stopwatch_t`를 쓴다 — `intermediate()`는 몇 번이든 호출하고, `stop()`은
정확히 한 번 호출한다. 플랫폼 특정 API 대신 이식 가능한 background thread엔
`thread_t`를 쓴다.

---

[`Contracts/Core/`](../../../../bindings/cpp/include/zlink/Contracts/Core/)와
[C++ 바인딩 스펙](../../spec/cpp/README.ko.md)에서 전체 근거를 확인한다.
