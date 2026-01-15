# Phase 1 ASIO 통합: 테스트 타임아웃/실패 디버깅 (Codex)

## 환경/빌드
- 경로: `/home/ulalax/project/ulalax/zlink`
- 빌드 타입: `Release` (`build/CMakeCache.txt` 기준)
- GDB: Ubuntu 15.0.50.20240403-0ubuntu1 (15.0.50.20240403-git)
- 바이너리 디버그 심볼 없음

## 재현 및 GDB 결과 요약

### 1) `test_pair_inproc` (타임아웃)
실행:
```
$ gdb -q -ex 'set pagination off' -ex 'handle SIGALRM stop' -ex 'run' \
      -ex 'info threads' -ex 'thread apply all bt' -ex 'quit' ./build/bin/test_pair_inproc
```
SIGALRM로 정지된 스택:
- Main thread: `zmq::ctx_t::terminate()` → `_term_mailbox.recv()` → `mailbox_t::recv()`에서 condvar 대기
- Reaper thread: `zmq::asio_poller_t::loop()` 내부에서 mutex lock 대기
- IO thread: `zmq::asio_poller_t::loop()` 내부에서 mutex lock 대기

핵심 포인트:
- 컨텍스트 종료가 `_term_mailbox`의 `done` 커맨드를 기다리며 막힘
- reaper/io_thread 쪽 mailbox 처리가 진행되지 않는 상태

### 2) `test_issue_566` (타임아웃)
실행:
```
$ gdb -q -ex 'set pagination off' -ex 'handle SIGALRM stop' -ex 'run' \
      -ex 'info threads' -ex 'thread apply all bt' -ex 'quit' ./build/bin/test_issue_566
```
SIGALRM로 정지된 스택:
- Main thread: `zmq::ctx_t::terminate()` → `_term_mailbox.recv()` → `mailbox_t::recv()` condvar 대기
- Reaper/IO thread: `zmq::asio_poller_t::loop()` 내부

관찰 결과:
- 이 환경에서는 segfault 대신 동일한 shutdown hang으로 종료됨

### 3) `test_proxy` (실패)
실행:
```
$ gdb -q -ex 'set pagination off' -ex 'handle SIGALRM stop' -ex 'run' \
      -ex 'info threads' -ex 'thread apply all bt' -ex 'quit' ./build/bin/test_proxy
```
결과:
- `tests/test_proxy.cpp:140`에서 `Expected 13 Was -1` 실패
- segfault 대신 기능 실패로 종료

## 원인 분석 (Root Cause)

### 핵심 원인: ASIO mailbox의 wakeup 스케줄링 조건 불일치
`mailbox_t::send()`의 ASIO 경로는 아래 조건에서만 `post()`를 호출합니다.
- `ypipe_t::flush()`가 `false`를 반환할 때만 `post()` 수행

그러나 ASIO mode에서는 **reader가 FD/poller 기반으로 잠든 상태가 존재하지 않음**에도 불구하고,
ypipe의 `flush()`는 다음 조건에서만 `false`를 반환합니다.
- `_cpipe.check_read()`가 `NULL`로 `_c`를 바꿔 **"reader asleep"** 상태가 되었을 때

문제는, **ASIO mode에선 `process_mailbox()`가 아직 한 번도 실행되지 않으면 `_c`가 NULL로 전환되지 않음** →
최초 `send()`에서 `flush()`가 `true`를 반환 → `post()`가 전혀 예약되지 않음.

즉:
- `process_mailbox()`가 단 한 번도 실행되지 못한 상태에서
- 모든 mailbox 커맨드가 큐에 쌓이고
- reaper/io_thread가 커맨드를 처리하지 못해 종료 시점에 `_term_mailbox`가 영원히 대기

이 패턴은 `test_pair_inproc`, `test_issue_566` 같은 대부분의 테스트를 동일하게 timeout으로 유도.

### 부가 관찰
- `asio_poller_t::loop()`는 정상적으로 돌지만, mailbox handler가 등록되지 않으므로 실제 작업이 발생하지 않음
- `socket_base`의 blocking mailbox 경로 자체는 condvar 방식으로 정상 동작하나,
  reaper 쪽 mailbox가 멈추면 종료 루틴에서 `_term_mailbox`가 풀리지 않음

## 수정 제안

### 1) ASIO mode에서는 `flush()` 결과와 무관하게 post 스케줄링
`mailbox_t::send()`에서 ASIO mode의 wakeup 조건을 분리하여,
**`_scheduled` 플래그가 false인 경우 항상 `post()`를 수행**하도록 변경 필요.

예시 방향:
- ASIO 모드: `if (!_scheduled.exchange(true)) post(process_mailbox);`
- Blocking 모드: `flush() == false`일 때만 condvar broadcast 유지

이렇게 하면:
- 최초 `send()`에서도 반드시 `process_mailbox()`가 실행됨
- double-check 로직으로 중복 실행 방지

### 2) mailbox 소멸 타이밍 검증
현재 `boost::asio::post([this] { process_mailbox(); })`가
mailbox 수명과 동기화되지 않으므로, shutdown 시점에 handler가 mailbox보다 늦게 실행되면 UAF 가능성이 있음.
- 이번 환경에서는 segfault 재현은 안 되었으나,
  다른 환경에서 `test_issue_566`, `test_proxy` segfault가 나올 수 있는 가능성이 높음
- 제안: poller stop 전에 mailbox handler queue를 drain하거나,
  mailbox에 `stop` 플래그를 두고 handler에서 guard 처리

## 테스트/검증 제안
수정 후 아래 테스트로 타임아웃 해소 확인:
- `./build/bin/test_pair_inproc`
- `./build/bin/test_issue_566`
- `./build/bin/test_proxy`
- `ctest --output-on-failure` (필요 시)

## 결론
- **타임아웃의 1차 원인은 ASIO mailbox의 `post()` 호출 조건이 ypipe의 “reader asleep” 의미에 의존하는 설계 불일치**
- ASIO mode에서는 poller 기반 reader가 없으므로 “reader asleep” 상태가 최초에 발생하지 않음 → wakeup 자체가 발생하지 않음
- `post()`를 `_scheduled` 기준으로 unconditional하게 예약하도록 조정하면 종료 hang 및 테스트 타임아웃이 해소될 가능성이 높음
