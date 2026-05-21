# 고성능 Poller 인터페이스 초안

이 문서는 구현 전 초안으로 작성되었고, 고성능 poller public API 반영이 끝난 뒤
정식 spec과 guide로 승격되었다. 현재 공개 계약은 `doc/spec/core/polling.ko.md`,
`doc/spec/core/polling.md`, `doc/guide/06-monitoring.ko.md`,
`doc/guide/06-monitoring.md` 및 각 바인딩의 public surface가 기준이다.
이 문서는 설계 이력 보존용이며 새 계약의 단일 기준이 아니다.

## 배경

C poller는 호출자가 준비한 `zlink_poller_event_t` 배열을 `zlink_poller_wait()`에
넘기고, 함수가 그 배열 앞쪽에 준비된 이벤트만 채운 뒤 개수를 반환한다.
이 방식은 wait마다 결과 list나 event object를 새로 만들지 않고, 준비되지 않은
source를 다시 훑지 않아도 된다.

여러 바인딩은 이 의미를 그대로 노출하지 않고 단일 event 반환, list 반환, object
tag 반환, socket/timer wrapper 반환 같은 형태를 제공한다. 이 형태는 사용하기는
쉬울 수 있지만 high-throughput event loop에서는 allocation, wrapper lookup,
전체 source scan이 hot path에 들어갈 수 있다.

이 초안의 목표는 perf 전용 우회 API가 아니라 일반 응용도 사용할 수 있는 단일
고성능 poller public API를 모든 바인딩에 제공하는 것이다.

## 목표

1. 모든 non-C 바인딩의 poller wait public API를 caller-owned event buffer 기반으로
   맞춘다.
2. wait 호출은 buffer에 실제 기록한 ready event 개수만 반환한다.
3. dispatch key는 object tag가 아니라 음수가 아닌 정수 `slot`으로 통일한다.
4. wait hot path에서 socket/timer wrapper lookup, object allocation, 전체 source
   scan을 요구하지 않는다.
5. C `zlink_poller_wait()`의 timeout, buffer fill, 반환값 의미를 바꾸지 않는다.
6. perf 전용 API, raw C handle 노출, internal handle 노출을 만들지 않는다.

## 비목표

1. C의 `zlink_poll` 배열 poll API를 없애거나 다른 바인딩에 새로 복제하지 않는다.
2. C `zlink_poller_event_t`의 ABI를 이 초안만으로 변경하지 않는다.
3. poller의 readiness 의미, retry 정책, timeout 정책을 바꾸지 않는다.
4. 바인딩별 event loop 구현 방식을 강제하지 않는다.
5. public API 바깥의 private helper 이름까지 통일하지 않는다.

## C 기준 계약

C는 기준 계약이다. 기존 함수 시그니처를 유지한다.

```c
int zlink_poller_wait(void *poller_,
                      zlink_poller_event_t *events_,
                      int n_events_,
                      long timeout_,
                      zlink_config_result_t *error_out_);
```

계약:

1. `events_`는 호출자가 준비한 이벤트 배열이다.
2. `n_events_`는 `events_` 배열에 기록할 수 있는 최대 이벤트 수다.
3. 성공 시 반환값은 `events_[0]`부터 실제 기록한 이벤트 개수다.
4. 반환값은 `n_events_`보다 클 수 없다.
5. timeout 또는 현재 준비된 이벤트가 없으면 `0`을 반환한다.
6. 실패하면 `-1`을 반환하고 `error_out_`에 실패 원인을 기록한다.
7. `user_data`는 등록 시 넘긴 값과 같은 값으로 이벤트에 돌아와야 한다.
8. `events` 필드는 실제 발생한 ready event mask다.

`n_events_`가 0 이하이거나 `events_`가 유효하지 않은 경우는 성공한 timeout으로
처리하지 않는다. 정식 구현에서는 명확한 invalid argument 오류로 고정해야 한다.

## 공통 바인딩 계약

non-C 바인딩의 표준 poller wait 의미는 아래 한 가지다.

```text
poller.add_socket(socket, events, slot)
poller.add_fd(fd, events, slot)
poller.add_timer(timer, slot)

count = poller.wait(events_buffer, timeout)
for i where 0 <= i < count:
    event = events_buffer[i]
```

언어별 관용에 따라 `add(...)` overload를 쓰거나 `add_socket`처럼 이름을 나눌 수
있다. 그러나 모든 등록 경로는 정수 `slot`을 받아야 한다. object `tag`, object
`userData`, raw pointer tag를 public dispatch 값으로 받는 overload는 표준 surface에
두지 않는다.

`modify`와 `remove`는 C poller처럼 등록 source를 기준으로 할 수 있다. 이 경로는
wait hot path가 아니므로 source object를 인자로 받는 것이 금지되지는 않는다.
다만 wait 결과 dispatch를 위해 poller가 wrapper나 tag table을 조회해서는 안 된다.

## Event Buffer 계약

`events_buffer`는 호출자가 소유하고 반복해서 재사용한다.

1. buffer capacity는 1 이상이어야 한다.
2. wait는 `events_buffer[0:count]`만 유효한 event로 채운다.
3. `count` 이후 영역은 읽지 않는다.
4. buffer가 ready event 수보다 작아도 반환값은 buffer capacity를 넘지 않는다.
5. 아직 ready 상태인 source는 다음 wait에서 다시 관찰될 수 있다.
6. wait 결과 list, array, object collection을 새로 만들어 반환하지 않는다.

event buffer의 각 항목은 hot path dispatch에 필요한 최소 정보만 제공한다.

| 항목 | 의미 |
|------|------|
| source kind | socket, fd, timer 구분 |
| slot | 등록 시 호출자가 넘긴 dispatch key |
| ready event mask | 실제 발생한 이벤트 비트마스크 |
| fd | fd source일 때의 native descriptor |

event 항목에는 아래 정보를 넣지 않는다.

| 항목 | 제외 이유 |
|------|-----------|
| socket/timer wrapper | wait 결과 생성 중 wrapper table 조회가 필요함 |
| object tag | managed runtime object 조회와 allocation을 유발할 수 있음 |
| raw C handle | public API가 core 내부 표현에 묶임 |
| total ready count | C `zlink_poller_wait()`가 제공하지 않는 의미임 |

## Slot 계약

`slot`은 사용자 dispatch key다. pointer 값을 표현하기 위한 API가 아니다.

공통 규칙:

1. `slot`은 음수가 아닌 정수다.
2. native `uintptr_t`와 손실 없이 왕복할 수 있어야 한다.
3. 왕복할 수 없는 값은 invalid argument 오류로 거부한다.
4. perf와 samples는 작은 정수 slot을 사용한다.

언어별 slot 타입:

| 언어 | slot 타입 |
|------|-----------|
| C | `void *user_data`, 정수 왕복은 `uintptr_t` 사용 |
| C++ | `std::uintptr_t` |
| .NET | `nuint` |
| Java | `long` |
| Go | `uintptr` |
| Rust | `usize` |
| Node | safe integer `number` |
| Python | `int` |

Java와 Python은 언어 타입이 native pointer 폭보다 넓을 수 있다. 해당 바인딩은
현재 platform의 `uintptr_t` 범위를 넘는 값을 거부해야 한다. Node는
`Number.isSafeInteger` 범위를 벗어난 값을 거부해야 한다.

## Timeout 계약

timeout 의미는 C와 같아야 한다.

| 언어 | 무한 대기 | 즉시 반환 | 제한 대기 |
|------|-----------|-----------|-----------|
| C | `-1` | `0` | millisecond 정수 |
| C++ | 음수 `std::chrono::milliseconds` | `0ms` | `std::chrono::milliseconds` |
| .NET | `Timeout.InfiniteTimeSpan` | `TimeSpan.Zero` | `TimeSpan` |
| Java | `Duration.ofMillis(-1)` | `Duration.ZERO` | `Duration` |
| Go | 음수 `time.Duration` | `0` | `time.Duration` |
| Rust | `-1` | `0` | millisecond 정수 |
| Node | `-1` | `0` | millisecond 정수 |
| Python | `-1` | `0` | millisecond 정수 |

바인딩은 timeout 의미를 바꾸기 위해 짧은 timeout 반복, sleep, yield, runtime-specific
backoff를 표준 poller wait에 넣지 않는다. wait가 실패하거나 중단된 경우의 오류
표현만 언어별 기존 오류 모델에 맞춘다.

## 언어별 공개 API 모양

### C++

```cpp
std::vector<zlink::poll_event_t> events(capacity);
std::size_t n = poller.wait(events.data(), events.size(), timeout);
```

등록 API는 `std::uintptr_t slot`을 받는다. `poll_event_t`에는 source kind, slot,
ready event mask, fd만 둔다. `std::any tag`, raw tag, timer pointer 반환 필드는
표준 event type에 두지 않는다.

### .NET

```csharp
Span<PollEvent> events = stackalloc PollEvent[128];
int n = poller.Wait(events, timeout);
```

등록 API는 `nuint slot`을 받는다. `PollEvent`는 struct이며 `SourceKind`, `Slot`,
`Revents`, `Fd`만 hot path 필드로 제공한다. `Socket`, `Timer`, object `Tag`,
`out totalReady`를 표준 surface에 두지 않는다.

### Java

```java
PollEvents events = new PollEvents(128);
int n = poller.wait(events, timeout);
```

`PollEvents`는 재사용 가능한 mutable buffer다. 내부는 native event storage 또는
primitive array storage여야 한다. wait는 `List<PollEvent>`를 반환하지 않는다.
hot path 접근자는 `sourceKind(i)`, `slot(i)`, `revents(i)`, `fd(i)`다.
`socket(i)`, `timer(i)`, object tag 접근자는 제공하지 않는다.

### Go

```go
events := make([]zlink.PollEvent, 128)
n, err := poller.Wait(events, timeout)
```

등록 API는 `uintptr slot`을 받는다. `WaitMany() []PollEvent`와 단일 event 반환형
`Wait(timeout)`은 표준 public surface에 두지 않는다. `PollEvent`에는 `SourceKind`,
`Slot`, ready event mask, `Fd`만 둔다.

### Rust

```rust
let mut events = vec![PollEvent::default(); 128];
let n = poller.wait(&mut events, timeout_ms)?;
```

등록 API는 `usize slot`을 받는다. `Vec<PollEvent>` 반환형 wait와 단일 event 반환형
wait는 표준 public surface에 두지 않는다. `PollEvent`는 `Default + Copy` 또는
동등하게 재사용 가능한 형태여야 한다.

### Node

```ts
const events = new zlink.PollEvents(128);
const n = poller.wait(events, timeoutMs);
```

`PollEvents`는 native-backed reusable buffer다. 등록 API는 safe integer `number`
slot을 받는다. `PollEvent[]` 반환형 wait, 단일 `PollEvent | null` 반환형 wait,
object tag 등록 overload는 표준 public surface에 두지 않는다.

### Python

```python
events = zlink.PollEvents(128)
n = poller.wait(events, timeout_ms)
```

`PollEvents`는 ctypes 또는 native buffer를 보유하는 reusable object다. 등록 API는
`int slot`을 받는다. list 반환형 `poll()` 또는 `wait()`와 socket/timer/tag를 담은
`PollEvent` dataclass는 표준 public surface에 두지 않는다.

## 오류 계약

아래 경우는 성공한 timeout으로 처리하지 않는다.

1. event buffer가 `null` 또는 언어별 null 값이다.
2. event buffer capacity가 0이다.
3. slot 값이 음수이거나 native `uintptr_t`와 손실 없이 왕복할 수 없다.
4. 등록 source가 유효하지 않다.
5. timeout 값이 언어별 표현 범위를 벗어난다.

오류의 구체적인 타입은 언어별 기존 오류 모델을 따른다. 그러나 surface test는 위
입력이 성공처럼 보이지 않는다는 점을 확인해야 한다.

## 회귀 테스트 요구

각 바인딩은 최소한 아래 동작을 테스트한다.

1. ready event 하나가 `events[0]`에 기록된다.
2. 여러 ready source가 `events[0:count]`에 기록된다.
3. 등록한 slot이 그대로 돌아온다.
4. wait 결과 dispatch가 wrapper lookup 없이 slot과 ready event mask로 가능하다.
5. `modify` 후 관심 이벤트가 바뀐다.
6. `remove` 후 event가 나오지 않는다.
7. timeout 시 count가 0이다.
8. buffer capacity보다 많은 ready source가 있어도 반환값이 capacity를 넘지 않는다.
9. timer source와 socket source를 같은 buffer에서 구분한다.
10. allocation형 wait API와 단일 event 반환형 wait API가 public surface에 없다.

## 문서 반영 규칙

구현 전에는 이 초안만 수정한다. 정식 `doc/spec/core/polling.ko.md`와
`doc/spec/core/polling.md`에는 현재 공개 헤더와 구현에 존재하는 계약만 유지한다.
구현과 회귀 테스트가 끝난 뒤 이 초안의 계약을 정식 spec과 guide에 나누어 반영한다.
