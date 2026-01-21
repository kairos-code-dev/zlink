# ASIO 기반 poller 완전 통합 계획

## 1) 배경/문제
- zlink는 I/O thread가 ASIO 기반으로 통합되어 있으나, `zmq_poll()`/`zmq_poller_*`는 여전히 `poll/select` 경로에 의존합니다.
- Windows는 `poll()`을 `WSAPoll()`로 매핑하는 shim이 남아 있어 코드 경로가 분기됩니다.
- “ASIO-only” 목표를 완성하려면 `zmq_poll()`까지 ASIO 경로로 통합해야 합니다.

## 2) 현재 구조 요약
- I/O thread: `src/asio/asio_poller.*` (ASIO)
- 사용자 API poll 경로:
  - `src/zmq.cpp`의 `zmq_poll()` → `poll/select` + `ZMQ_FD`
  - `src/socket_poller.*` → `poll/select` + signaler
  - `src/windows.hpp` → Windows `poll()` shim(`WSAPoll`)
- CMake: `API_POLLER`, `ZMQ_POLL_BASED_ON_*` 매크로로 분기

### 2.1) 현재 동작 상세 (리뷰 반영)
- `zmq_poll()`은 `ZMQ_FD`를 우선 조회하고, `ZMQ_FD`가 `EINVAL`이면
  fd 없이 최대 10ms 간격으로 깨우며 `ZMQ_EVENTS`를 재검사한다.
- `socket_poller_t`는 thread-safe 소켓이 포함되면 `signaler_t`를 사용해
  poll/select 대상에 “깨움” fd를 추가하고, 깨어난 뒤 `ZMQ_EVENTS`를
  다시 조회한다.
- `ZMQ_FD`는 실제 소켓 fd가 아니라 mailbox/signaler 기반 wakeup fd이며,
  `socket_base_t::getsockopt(ZMQ_FD)`에서 지연 생성된다.
- `ZMQ_EVENTS`는 `process_commands(0, false)` 이후 계산되므로,
  새 경로도 동일한 시맨틱을 유지해야 한다.

### 2.2) 참고 소스 위치
- `src/zmq.cpp` (`zmq_poll`, 10ms fallback, `ZMQ_FD`/`ZMQ_EVENTS` 경로)
- `src/socket_poller.cpp`, `src/socket_poller.hpp` (`zmq_poller_*`)
- `src/polling_util.cpp`, `src/polling_util.hpp` (timeout 계산)
- `src/socket_base.cpp`, `src/socket_base.hpp` (`ZMQ_FD`, `ZMQ_EVENTS`,
  `add_signaler/remove_signaler`)
- `src/mailbox.cpp`, `src/mailbox.hpp`, `src/mailbox_safe.*` (signaler 등록)
- `src/signaler.cpp`, `src/signaler.hpp` (wakeup fd 구현)
- `src/windows.hpp` (WSAPoll shim)
- `src/proxy.cpp` (socket_poller_t 사용)
- `src/poller.hpp`, `src/asio/asio_poller.*` (I/O thread poller)
- 테스트: `tests/test_disconnect_inproc.cpp`, `tests/test_spec_router.cpp`,
  `tests/test_proxy.cpp`, `tests/test_ctx_destroy.cpp`

## 3) 목표/범위
### 목표
- `zmq_poll()`과 `zmq_poller_*`를 ASIO 기반으로 재구현하여 **poll/select 경로 제거**
- Windows 전용 `WSAPoll` shim 제거
- `API_POLLER`/`ZMQ_POLL_BASED_ON_*` 관련 옵션 및 분기 제거
- 동작은 “socket-only poll”로 단순화 (raw fd 지원 제거)

### 비범위
- 외부 이벤트 루프/파일 디스크립터 혼합 폴링 지원
- API 호환성 유지(이 프로젝트는 “우리만의 길”을 전제로 함)

## 4) 핵심 결정(정책)
- `zmq_poll()`은 **zlink 소켓만 허용** (raw fd는 EINVAL 처리)
- 블로킹 동작(타임아웃 포함)은 유지
- I/O thread는 기존 ASIO 구조 유지
- `zmq_pollitem_t`에 `socket`과 `fd`가 동시에 있을 경우 `EINVAL` 처리

## 5) 목표 아키텍처(개념)
```
app thread
  -> zmq_poll()
     -> socket-only poller 대기 (ASIO 기반 notifier)
     -> ZMQ_EVENTS 재검사 후 revents 채움 (level-triggered)

I/O thread (ASIO)
  -> 소켓 상태 변화 시 poller에 notify
```

핵심은 “큐 상태 변화 → poller 깨움” 경로를 **FD 기반에서 ASIO 기반 notifier로 전환**하는 것.

## 6) 상세 설계 항목
### 6.1 이벤트 소스 정의
- ZMQ 내부 큐 상태 변화(수신 가능/송신 가능)를 “event”로 정의
- 변경점이 있을 때 poller에 **단일 wakeup** 전달
- poller 진입 시 `ZMQ_EVENTS`를 먼저 검사하여 즉시 ready 케이스는
  불필요한 대기를 피한다

### 6.2 깨움(wakeup) 메커니즘
- 옵션 A: `condition_variable` + **epoch 카운터**
  - 상태 변화마다 epoch++
  - poller는 `epoch` 변화 또는 timeout까지 대기
  - wakeup은 “깨움”만 담당하고 `ZMQ_EVENTS`는 항상 재검사
  - epoch는 atomic으로 관리하고 메모리 오더를 명확히 한다
- 옵션 B: ASIO `io_context` + `steady_timer`/`post` 기반 신호
  - poller는 내부 io_context에서 `run_one()` 형태로 대기
  - 상태 변화 시 `post()`로 즉시 깨움
  - io_context 소유 스레드와 재진입 정책을 명확히 한다

### 6.3 타임아웃 정확도
- 타임아웃은 wall-clock 기준으로 계산
- spurious wakeup 후 반드시 이벤트 재검사

### 6.4 소켓 등록/해제 동기화
- poller 등록/해제 시 epoch 증가
- 소켓 제거 중 대기 중인 poller를 깨워 안전하게 재검사
- 다중 poller 등록을 허용할 경우, 소켓은 poller notifier 목록을
  약한 참조로 관리하고 소켓 close/ctx_term 시 모두 해제한다

### 6.5 스레드 안전성
- I/O thread → app thread로 신호 전달 시 락 순서 정의
- poller 내부 락은 짧게 유지(스캔 후 즉시 해제)
- `process_commands(0, false)` 호출 지점을 명확히 하여 이벤트 시맨틱을
  기존과 동일하게 유지한다

## 7) 단계별 실행 계획
### Phase 0: 정리(리스트업)
- `poll/select` 의존 파일/매크로 목록화
  - `src/zmq.cpp`, `src/socket_poller.*`, `src/polling_util.*`, `src/windows.hpp`
  - `CMakeLists.txt`의 `API_POLLER`, `ZMQ_POLL_BASED_ON_*`
- 현재 `zmq_poll`/`zmq_poller_*` 성능 기준(지연/처리량/오버헤드) 확보

### Phase 1: 정책 적용(소켓 전용)
- `zmq_poll()`에서 raw fd 입력을 `EINVAL` 처리
- `zmq_poller_add_fd/modify_fd/remove_fd` 제거 또는 `EINVAL` 처리
- 문서에 “socket-only” 정책 명시
  - `include/zmq.h`, `README.md`에 raw fd 미지원 명시
  - `ZMQ_FD` 정책(유지/제거/경고)과 호환성 영향 명시

### Phase 2: ASIO 기반 poller 구현
- 새로운 `asio_socket_poller_t`(가칭) 구현
  - notifier + timeout + revents 계산
  - `socket_base_t` 상태 변경 시 notify 훅 추가
  - 기존 `signaler_t` 경로 재사용 여부 검토
  - `proxy.cpp`의 `socket_poller_t` 사용 경로 대체
- 기존 `socket_poller_t` 대체

### Phase 3: poll/select 경로 제거
- `poll/select` 관련 코드/매크로 정리
- `src/windows.hpp`의 `WSAPoll` shim 제거
- `API_POLLER`/`ZMQ_POLL_BASED_ON_*` 제거

### Phase 4: 테스트 보강
- socket-only `zmq_poll` 동작 테스트
- 타임아웃/동시성/소켓 제거 중 대기 테스트
- 기존 test suite + 신규 unit test
  - `tests/test_disconnect_inproc.cpp`, `tests/test_spec_router.cpp`,
    `tests/test_proxy.cpp`, `tests/test_ctx_destroy.cpp`
  - `zmq_poll(items=0)` 타임아웃/즉시 반환
  - raw fd 입력 시 `EINVAL`
  - spurious wakeup 재검사
  - 대기 중 add/modify/remove 경쟁

### Phase 5: 벤치/성능 기준
- `ROUTER_ROUTER_POLL` 패턴 중심으로 회귀 확인
- 지연/처리량이 기존 대비 ±5% 이내인지 확인
- poller 등록/해제 오버헤드와 다중 소켓 스캔 비용 측정

## 8) 테스트 전략
- 기능 테스트: `zmq_poll` 동작, 타임아웃, 다중 소켓
- 경합 테스트: 소켓 close/unregister 중 `zmq_poll` 대기
- 회귀 테스트: 기존 `tests/` 전부 통과
- 벤치: `benchwithzmq`의 poll 패턴 포함
- 추가: `zmq_poll` 즉시 ready, 다중 poller 등록, `zmq_poller_wait_all`

## 9) 성공 기준
- 빌드/테스트 100% 통과(기존 스킵 제외)
- `zmq_poll` socket-only 동작 안정
- poll/select 분기 제거로 코드 단순화
- Windows에서 WSAPoll shim 제거

## 10) 문서/정책 업데이트
- `README.md`/`CLAUDE.md`/`include/zmq.h`에 socket-only 정책 명시
- `docs/`에 변경 이력 및 마이그레이션 노트 추가

## 11) 리스크/미결정/가정
- notifier 선택(옵션 A/B) 기준과 결정 시점 명시 필요
- `ZMQ_FD` 정책 변경은 API breaking 가능성이 있으므로 마이그레이션 노트 필수
- 다중 poller 등록/해제의 수명 관리와 락 순서가 핵심 리스크
- ctx_term/close 시 대기 중 poller를 확실히 깨우는 경로 보장 필요

## 12) 롤백 전략
- Phase별 커밋 분리
- Phase 2 이후 문제 시 poller 구현만 롤백 가능하도록 분리
