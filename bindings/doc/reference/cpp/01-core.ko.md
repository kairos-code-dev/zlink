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

**옵션.**

| Member | 의미 |
| --- | --- |
| `context_t()` / `explicit context_t(io_thread_count_t)` | 기본 / I/O thread count 지정 |
| 이동 생성·대입 가능; 복사는 delete | — |
| `valid()` | `bool` |
| `shutdown()` | 이 context 하위 socket의 blocking operation을 닫지 않고 인터럽트 |
| `term()` | context를 종료하고 파괴 |
| `options()` | `context_options_t`(아래) 반환 |
| `recalculate_auto_hwm()` | — |

**완료 결과.** `valid()`/`options()`를 제외한 모든 member는 반환값 없이 동기다.
소멸자는 아직 종료되지 않았으면 `term()`을 호출한다.

**선택 기준.** application이 필요로 하는 context마다 `context_t` 하나를 생성한다 —
대부분은 정확히 하나가 필요하다. 여러 스레드에서 socket을 쓰는 중에 소멸시키기
전엔 `shutdown()`을 호출한다.

---

## `context_options_t`

`ctx.options()`로 도달하는 typed option facade.

```cpp
ctx.options ().io_threads (zlink::io_thread_count_t::value (8));
ctx.options ().auto_hwm_profile (zlink::auto_hwm_profile::low_latency);
ctx.options ().add_thread_affinity (zlink::cpu_index_t::value (2));
```

**옵션.** 짝을 이루는 getter/setter 메서드(getter는 접미사 없음, setter는 새 값을
받음):

| Member | 타입 | 의미 |
| --- | --- | --- |
| `io_threads()` | `io_thread_count_t` | — |
| `max_sockets()` | `socket_count_t` | — |
| `max_msg_size()` | `byte_size_t` | — |
| `thread_priority()` | `std::optional<thread_priority_t>` | — |
| `thread_scheduling_policy()` | `thread_scheduling_policy_t` | — |
| `thread_name_prefix()` | `std::string` | — |
| `blocky()` | `bool` | — |
| `auto_hwm_enabled()` | `bool` | — |
| `auto_hwm_recalc_debounce()` | `std::chrono::milliseconds` | — |
| `auto_hwm_profile()` | `zlink::auto_hwm_profile` | — |
| `auto_hwm_msg_unit_bytes()` | `byte_count_t` | — |
| `socket_limit()` | `socket_count_t` | 읽기 전용 |
| `msg_t_size()` | `byte_size_t` | 읽기 전용 |
| `add_thread_affinity(cpu_index_t)` / `remove_thread_affinity(cpu_index_t)` | — | setter만 |

**완료 결과.** 모든 getter/setter는 동기다.

**선택 기준.** 기본값이 배포 환경에 맞지 않을 때 socket 생성 전에 조정한다.
`auto_hwm_profile`/`auto_hwm_enabled` 변경은 `context_t::recalculate_auto_hwm()`과
짝지어 즉시 적용한다.

---

## Strongly-typed option 값 wrapper

raw `int`/`uint32_t` 대신 `context_options_t`와 socket option 전반에서 쓰이는 작은
value-type wrapper들로, 각각 static `value(...)` factory로 생성한다.

**옵션.**

| 타입 | 감싸는 것 | 의미 |
| --- | --- | --- |
| `io_thread_count_t`, `socket_count_t`, `worker_count_t`, `thread_priority_t`, `cpu_index_t`, `socket_backlog_t` | `int`(`::value(int)`/`.value()`) | — |
| `byte_size_t` | `int64_t`(`::bytes(int64_t)`/`.bytes()`) | — |
| `byte_count_t`(Core) | `uint64_t`(`::bytes(uint64_t)`/`.bytes()`) | HWM과 byte-budget option이 쓰는 무손실 byte count |
| `peer_weight_t` | `uint32_t`(`::value(uint32_t)`) | 0-100 범위 밖이면 `std::invalid_argument` |

**완료 결과.** `peer_weight_t::value`를 제외한 모든 factory·accessor는
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

**옵션.**

| Member | 의미 |
| --- | --- |
| `routing_id_t(const uint8_t *bytes_, size_t size_)` | 생성자 |
| `from(const uint8_t*, size_t)` / `from(const std::vector<uint8_t>&)` | raw byte 그대로 |
| `from(const std::string&)` | raw byte, UTF-8 검증 없음 |
| `from(uint32_t)` | 4-byte big-endian |
| `from(const std::array<uint8_t, 16>&)` | 16-byte 값, 예: GUID의 raw byte |
| `from_hex(const std::string&)` | `to_hex()`가 출력한 바이트 복원 |
| `data()` / `size()` / `to_bytes()` | — |
| `to_string()` | printable UTF-8, 그다음 4-byte를 uint32로, 그다음 16-byte를 GUID 포맷으로, 마지막 `hex:` 접두 fallback |
| `to_hex()` | `from_hex`와 round-trip 가능 |
| `operator==`/`!=`, `std::hash<routing_id_t>` | 값 동등성, unordered container 지원 |

**완료 결과.** 모든 factory·accessor는 동기다. 빈 입력, 255바이트 초과, 크기는
0이 아닌데 null pointer면 `std::invalid_argument`를 던진다. `from_hex`에 잘못된
hex 문자열을 주면 마찬가지다.

**선택 기준.** 사람이 부여한 identity엔 `from(const std::string&)`를, 숫자·GUID
형태 identity엔 `from(uint32_t)`/16-byte 배열 overload를, 이미 binary인
identity엔 raw byte overload를 쓴다. 내구성 있는 round trip 전용으로
`to_hex()`/`from_hex()`를 쓴다 — `to_string()`은 표시 전용이다.

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

**옵션.**

| Member | 매개변수 | 의미 |
| --- | --- | --- |
| `version(int &major_, int &minor_, int &patch_)` | 세 개의 출력 참조 | — |
| `error_text(int errnum_) noexcept` | — | `const char*` 반환; caller가 수정·해제하면 안 됨 |
| `has(const std::string &capability_)` | `"tcp"`/`"ipc"`/`"tls"`/`"ws"`/`"wss"`; 그 외 `false` | — |

**완료 결과.** 셋 다 동기이며 예외를 던지지 않는다.

**선택 기준.** 동적으로 로드된 native library가 기대와 일치하는지 확인하려면
`version()`을 쓴다. 기동 시점에 선택적 transport를 분기하려면 `has(...)`를 쓴다.

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

**옵션.** 셋 다: 기본 생성 가능, move-only, `valid() const noexcept`, `close()`.

| 타입 | Member |
| --- | --- |
| `stopwatch_t` | `intermediate()`/`stop()`(둘 다 `uint64_t` 마이크로초) |
| `atomic_counter_t` | `set(int)`, `increment()`/`decrement()`(*새* 값 반환), `value() const` |
| `thread_t(std::function<void()> task_)` | 생성 즉시 task 실행; `join()`은 끝날 때까지 block |

**완료 결과.** 모든 member는 동기다. 소멸자는 아직 닫히지 않았으면 `close()`를
호출한다.

**선택 기준.** 스레드 전체에서 안전한 공유 count엔 `atomic_counter_t`를 쓴다.
벤치마킹엔 `stopwatch_t`를 쓴다. 플랫폼 특정 API 대신 이식 가능한 background
thread엔 `thread_t`를 쓴다.

---

[`Contracts/Core/`](../../../../bindings/cpp/include/zlink/Contracts/Core/)와
[C++ 바인딩 스펙](../../spec/cpp/README.ko.md)에서 전체 근거를 확인한다.
