# Phase 1 ASIO 통합: Mutex EINVAL 에러 디버깅 요청

## 현재 상황

Phase 1 mailbox 구현 후 모든 테스트가 실패하고 있으며, 간단한 테스트에서도 mutex EINVAL 에러 발생.

## 이전 수정 사항

1. **Codex의 1차 진단 결과 적용**:
   - `mailbox.cpp:send()`에서 ASIO mode 시 `flush()` 결과 무시하고 무조건 `post()` 호출하도록 수정
   - 하지만 여전히 실패

2. **UAF (Use-After-Free) 문제 수정**:
   - Codex가 경고한 대로 `mailbox_t` 소멸자에서 `_scheduled` flag 대기 로직 추가
   - 하지만 여전히 mutex 에러 발생

## 에러 로그

```
Starting minimal test...
Context created: 0x63892cca12f0
[DEBUG] reaper_t constructor called
[DEBUG] reaper_t: about to call set_io_context
[DEBUG] set_io_context() called, this=0x63892cca4aa0, io_ctx=0x63892cca5110
[DEBUG] reaper_t: set_io_context done
[DEBUG] set_io_context() called, this=0x63892cca5440, io_ctx=0x63892cca5a90
Socket created: 0x63892cca5dc0
[DEBUG] send() ASIO mode: this=0x63892cca4aa0, already_scheduled=0
[DEBUG] send() posting process_mailbox, this=0x63892cca4aa0
Socket closed
[DEBUG] process_mailbox() called, this=0x63892cca4aa0
[DEBUG] send() ASIO mode: this=0x63892cca4aa0, already_scheduled=1
[DEBUG] send() ASIO mode: this=0x63892cca4aa0, already_scheduled=1
Invalid argument (/home/ulalax/project/ulalax/zlink/src/mutex.hpp:109)
```

mutex.hpp:109는 `pthread_mutex_lock()` 호출 부분으로, EINVAL 에러 발생.

## 분석 포인트

1. `process_mailbox()` 실행 중 두 번의 `send()` 호출 발생 (`already_scheduled=1`)
2. 그 직후 mutex EINVAL 에러
3. 어떤 mutex가 문제인지 불명확 (mailbox의 `_sync`인지, 다른 객체의 mutex인지)

## 디버깅 요청

다음 사항을 GDB로 조사해 주세요:

1. **Mutex 에러 위치 정확히 특정**:
   - EINVAL을 발생시키는 정확한 코드 위치 (stack trace)
   - 어떤 객체의 mutex가 문제인지

2. **명령 처리 플로우**:
   - `process_mailbox()`가 처리하는 명령들이 무엇인지
   - 각 명령이 어떤 작업을 수행하는지
   - 어느 명령 처리 중에 에러가 발생하는지

3. **객체 수명 문제**:
   - 에러 발생 시점에 관련 객체들(mailbox, reaper, socket 등)이 유효한 상태인지
   - 소멸 중인 객체가 있는지

4. **스레딩 이슈**:
   - 어떤 스레드에서 에러가 발생하는지
   - ASIO io_context 스레드 vs 메인 스레드

5. **수정 제안**:
   - 근본 원인 파악 후 수정 방안 제시

## 테스트 파일

`/home/ulalax/project/ulalax/zlink/test_mailbox_debug.cpp`

실행 방법:
```bash
cd /home/ulalax/project/ulalax/zlink
g++ -g -o test_mailbox_debug test_mailbox_debug.cpp -I./include -L./build/lib -lzmq -lpthread -lstdc++
LD_LIBRARY_PATH=./build/lib gdb -q ./test_mailbox_debug
```

GDB 명령:
```
set pagination off
catch throw
run
bt
info threads
thread apply all bt
```

결과를 `docs/team/20260116_full-asio-integration/debug2_codex.md`에 작성해 주세요.
