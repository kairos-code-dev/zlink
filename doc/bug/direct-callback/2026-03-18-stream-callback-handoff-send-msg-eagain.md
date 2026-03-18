# STREAM raw callback handoff 후 worker-thread resend `EAGAIN` 회귀

## 요약

`ZLINK_STREAM` raw callback으로 전달받은 `zlink_msg_t`를 worker thread로
handoff한 뒤 같은 socket에 `zlink_stream_send_msg()`로 다시 전송하는 기존
계약이 현재 `core`에서 깨진다.

기준 비교 브랜치 `/home/hep7/project/kairos/zlink-direct-callback-rewrite`
에서는 동작하던 시나리오이며, 현재 워크스페이스에서는
`EAGAIN (11)`으로 실패한다.

## 영향 범위

- STREAM raw callback path
- callback에서 받은 message ownership handoff
- worker-thread resend via `zlink_stream_send_msg()`
- regression test:
  `test_stream_callback_handoff_to_worker_thread_send_msg_is_safe`

## 재현

```bash
cmake -S . -B /home/hep7/project/kairos/zlink/core/build -DZLINK_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /home/hep7/project/kairos/zlink/core/build --target test_stream_threadsafe -j$(nproc)
timeout 20s /home/hep7/project/kairos/zlink/core/build/bin/test_stream_threadsafe
```

## 실제 결과

`core/tests/integration/test_stream_threadsafe.cpp`의
`test_stream_callback_handoff_to_worker_thread_send_msg_is_safe`가 실패한다.

실패 로그:

```text
test_stream_callback_handoff_to_worker_thread_send_msg_is_safe:FAIL: Expected 0 Was 11
```

여기서 `11`은 `EAGAIN`이다.

## 기대 결과

- raw callback에서 받은 `zlink_msg_t`를 worker thread로 handoff 가능해야 한다.
- handoff된 msg를 `zlink_stream_send_msg()`로 같은 STREAM socket에 다시
  보내도 성공해야 한다.
- test expectation대로 `probe.send_errno == 0` 이어야 한다.
- payload echo round-trip이 성공해야 한다.

## 회귀 판단

회귀로 본다.

근거:

- 기준 브랜치
  `/home/hep7/project/kairos/zlink-direct-callback-rewrite`
  의 동일 시나리오는 raw callback 기반으로 작성돼 있다.
- 현재 워크스페이스에서도 해당 테스트를 기준 브랜치와 같은 raw callback
  경로로 최대한 맞췄지만, 현재 쪽만 `EAGAIN`이 남는다.

## 현재까지 확인한 범위

- direct `zlink_recv()` parity 문제는 아니다.
- generic multipart `zlink_recv_handler()`만의 문제도 아니다.
- STREAM raw callback 경로를 hidden compat 형태로 복원해도 동일하게 재현된다.
- 다른 STREAM thread-safe tests는 대부분 통과한다.
- 따라서 문제 범위는 STREAM send/write readiness 또는 route ownership 하위
  경로로 좁혀진다.

## 기준 브랜치와 비교하며 시도한 내용

- STREAM raw callback helper
  `zlink_stream_attach_raw()` / `zlink_stream_detach()` 내부 복원
- `test_stream_threadsafe`를 기준 브랜치처럼 raw callback 경로로 재정렬
- `session_base.cpp`의 STREAM dispatch ordering을 기준 브랜치와 동일하게 정렬
- `stream.cpp`의 raw dispatch routing-id 해석 순서를 기준 브랜치 방식으로 조정
- STREAM test compat wrapper를 기준 브랜치 helper semantics에 맞게 조정

위 변경 후에도 실패는 동일하다.

## 주요 의심 지점

- `core/src/sockets/stream.cpp`
  - direct-route `xsend()`
  - selected out pipe readiness
  - route shard ownership
- `core/src/core/pipe.cpp`
  - actual write readiness / peer state
- `core/src/core/session_base.cpp`
  - STREAM dispatch와 raw delivery ordering
- `core/src/sockets/socket_base.cpp`
  - socket dispatch와 stream dispatch interaction

## 관찰 메모

- 실패는 callback thread 안이 아니라 worker thread resend 시점에 발생한다.
- 증상은 `EHOSTUNREACH`가 아니라 `EAGAIN`이므로 route 자체가 아예 없는 경우보다
  write readiness 또는 pipe state 문제일 가능성이 높다.
- 현재 core에는 canonical public API 정렬 작업 외에도 STREAM callback rewrite,
  socket dispatch bridge, recv/source-rid 추적 변경이 함께 들어가 있어 단일 변경점
  하나로 단정하기 어렵다.

## 참고 파일

- 현재 실패 테스트:
  `/home/hep7/project/kairos/zlink/core/tests/integration/test_stream_threadsafe.cpp`
- 현재 STREAM 구현:
  `/home/hep7/project/kairos/zlink/core/src/sockets/stream.cpp`
- 기준 브랜치 테스트:
  `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/tests/integration/test_stream_threadsafe.cpp`
- 기준 브랜치 STREAM 구현:
  `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/stream.cpp`
