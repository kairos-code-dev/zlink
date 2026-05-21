# 고성능 Poller 인터페이스 적용 계획

> 이 문서는 모든 바인딩의 poller public API를 하나의 고성능 형태로 정렬하기 위한
> 실행 계획이다. 아직 구현 전 계획이며, 실제 공개 계약은 별도 draft spec 작성,
> core/header 반영, 언어별 회귀 테스트를 거친 뒤 확정한다.

## 1. 목표

현재 multi perf에서 single 대비 성능 차이가 크게 벌어지는 원인 후보 중 하나는
poller 결과를 언어 런타임에 전달하는 방식이다. C 기준은 호출자가 준비한 이벤트
배열을 `zlink_poller_wait`에 넘기고, 함수가 그 배열 앞쪽에 ready event만 채운 뒤
개수를 반환한다. 이 방식은 매 wait마다 새 list/object를 만들 필요가 없고,
ready가 아닌 소켓을 다시 훑을 필요도 없다.

이번 작업의 목표는 이 C 기준 의미를 모든 바인딩에 같은 public API 형태로 제공하는
것이다. perf 전용 우회 API를 만들지 않고, 일반 애플리케이션 event loop에서도 쓸 수
있는 단일 고성능 poller 인터페이스로 정리한다.

## 2. 고정 결정

- poller 대기 public API는 고성능 형태 하나만 둔다.
- 호출자가 event buffer를 소유하고 재사용한다.
- poller wait는 buffer를 채우고 ready event 개수만 반환한다.
- 호출자는 `events[0:count]`만 처리한다.
- wait 호출은 ready event 목록을 반환 list/array로 새로 만들지 않는다.
- hot path에서 전체 등록 source를 다시 훑는 `isReady(index)` 방식은 사용하지 않는다.
- hot path dispatch는 정수 `user_data` / `slot` 값으로 수행한다.
- object `tag`는 표준 고성능 wait API의 dispatch key로 쓰지 않는다. managed/JS
  런타임에서 object tag를 event로 되돌리려면 map lookup이나 wrapper 조회가 필요해질
  수 있기 때문이다.
- `tag`, `userData`, `rawTag`처럼 object나 pointer를 그대로 돌려주는 바인딩별
  이름을 남기지 않는다. non-C 바인딩의 dispatch 값은 모두 정수 `slot`으로 통일한다.
- event에서 socket, timer 같은 언어별 wrapper 객체를 조회하는 기능은 표준 event
  buffer API에 넣지 않는다. 호출자는 `slot`으로 자기 쪽 source table을 직접 찾는다.
- API 이름은 언어별 관용을 따르되 의미는 모두 `Poller.wait(events, timeout) -> count`로
  맞춘다.
- C core의 `zlink_poller_wait` 의미와 다른 readiness, retry, timeout 정책을 만들지 않는다.
- C core가 반환하지 않는 전체 ready 수를 바인딩이 별도로 계산해 반환하지 않는다.
  반환값은 항상 이번 호출에서 event buffer에 실제 기록된 개수다.
- perf 수치 달성만을 위한 internal handle 노출, raw C handle 노출, perf 전용 API는 만들지 않는다.
- 이 계획의 대상은 객체형 poller API다. C의 `zlink_poll` 배열 poll은 기존 C low-level
  API로 유지하되, 다른 바인딩에 별도 배열 poll API를 새로 만들지 않는다.

## 3. 공통 인터페이스 의미

공통 모델:

```text
poller.add_socket(socket, events, slot)
poller.add_fd(fd, events, slot)
poller.add_timer(timer, slot)
poller.modify(source, events)
poller.remove(source)

count = poller.wait(events_buffer, timeout)
for i where 0 <= i < count:
    event = events_buffer[i]
```

언어에 따라 `add(...)` overload 하나로 socket/fd/timer를 받거나,
`add_socket` / `add_fd` / `add_timer`처럼 나누어도 된다. 다만 모든 등록 경로는
정수 slot을 받아야 하며 object tag를 받는 overload를 표준 public surface로 두지 않는다.
`modify`와 `remove`는 C poller처럼 등록 source(socket, fd, timer)를 기준으로 해도
된다. 이 경로는 hot path가 아니며, wait 결과 dispatch를 위해 wrapper나 tag를
조회해서는 안 된다. timer source는 C처럼 별도 event mask 없이 읽기 준비 신호와
같은 의미로 등록해도 된다.

`events_buffer`는 호출자가 반복해서 재사용한다. `count`가 `0`이면 timeout 또는 현재
ready event 없음이다. 실패는 각 언어의 기존 오류 모델에 맞게 반환하거나 예외로
보고한다. timeout 의미는 core와 같아야 한다. 무한 대기, 즉시 반환, 제한 대기는
언어별 타입만 다르고 의미는 같아야 한다.

`events_buffer`의 capacity는 1 이상이어야 한다. 빈 buffer를 넘긴 호출은 성공한
timeout으로 처리하지 않고 명확한 invalid argument 오류로 처리한다. buffer가 작아서
ready event 일부만 반환되는 경우에도 반환값은 buffer capacity를 넘지 않아야 한다.

event에는 최소한 아래 정보가 있어야 한다.

| 항목 | 의미 |
|------|------|
| source kind | socket, fd, timer 구분 |
| slot | `add` 시 호출자가 등록한 정수 dispatch key |
| ready event mask | 실제 발생한 이벤트 비트마스크. C poller event의 필드명은 `events`이며, 기존 바인딩 관용상 `revents` 이름을 쓸 수 있다. |
| fd | fd source일 때의 native descriptor |

event에는 아래 정보를 넣지 않는다.

| 항목 | 제외 이유 |
|------|-----------|
| socket/timer wrapper | wait 결과를 만들 때 wrapper table 조회가 필요해질 수 있음 |
| object tag | managed/JS/Python 런타임 object 조회와 allocation을 유발할 수 있음 |
| raw C handle | public API가 core 내부 표현에 묶임 |
| total ready count | C `zlink_poller_wait`가 제공하지 않는 의미를 바인딩이 새로 만들게 됨 |

성능형 event loop는 `slot`과 ready event mask만으로 dispatch해야 한다. socket/timer wrapper
객체가 필요하면 호출자 코드가 `slot`으로 자신이 관리하는 배열이나 slice에서 찾아야
한다. 이 조회는 poller API 내부에서 수행하지 않는다.

slot 타입은 언어별로 아래 원칙을 따른다.

| 언어 | slot 타입 |
|------|-----------|
| C | `void *user_data`를 유지하되 정수 slot을 `uintptr_t`로 왕복할 수 있어야 함 |
| C++ | `std::uintptr_t` |
| .NET | `nuint` |
| Java | `long` |
| Go | `uintptr` |
| Rust | `usize` |
| Node | 안전한 정수 범위의 `number` |
| Python | `int` |

slot은 pointer 값을 표현하기 위한 API가 아니라 사용자 dispatch key를 표현하는 API다.
slot은 음수가 아닌 정수여야 하며, 각 binding은 자기 언어의 slot 타입과 native
`uintptr_t` 사이에서 손실 없이 왕복할 수 없는 값을 거부한다. Node binding은
`Number.isSafeInteger` 범위를 벗어난 값을 거부하고, perf와 samples는 작은 정수
slot을 사용한다.

timeout 표현은 아래처럼 고정한다.

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

## 4. 언어별 public API 목표

### C

C는 기준이다. 기존 계약을 유지한다.

```c
zlink_poller_event_t events[128];
int n = zlink_poller_wait(poller, events, 128, timeout_ms, &error);
```

확인 사항:

- `zlink_poller_wait`가 ready event만 `events[0:n]`에 채운다는 계약을 spec에 명확히 둔다.
- `user_data`를 바인딩에서 source lookup 없이 빠른 dispatch key로 쓸 수 있음을 명시한다.
- binding perf는 socket pointer 비교보다 `user_data` slot dispatch를 우선 사용한다.

### C++

목표 API:

```cpp
std::vector<zlink::poll_event_t> events(poller.size());
std::size_t n = poller.wait(events.data(), events.size(), timeout);
```

작업:

- socket/fd/timer 등록 API는 `std::uintptr_t` slot을 받는다.
- 포인터와 capacity를 받는 `wait`를 public API의 기본 형태로 둔다.
- 기존 vector 반환형이나 allocation형 wait API가 있으면 public surface에서 제거한다.
- `poll_event_t`에는 `source_kind`, `slot`, ready event mask, fd만 남긴다.
- 기존 `std::any tag`, `raw_tag`, timer pointer 반환 필드는 public event에서 제거한다.
- perf는 미리 할당한 vector storage를 반복 재사용한다.
- perf dispatch는 `poll_event_t::slot` 또는 같은 의미의 `user_data` 정수 값을 사용한다.

### .NET

목표 API:

```csharp
Span<PollEvent> events = stackalloc PollEvent[128];
int n = poller.Wait(events, timeout);
```

작업:

- socket/fd/timer 등록 API는 `nuint` slot을 받는다.
- `Poller.Wait(Span<PollEvent> events, TimeSpan timeout)`를 단일 대기 API로 둔다.
- list/array 반환형 wait API가 있으면 제거하거나 public surface에서 제외한다.
- `Poller.Wait(..., out int totalReady)`처럼 buffer 기록 개수와 별도의 ready 총수를
  반환하는 API는 표준 public surface에 두지 않는다.
- `PollEvent`는 struct로 유지하고, wait 중 allocation이 발생하지 않도록 확인한다.
- `PollEvent`의 hot path 필드는 `SourceKind`, `Slot`, `Revents`, `Fd`로 제한한다.
  `Socket`, `Timer`, object `Tag` 같은 wrapper/object 필드는 표준 event struct에 두지 않는다.
- framework runtime이 poller를 사용할 때도 이 Span 기반 API만 사용한다.
- framework/perf hot path는 `PollEvent.Slot` 또는 같은 의미의 정수 값을 사용한다.

### Java

목표 API:

```java
PollEvents events = new PollEvents(128);
int n = poller.wait(events, timeout);
```

작업:

- `PollEvents`는 재사용 가능한 mutable buffer다. 내부는 객체 배열이 아니라 native
  event storage 또는 primitive array storage여야 한다.
- socket/fd/timer 등록 API는 `long` slot을 받는다.
- `poller.wait(PollEvents events, Duration timeout)`만 public wait API로 둔다.
- `List<PollEvent>` 반환형 wait API는 만들지 않는다.
- 기존 object `tag` 등록 overload와 `PollEvent` record의 socket/timer/tag 반환 필드는
  public surface에서 제거한다.
- `PollEvents`는 hot path 접근자 `sourceKind(i)`, `slot(i)`, `revents(i)`,
  `fd(i)`를 제공한다.
- `PollEvents`는 `socket(i)`와 `timer(i)` 같은 wrapper 조회 접근자를 제공하지 않는다.
- hot path에서 `PollEvent` 객체를 매 event마다 생성하지 않는다.

### Go

목표 API:

```go
events := make([]zlink.PollEvent, 128)
n, err := poller.Wait(events, timeout)
```

작업:

- socket/fd/timer 등록 API는 `uintptr` slot을 받는다.
- `func (p *Poller) Wait(events []PollEvent, timeout time.Duration) (int, error)`를
  단일 대기 API로 둔다.
- 기존 `WaitMany() []PollEvent` 형태가 있으면 제거하거나 public surface에서 제외한다.
- 기존 단일 event 반환형 `Wait(timeout)`이 있으면 새 표준 API와 이름이 충돌하므로
  제거하거나 내부 helper로 내린다.
- `AddSocket/AddFd/AddTimer`의 variadic `userData ...interface{}` 형태는 정수 slot
  인자로 교체한다.
- `PollEvent`에는 `SourceKind`, `Slot`, ready event mask, `Fd`만 남긴다. `Socket`,
  `Timer`, `UserData interface{}` 필드는 public event에서 제거한다.
- `events` slice 길이가 capacity이며, 반환값 `n`만큼만 유효하다는 계약을 테스트한다.
- `nil` 또는 길이 0 slice 입력은 명확한 오류로 처리한다.
- perf dispatch는 `PollEvent.Slot` 또는 같은 의미의 정수 값을 사용한다.

### Rust

목표 API:

```rust
let mut events = vec![PollEvent::default(); 128];
let n = poller.wait(&mut events, timeout_ms)?;
```

작업:

- socket/fd/timer 등록 API는 `usize` slot을 받는다.
- `pub fn wait(&self, events: &mut [PollEvent], timeout_ms: i64) -> Result<usize, RecvError>`를
  단일 대기 API로 둔다.
- `Vec<PollEvent>`를 생성해 반환하는 wait API와 단일 event 반환형 wait API는 public
  surface에 두지 않는다.
- 등록 API의 `Option<*mut c_void>` user data는 public surface에서 `usize` slot으로
  교체한다.
- `PollEvent`는 `Default + Copy` 또는 재사용에 적합한 형태로 유지한다.
- `events[..n]`만 유효하다는 계약을 문서와 테스트에 반영한다.
- perf dispatch는 `PollEvent.slot` 또는 같은 의미의 정수 값을 사용한다.

### Node

목표 API:

```ts
const events = new zlink.PollEvents(128);
const n = poller.wait(events, timeoutMs);
```

작업:

- `PollEvents`는 native-backed reusable buffer다.
- socket/fd/timer 등록 API는 안전한 정수 범위의 `number` slot을 받는다.
- `Poller.wait(events: PollEvents, timeoutMs: number): number`만 public wait API로 둔다.
- `PollEvent[]` 반환형 wait API는 만들지 않는다.
- 기존 `wait(timeoutMs): PollEvent | null`, `waitMany(...)`, object `tag` 등록
  overload는 public surface에서 제거한다.
- `PollEvents`는 hot path 접근자 `sourceKind(i)`, `slot(i)`, `revents(i)`,
  `fd(i)`를 제공한다.
- `PollEvents`는 `socket(i)`와 `timer(i)` 같은 wrapper 조회 접근자를 제공하지 않는다.
- TypeScript typecheck에서 allocation형 wait API가 노출되지 않는지 확인한다.

### Python

목표 API:

```python
events = zlink.PollEvents(128)
n = poller.wait(events, timeout_ms)
```

작업:

- `PollEvents`는 ctypes/native buffer를 보유하는 reusable object다.
- socket/fd/timer 등록 API는 `int` slot을 받는다.
- `Poller.wait(events, timeout_ms) -> int`만 public wait API로 둔다.
- list 반환형 `poll()` 또는 `wait()`는 새 public surface로 유지하지 않는다.
- 기존 `tag`/`user_data` 등록 인자와 `PollEvent` dataclass의 socket/timer/tag 반환
  필드는 public surface에서 제거한다.
- `PollEvents`는 hot path 접근자 `source_kind(i)`, `slot(i)`, `revents(i)`,
  `fd(i)`를 제공한다.
- `PollEvents`는 `socket(i)`와 `timer(i)` 같은 wrapper 조회 접근자를 제공하지 않는다.
- Python perf는 같은 `PollEvents` 인스턴스를 active loop 동안 재사용한다.

## 5. 적용 순서

### Phase 1. Draft spec 작성

목표:

- 구현 전 공개 계약을 `doc/spec/draft/`에 먼저 고정한다.

작업:

- `doc/spec/draft/high-performance-poller-interface.ko.md` 작성
- C 기준 구조체, wait 의미, timeout 의미, buffer 유효 범위, 오류 의미 정리
- 언어별 이름 차이는 허용하되 의미는 하나임을 명시

완료 조건:

- draft 첫머리에 아직 공개 계약이 아님을 명시한다.
- 기존 `doc/spec/core/polling.ko.md`와 충돌하지 않는다.
- list/array 반환형 wait API를 새 표준으로 두지 않는다고 명시한다.

### Phase 2. Core/C 계약 점검

목표:

- core/C API가 고성능 poller 계약의 기준으로 충분한지 확인한다.

작업:

- `bindings/c/include/zlink/monitoring.h`
- `core/include/zlink.h`
- `doc/spec/core/polling.ko.md`
- `bindings/c/tests/`

완료 조건:

- `zlink_poller_wait`의 buffer fill 계약이 테스트로 고정된다.
- `user_data`가 event에 그대로 돌아오는 회귀 테스트가 있다.
- `n_events_`가 0 이하일 때의 오류 의미가 명확하다.
- timer source는 기존 C 계약처럼 별도 event mask 없이 등록되는지 확인하고,
  바인딩 draft에서 같은 의미를 유지한다.

### Phase 3. 바인딩 public API 반영

목표:

- 모든 바인딩이 단일 고성능 wait API만 제공하도록 맞춘다.

작업 대상:

- C++:
  - `bindings/cpp/include/zlink/Contracts/Monitoring/poller.hpp`
  - `bindings/cpp/tests/contract/test_cpp_contract_monitor.cpp`
- .NET:
  - `bindings/dotnet/src/Zlink/Contracts/Monitoring/Poller.cs`
  - `bindings/dotnet/src/Zlink/Contracts/Monitoring/PollEvent.cs`
  - `bindings/dotnet/src/Zlink/Runtime/Native/NativeMethods.Poller.cs`
  - `bindings/dotnet/src/Zlink/Runtime/Monitoring/Poller.cs`
  - `bindings/dotnet/tests/Zlink.Tests/`
- Java:
  - `bindings/java/src/main/java/systems/zlink/contracts/Poller.java`
  - `bindings/java/src/main/java/systems/zlink/contracts/PollEvent.java`
  - `bindings/java/src/main/java/systems/zlink/runtime/nativebridge/Native.java`
  - `bindings/java/src/test/java/systems/zlink/contract/`
- Go:
  - `bindings/go/poller_timer.go`
  - `bindings/go/contracts/contracts.go`
  - `bindings/go/surface_test.go`
  - `bindings/go/behavior_test.go`
- Rust:
  - `bindings/rust/src/runtime/monitoring/poller.rs`
  - `bindings/rust/src/runtime/native/ffi.rs`
  - `bindings/rust/src/lib.rs`
  - `bindings/rust/tests/`
- Node:
  - `bindings/node/src/zlink/runtime/core/canonical.ts`
  - `bindings/node/src/zlink/runtime/native/native.ts`
  - `bindings/node/src/zlink/contracts/monitoring/`
  - `bindings/node/native/src/addon_core.cc`
  - `bindings/node/native/src/addon_core_api.h`
  - `bindings/node/tests/`
- Python:
  - `bindings/python/src/zlink/contracts/monitoring/poller.py`
  - `bindings/python/src/zlink/_native/ffi.py`
  - `bindings/python/src/zlink/_runtime/monitoring/poller.py`
  - `bindings/python/tests/`

Node는 source 변경 뒤 `dist/`와 `dist-tools/` 산출물 갱신이 필요할 수 있다. 이 경우
source와 생성물의 public surface가 같은지 typecheck로 확인한다. Rust/Go/Python의
vendored `include/zlink*.h` 파일은 core header 변경이 있을 때만 동기화한다. Python은
`__pycache__` 같은 로컬 캐시 파일을 작업 대상에 포함하지 않는다.

완료 조건:

- 각 언어의 public surface test가 새 wait API를 확인한다.
- allocation형 wait API가 public surface에 남아 있지 않다.
- 단일 event 반환형 wait API가 public surface에 남아 있지 않다.
- non-C 바인딩의 public event type에는 socket/timer wrapper 객체나 object tag 필드가
  남아 있지 않다. source 객체가 필요하면 사용자가 slot으로 자신의 source table을 조회한다.
- `add/modify/remove/size` 의미는 기존 C poller와 동일하다.
- 각 언어의 오류 모델에 맞는 invalid buffer 테스트가 있다.

### Phase 4. 회귀 테스트 추가

목표:

- API 모양과 성능에 영향을 주는 poller 동작을 언어별로 막는다.

공통 테스트:

- ready event 하나가 `events[0]`에 기록된다.
- 여러 ready source가 `events[0:n]`에 기록된다.
- `slot`이 등록한 값으로 돌아온다.
- hot path에서 wrapper 객체 조회 없이 `slot`과 ready event mask만으로
  dispatch할 수 있다.
- `modify` 후 관심 이벤트가 바뀐다.
- `remove` 후 event가 나오지 않는다.
- timeout 시 count가 0이다.
- buffer capacity보다 많은 ready source가 있을 때 반환값은 capacity를 넘지 않고,
  buffer 밖을 쓰지 않는다. 아직 ready 상태인 source는 다음 wait에서 다시 관찰될 수
  있어야 한다.
- timer source와 socket source를 같은 buffer에서 구분한다.

언어별 추가 테스트:

- .NET: `Span<PollEvent>` wait가 allocation 없이 동작하는지 최소 회귀를 둔다.
- Java/Node/Python: 반복 wait에서 result list/object를 새로 만들지 않는 API만 노출되는지 surface test를 둔다.
- Java/Node/Python: `PollEvents`의 hot path 접근자가 wrapper 객체 생성 없이
  `source kind`, `slot`, ready event mask를 읽을 수 있음을 테스트한다.
- Go/Rust: caller-owned slice가 재사용되고 `n` 이후 영역을 읽지 않는 예제를 테스트한다.

### Phase 5. samples 반영

목표:

- 사용자가 poller를 배울 때도 단일 고성능 API만 보게 한다.

작업:

- C/C++/.NET/Java/Go/Rust/Node/Python sample의 poller 사용 코드를 새 API로 갱신
- `bindings/<lang>/samples`
- Node는 `bindings/node/samples`와 `bindings/node/dist-tools/samples`를 함께 갱신
- `doc/guide/06-monitoring.ko.md`
- `doc/guide/06-monitoring.md`
- 언어별 README 또는 samples README

완료 조건:

- sample에서 list 반환형 wait API를 사용하지 않는다.
- sample은 event buffer를 한 번 만들고 loop에서 재사용한다.
- guide는 내부 구현 설명이 아니라 사용자가 써야 하는 호출 형태만 설명한다.

### Phase 6. perf 반영

목표:

- binding perf가 C 기준 poller 의미를 유지하면서 새 public API를 사용한다.

작업:

- 모든 `bindings/<lang>/perf/single`
- 모든 `bindings/<lang>/perf/multi`
- 모든 `bindings/<lang>/perf/tests`
- Node는 `bindings/node/perf`와 `bindings/node/dist-tools/perf`를 함께 갱신
- 특히 multi routed echo, multi pubsub, SPOT reqrep/sendsend client의 poller loop

완료 조건:

- perf hot path가 새 public wait API를 사용한다.
- active loop에서 wait 결과 list/array allocation이 없다.
- poller wakeup 뒤 ready event만 dispatch한다.
- 전체 socket scan 또는 `isReady(index)` 반복 호출이 없다.
- event dispatch는 socket wrapper lookup이 아니라 `slot`으로 한다.
- C perf에 없는 `sleep`, `Atomics.wait`, 짧은 timeout 반복 같은 인위적 backoff를
  추가하지 않는다.
- `doc/perf` 정책의 timeout, stop token, HWM/MsgUnit 조건을 바꾸지 않는다.
- C perf와 같은 suite/pattern/transport/size 의미를 유지한다.

### Phase 7. framework 반영

목표:

- `.NET` framework/runtime도 새 Span 기반 poller API만 사용한다.

작업:

- `framework/languages/dotnet/src/Zlink.Framework/`
- `framework/languages/dotnet/src/Zlink.Framework.AspNetCore/`
- `framework/languages/dotnet/src/Systems.Zlink.Stream.Connector/`
- `framework/languages/dotnet/tests/`
- `framework/languages/dotnet/samples/`
- `framework/languages/dotnet/doc/guide/samples/`
- framework monitoring/runtime polling loop
- framework tests and samples

완료 조건:

- framework 코드가 제거된 poller API나 allocation형 wait API에 의존하지 않는다.
- monitoring hosted service, registry polling, stream connector receive/progress loop가
  새 poller API와 충돌하지 않는다.
- `Zlink.Framework.sln` 또는 해당 solution의 build/test가 통과한다.
- framework guide/sample 문서가 새 poller 사용 방식과 충돌하지 않는다.

### Phase 8. 문서 승격과 정리

목표:

- 구현 완료 뒤 draft 내용을 정식 문서로 옮긴다.

작업:

- `doc/spec/core/polling.ko.md`
- `doc/spec/core/polling.md`
- `doc/guide/06-monitoring.ko.md`
- `doc/guide/06-monitoring.md`
- binding별 public surface 문서 또는 README
- perf 정책 문서에서 poller hot path 표현이 새 API와 충돌하는지 확인

완료 조건:

- draft 문서 내용이 정식 spec/guide에 나뉘어 반영된다.
- draft가 폐기 또는 완료 상태로 표시된다.
- guide에는 내부 socket/poller 구현 설명을 넣지 않는다.
- spec에는 사용법 설명보다 계약을 중심으로 남긴다.

## 6. 검증 게이트

각 단계는 아래 검증을 통과해야 다음 단계로 넘어간다.

| 단계 | 필수 검증 |
|------|-----------|
| Core/C | C poller contract tests, core build |
| C++ | contract tests, sample build, perf smoke |
| .NET binding | binding tests, surface tests, perf smoke |
| Java | unit/contract tests, type/surface tests, perf smoke |
| Go | `go test`, surface tests, perf smoke |
| Rust | `cargo test`, surface tests, perf smoke |
| Node | typecheck, surface tests, perf smoke |
| Python | pytest, surface tests, perf smoke |
| Framework | framework tests, sample regression tests |

perf 검증은 smoke-first로 진행한다. timeout/no-result는 통과 근거가 아니며,
poller API 변경 후 수치가 좋아져도 C perf와 테스트 의미가 달라졌다면 반영하지 않는다.

## 7. 진행 상태

- [ ] Phase 1. Draft spec 작성
- [ ] Phase 2. Core/C 계약 점검
- [ ] Phase 3. 바인딩 public API 반영
- [ ] Phase 4. 회귀 테스트 추가
- [ ] Phase 5. samples 반영
- [ ] Phase 6. perf 반영
- [ ] Phase 7. framework 반영
- [ ] Phase 8. 문서 승격과 정리

## 8. 작업 중 주의 사항

- 이 계획은 public API 변경을 포함하므로 구현 전에 draft spec을 먼저 작성한다.
- perf 수치만으로 API를 확정하지 않는다. 일반 사용자가 event loop를 효율적으로 작성할 수
  있는 public 계약인지 확인한다.
- 바인딩별 별도 API 이름을 만들지 않는다. 언어 관용에 맞추되 `wait(events, timeout)`
  의미에서 벗어나지 않는다.
- 기존 allocation형 wait API 제거는 breaking change일 수 있으므로 각 언어별 release note
  또는 migration note 필요 여부를 implementation 단계에서 확인한다.
- framework는 binding public API만 사용한다. reflection, internal 접근,
  `InternalsVisibleTo` 추가로 우회하지 않는다.
