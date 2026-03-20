# core socket callback support gap for `SUB` / `STREAM`

## Summary

현재 zlink core는 "수신 가능한 socket/service는 `recv` 또는 `callback` 중 하나를
선택할 수 있어야 한다"는 perf 정책 목표와 아직 일치하지 않는다.

특히 아래 두 경로가 막혀 있다.

- `SUB`: `PUBSUB` 패턴의 수신 측이지만 `zlink_recv_handler()` attach가 거부된다.
- `STREAM`: perf surface 이름은 아직 `STREAM_CALLBACK`로 남아 있고, 실제 구현도
  callback variant만 존재한다. `STREAM` 단일 패턴명과 dual-mode(`recv`,
  `callback`) surface가 없다.

이 상태는 perf wrapper 문제가 아니라 core socket callback support matrix의 결함으로
취급해야 한다.

## Why this is a bug

패턴 단위 정책에서 mode 선택은 "그 패턴의 수신 측 handle이 `recv`와 `callback`
둘 다 지원한다"는 뜻이다.

- `PUBSUB`에서 선택 주체는 `PUB`가 아니라 `SUB`다.
- `STREAM`에서 선택 주체는 `STREAM` 수신 경로다.

따라서 `SUB`/`STREAM` 수신 측이 callback receive를 공식적으로 지원하지 않으면,
`PUBSUB callback`과 `STREAM recv|callback` full matrix를 완성할 수 없다.

## Current observed facts

현재 public header는 `zlink_recv_handler()`를 "recv mode에서 callback mode로
전환하는 공식 API"로 설명한다.

참조:

- [`core/include/zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h#L572)

하지만 현재 integration test는 `SUB`에서 이 API가 실패해야 한다고 고정한다.

참조:

- [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp#L340)

현재 runner/perf surface도 `STREAM` 대신 `STREAM_CALLBACK` 이름과 callback-only
구현에 묶여 있다.

참조:

- [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py#L42)
- [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh#L7)
- [`core/perf/multi/src/perf_multi_stream_callback_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_stream_callback_server.cpp#L399)

## Minimal reproduction

### 1. `SUB` callback attach is rejected

Existing regression already reproduces it:

```bash
cmake --build /home/hep7/project/kairos/zlink/core/build --target test_socket_with_handler
ctest --test-dir /home/hep7/project/kairos/zlink/core/build --output-on-failure -R test_socket_with_handler
```

Relevant assertion:

- `zlink_recv_handler(sub, ...) == -1`
- `errno == EINVAL`

Code:

```cpp
TEST_ASSERT_EQUAL_INT (-1, zlink_recv_handler (sub, &capture_raw_message, NULL));
TEST_ASSERT_EQUAL_INT (EINVAL, errno);
```

### 2. `STREAM` dual-mode surface does not exist

Current multi perf surface still exposes only `STREAM_CALLBACK`.

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --recv recv \
  --build-dir /home/hep7/project/kairos/zlink/core/build
```

Current behavior:

- `STREAM` is treated as alias to `STREAM_CALLBACK`
- runner support matrix is callback-only
- no dedicated `STREAM recv` server/client implementation exists

## Actual result

- `SUB` cannot enter callback receive mode through the public direct callback API.
- `PUBSUB` pattern cannot be completed as a true dual-mode matrix without special-case
  workaround outside core.
- `STREAM` remains a callback-named special case instead of a normal dual-mode pattern.

## Expected result

- `SUB` must support callback receive attach when used as the receive side of `PUBSUB`.
- `STREAM` must be renamed to `STREAM` at the official perf surface.
- `STREAM` must support both `--recv recv` and `--recv callback`.
- perf runner/support matrix should not need special-case aliasing that encodes one mode
  in the pattern name.

## Non-goals

아래는 해결로 인정하지 않는다.

- perf runner에서 `PUBSUB callback`을 다른 패턴이나 hidden fallback으로 우회
- `STREAM_CALLBACK` 이름을 유지한 채 문서만 dual-mode처럼 설명
- unsupported matrix를 문서로만 정당화

## Required fix direction

1. core socket mode contract를 패턴 요구사항에 맞게 확장한다.
2. `SUB` callback receive attach를 허용하고 회귀 테스트를 갱신한다.
3. `STREAM` recv path를 구현하고 `STREAM_CALLBACK` 명칭을 `STREAM`으로 수렴한다.
4. perf runner, support matrix, README, policy verification commands를 새 surface에
   맞게 정렬한다.

## Suggested regression coverage

- `SUB`에 `zlink_recv_handler()` attach 성공 + 실제 delivery 확인
- `PUBSUB --recv callback` single/multi smoke
- `STREAM --recv recv` smoke
- `STREAM --recv callback` smoke
- `STREAM_CALLBACK` legacy alias 제거 또는 deprecated alias 동작 검증

## Current repo decision

- 이 문제는 perf 쪽 workaround로 닫지 않는다.
- core bug로 추적하고, core 수정 + 회귀 테스트 + perf surface 정렬을 함께 진행해야
  한다.
