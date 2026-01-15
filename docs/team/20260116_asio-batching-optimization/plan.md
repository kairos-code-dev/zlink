# ASIO Batching 최적화 구현 계획 (Revised)

**Date:** 2026-01-16
**Author:** Codex (Planning Agent)
**Revised:** After Gemini critical review + Windows batching addition
**Target Files:**
- `src/asio/asio_poller.hpp`
- `src/asio/asio_poller.cpp`

**Target Platforms:** All (Linux, macOS, Windows)

---

## 중요: Gemini 리뷰 피드백 반영

### 수정된 문제점들

| 이슈 | 심각도 | 수정 내용 |
|------|--------|-----------|
| `in_event()` 의미 오해 | CRITICAL | void 반환, ZMQ 상태 머신 트리거 - raw I/O가 아님 |
| `handler_allocator` 부재 | CRITICAL | 현재 `poll_entry_t`에 없음 - 일반 lambda 사용 |
| `has_pending_data()` 이슈 | MAJOR | `#include <poll.h>` 누락, 루프 로직 오류 |
| 루프 로직 오류 | MAJOR | do-while → while로 수정 (첫 호출은 async_wait가 보장) |
| Windows도 배칭 필요 | CRITICAL | Windows WSAPoll도 FD당 1회만 in_event() 호출 - 배칭 필요 |
| 성능 기대치 | MODERATE | 73% → 30-50% (ZMQ 상태 머신 복잡성) |

---

## 1. 목표 (Goal) - 수정됨

### 성능 목표 (현실적 조정)

| 메트릭 | 현재 값 | 목표 값 | 개선률 |
|--------|---------|---------|--------|
| TCP 64B Throughput | 3.15 M/s | 4.10-4.72 M/s | **+30-50%** |
| libzmq 대비 성능 | -31% | -5% ~ +10% | 26-41%p 개선 |
| 이벤트당 오버헤드 | 394.1 ns | ~200 ns | ~50% |

**참고:** 마이크로벤치마크의 73% 개선은 raw I/O 환경에서 측정된 값입니다. 실제 ZMQ 환경에서는 상태 머신 처리, 메시지 인코딩/디코딩 등 추가 오버헤드로 인해 30-50% 개선이 현실적인 목표입니다.

### 핵심 근거

마이크로벤치마크 결과(`tests/microbench_epoll_vs_asio.cpp`)에서 ASIO batching 패턴이 기존 lambda 패턴 대비 성능 개선을 보임:

```
Raw epoll:        226.8 ns/event (baseline)
ASIO (lambda):    394.1 ns/event (+73.7%)  ← 현재 zlink 패턴
ASIO (batching):  104.5 ns/event (-53.9%)  ← 목표 패턴
```

**주의:** 이 개선은 **async_wait 재등록 횟수 감소**로 달성되나, ZMQ의 `in_event()`는 raw `read()` 호출이 아닌 상태 머신 트리거이므로 실제 개선률은 다를 수 있습니다.

---

## 2. 배경 (Background)

### 2.1 현재 구현 분석

현재 `asio_poller.cpp`의 `start_wait_read()` (라인 177-217):

```cpp
void zmq::asio_poller_t::start_wait_read (poll_entry_t *entry_)
{
    ASIO_DBG ("start_wait_read: fd=%d", entry_->fd);
#if defined ZMQ_HAVE_WINDOWS
    LIBZMQ_UNUSED (entry_);
    return;  // Windows uses WSAPoll in loop()
#else
    entry_->in_event_pending = true;
    entry_->descriptor.async_wait (
      boost::asio::posix::stream_descriptor::wait_read,
      [this, entry_] (const boost::system::error_code &ec) {  // ← 일반 lambda
          entry_->in_event_pending = false;
          // ... 에러 체크 ...
          entry_->events->in_event ();  // ← void 반환, 상태 머신 트리거
          // 매번 재등록
          if (entry_->pollin_enabled && entry_->fd != retired_fd && !_stopping) {
              start_wait_read (entry_);  // ← N번 이벤트 = N번 async_wait
          }
      });
#endif
}
```

### 2.2 in_event() 의미론 (Critical Understanding)

**`in_event()`는 void를 반환하며, raw I/O를 수행하지 않습니다.**

```cpp
// i_poll_events.hpp
struct i_poll_events
{
    virtual void in_event () = 0;  // ← void 반환
    virtual void out_event () = 0;
    virtual void timer_event (int id_) = 0;
};
```

`in_event()`가 하는 일:
1. ZMQ 엔진의 상태 머신 트리거
2. 수신 버퍼에서 메시지 파싱
3. 소켓 상태 업데이트
4. 필요 시 `reset_pollin()` 호출하여 더 이상 읽기 이벤트 불필요 표시

**마이크로벤치마크와의 차이:**
- 마이크로벤치마크: `read()` → EAGAIN까지 루프 (직접 raw I/O)
- ZMQ: `in_event()` → 상태 머신 트리거 (데이터 가용성 직접 확인 불가)

### 2.3 Windows 현황 - 배칭 필요 확인

**이전 오해:** Windows는 WSAPoll 루프에서 이미 배칭 형태로 동작한다고 생각했습니다.

**실제 코드 분석 (`asio_poller.cpp` 라인 349-366):**

```cpp
// Windows path in loop()
for (size_t i = 0; i < fds.size(); ++i) {
    if ((revents & POLLIN) != 0 && entry->pollin_enabled) {
        entry->events->in_event();  // ← Called ONCE per FD!
    }
    if ((revents & POLLOUT) != 0 && entry->pollout_enabled) {
        entry->events->out_event();  // ← Called ONCE per FD!
    }
}
```

**문제점:** Windows도 Unix와 동일한 문제가 있습니다:
- FD당 `in_event()` / `out_event()` 한 번만 호출
- 여러 메시지가 대기 중이어도 하나만 처리
- 다음 WSAPoll 호출까지 대기해야 다음 메시지 처리

**결론:** **Windows도 배칭 최적화가 필요합니다.** 본 최적화는 모든 플랫폼에 적용됩니다:
- **Unix/Linux/macOS**: `start_wait_read/write` 콜백에서 배칭
- **Windows**: `loop()` WSAPoll 결과 처리에서 배칭

---

## 3. 구현 전략 (Implementation Strategy) - 수정됨

### 3.1 핵심 변경 사항 (모든 플랫폼)

#### Unix/Linux/macOS: `start_wait_read/write()` 콜백에서

1. **첫 번째 `in_event()` 호출**: async_wait가 데이터 가용성을 보장
2. **추가 이벤트 배칭**: `poll()`로 데이터 가용성 확인 후 반복
3. **루프 종료 후 한 번만 async_wait 재등록**

#### Windows: `loop()` WSAPoll 결과 처리에서

1. **첫 번째 `in_event()` 호출**: WSAPoll이 데이터 가용성을 보장
2. **추가 이벤트 배칭**: `ioctlsocket(FIONREAD)` 또는 `select()`로 데이터 가용성 확인 후 반복
3. **다음 FD 처리로 진행**

### 3.2 올바른 루프 패턴

```cpp
// First call (data guaranteed by async_wait)
entry_->events->in_event();
batch_count++;

// Batch additional events if available
while (batch_count < max_batch
       && entry_->pollin_enabled
       && entry_->fd != retired_fd
       && !_stopping
       && has_pending_data(entry_)) {
    entry_->events->in_event();
    batch_count++;
}
```

**핵심 포인트:**
- 첫 번째 `in_event()` 호출은 루프 밖에서 (async_wait가 이미 데이터 보장)
- 이후 호출은 `has_pending_data()`로 확인 후 수행
- `do-while`이 아닌 `while` 사용 (조건 먼저 체크)

### 3.3 has_pending_data() 구현 (플랫폼별)

#### Unix/Linux/macOS 구현

```cpp
// asio_poller.cpp 상단에 추가
#if !defined ZMQ_HAVE_WINDOWS
#include <poll.h>
#endif

// 클래스 내 private 메서드로 추가
#if !defined ZMQ_HAVE_WINDOWS
bool zmq::asio_poller_t::has_pending_data (poll_entry_t *entry_)
{
    struct pollfd pfd;
    pfd.fd = entry_->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll (&pfd, 1, 0);  // timeout = 0 (즉시 반환)
    return (rc > 0 && (pfd.revents & POLLIN));
}
#endif
```

#### Windows 구현

```cpp
// Windows에서 데이터 가용성 확인
#if defined ZMQ_HAVE_WINDOWS
bool zmq::asio_poller_t::has_pending_data_win (fd_t fd_)
{
    //  Option 1: ioctlsocket with FIONREAD (preferred)
    u_long bytes_available = 0;
    int rc = ioctlsocket (fd_, FIONREAD, &bytes_available);
    return (rc == 0 && bytes_available > 0);
}

bool zmq::asio_poller_t::has_pending_write_space_win (fd_t fd_)
{
    //  Use select() with zero timeout to check writability
    fd_set write_fds;
    FD_ZERO (&write_fds);
    FD_SET (fd_, &write_fds);

    struct timeval tv = {0, 0};  // Zero timeout (non-blocking)
    int rc = select (0, NULL, &write_fds, NULL, &tv);
    return (rc > 0 && FD_ISSET (fd_, &write_fds));
}
#endif
```

**플랫폼별 주의사항:**

| 플랫폼 | 방식 | 참고 |
|--------|------|------|
| Unix/Linux/macOS | `poll()` timeout=0 | `#include <poll.h>` 필수 |
| Windows (read) | `ioctlsocket(FIONREAD)` | 바이트 수 확인, 빠름 |
| Windows (write) | `select()` timeout=0 | writability 확인 |

---

## 4. 구현 단계 (Implementation Steps) - 수정됨

### Phase 1: 기본 Batching 구현

**Step 1.1: `asio_poller.cpp` 상단에 include 추가**

```cpp
// asio_poller.cpp (기존 include 영역에 추가)
#if !defined ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <poll.h>  // ← 추가: has_pending_data()를 위해
#endif
```

**Step 1.2: `asio_poller.hpp`에 메서드 선언 추가**

```cpp
// asio_poller.hpp, private 섹션에 추가
    //  Batching helper methods
#if !defined ZMQ_HAVE_WINDOWS
    //  Check if more data is available for reading (non-blocking) - Unix
    bool has_pending_data (poll_entry_t *entry_);
    //  Check if socket is ready for more writing (non-blocking) - Unix
    bool has_pending_write_space (poll_entry_t *entry_);
#else
    //  Check if more data is available for reading (non-blocking) - Windows
    bool has_pending_data_win (fd_t fd_);
    //  Check if socket is ready for more writing (non-blocking) - Windows
    bool has_pending_write_space_win (fd_t fd_);
#endif
```

**Step 1.3: `has_pending_data()` 구현 (Unix)**

파일: `src/asio/asio_poller.cpp`

```cpp
#if !defined ZMQ_HAVE_WINDOWS
bool zmq::asio_poller_t::has_pending_data (poll_entry_t *entry_)
{
    //  Quick non-blocking check if more data is available
    struct pollfd pfd;
    pfd.fd = entry_->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll (&pfd, 1, 0);  // timeout = 0
    return (rc > 0 && (pfd.revents & POLLIN));
}

bool zmq::asio_poller_t::has_pending_write_space (poll_entry_t *entry_)
{
    //  Quick non-blocking check if socket is ready for writing
    struct pollfd pfd;
    pfd.fd = entry_->fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    int rc = poll (&pfd, 1, 0);  // timeout = 0
    return (rc > 0 && (pfd.revents & POLLOUT));
}
#endif
```

**Step 1.4: `has_pending_data_win()` 구현 (Windows)**

파일: `src/asio/asio_poller.cpp`

```cpp
#if defined ZMQ_HAVE_WINDOWS
bool zmq::asio_poller_t::has_pending_data_win (fd_t fd_)
{
    //  Use ioctlsocket with FIONREAD to check available bytes
    u_long bytes_available = 0;
    int rc = ioctlsocket (fd_, FIONREAD, &bytes_available);
    return (rc == 0 && bytes_available > 0);
}

bool zmq::asio_poller_t::has_pending_write_space_win (fd_t fd_)
{
    //  Use select() with zero timeout to check writability
    fd_set write_fds;
    FD_ZERO (&write_fds);
    FD_SET (fd_, &write_fds);

    struct timeval tv = {0, 0};  // Zero timeout (non-blocking)
    int rc = select (0, NULL, &write_fds, NULL, &tv);
    return (rc > 0 && FD_ISSET (fd_, &write_fds));
}
#endif
```

**Step 1.5: `start_wait_read()` 수정 (Unix)**

파일: `src/asio/asio_poller.cpp`, 라인 187-215 교체

```cpp
void zmq::asio_poller_t::start_wait_read (poll_entry_t *entry_)
{
    ASIO_DBG ("start_wait_read: fd=%d", entry_->fd);
#if defined ZMQ_HAVE_WINDOWS
    LIBZMQ_UNUSED (entry_);
    return;  // Windows uses WSAPoll batching in loop()
#else
    entry_->in_event_pending = true;
    entry_->descriptor.async_wait (
      boost::asio::posix::stream_descriptor::wait_read,
      [this, entry_] (const boost::system::error_code &ec) {
          entry_->in_event_pending = false;
          ASIO_DBG ("read callback: fd=%d, ec=%s, pollin_enabled=%d, stopping=%d",
                    entry_->fd, ec.message ().c_str (), entry_->pollin_enabled,
                    _stopping);

          //  Check if the entry has been retired or pollin disabled
          if (unlikely (ec || entry_->fd == retired_fd || !entry_->pollin_enabled
              || _stopping)) {
              ASIO_DBG ("read callback: returning early for fd=%d", entry_->fd);
              return;
          }

          //  === BATCHING OPTIMIZATION ===
          //  Process multiple events if available to reduce async_wait overhead
          static const int max_batch = 64;  // Fairness limit
          int batch_count = 0;

          //  First call: async_wait guarantees data is available
          ASIO_DBG ("read callback: calling in_event() for fd=%d [batch %d]",
                    entry_->fd, batch_count);
          entry_->events->in_event ();
          batch_count++;

          //  Batch additional events while data is available
          while (batch_count < max_batch
                 && entry_->pollin_enabled
                 && entry_->fd != retired_fd
                 && !_stopping
                 && has_pending_data (entry_)) {
              ASIO_DBG ("read callback: calling in_event() for fd=%d [batch %d]",
                        entry_->fd, batch_count);
              entry_->events->in_event ();
              batch_count++;
          }

          ASIO_DBG ("read callback: processed %d events for fd=%d",
                    batch_count, entry_->fd);
          //  === END BATCHING ===

          //  Re-register for read events if still enabled
          if (entry_->pollin_enabled && entry_->fd != retired_fd
              && !_stopping) {
              start_wait_read (entry_);
          } else {
              ASIO_DBG ("read callback: NOT re-registering for fd=%d (pollin=%d, fd=%d, stopping=%d)",
                        entry_->fd, entry_->pollin_enabled, entry_->fd, _stopping);
          }
      });
#endif
}
```

**Step 1.6: `start_wait_write()` 동일하게 수정 (Unix)**

파일: `src/asio/asio_poller.cpp`, 라인 229-250 교체

```cpp
void zmq::asio_poller_t::start_wait_write (poll_entry_t *entry_)
{
    ASIO_DBG ("start_wait_write: fd=%d", entry_->fd);
#if defined ZMQ_HAVE_WINDOWS
    LIBZMQ_UNUSED (entry_);
    return;  // Windows uses WSAPoll batching in loop()
#else
    entry_->out_event_pending = true;
    entry_->descriptor.async_wait (
      boost::asio::posix::stream_descriptor::wait_write,
      [this, entry_] (const boost::system::error_code &ec) {
          entry_->out_event_pending = false;
          ASIO_DBG ("write callback: fd=%d, ec=%s, pollout_enabled=%d, stopping=%d",
                    entry_->fd, ec.message ().c_str (), entry_->pollout_enabled,
                    _stopping);

          //  Check if the entry has been retired or pollout disabled
          if (unlikely (ec || entry_->fd == retired_fd || !entry_->pollout_enabled
              || _stopping)) {
              return;
          }

          //  === BATCHING OPTIMIZATION ===
          static const int max_batch = 64;
          int batch_count = 0;

          //  First call: async_wait guarantees socket is writable
          ASIO_DBG ("write callback: calling out_event() for fd=%d [batch %d]",
                    entry_->fd, batch_count);
          entry_->events->out_event ();
          batch_count++;

          //  Batch additional writes while socket is ready
          while (batch_count < max_batch
                 && entry_->pollout_enabled
                 && entry_->fd != retired_fd
                 && !_stopping
                 && has_pending_write_space (entry_)) {
              ASIO_DBG ("write callback: calling out_event() for fd=%d [batch %d]",
                        entry_->fd, batch_count);
              entry_->events->out_event ();
              batch_count++;
          }

          ASIO_DBG ("write callback: processed %d events for fd=%d",
                    batch_count, entry_->fd);
          //  === END BATCHING ===

          //  Re-register for write events if still enabled
          if (entry_->pollout_enabled && entry_->fd != retired_fd
              && !_stopping) {
              start_wait_write (entry_);
          }
      });
#endif
}
```

**Step 1.7: Windows `loop()` WSAPoll 결과 처리에 배칭 추가**

파일: `src/asio/asio_poller.cpp`, 라인 349-366 (Windows `loop()` 내부) 수정

현재 코드:
```cpp
// Current Windows implementation (lines 349-366)
for (size_t i = 0; i < fds.size(); ++i) {
    if ((revents & POLLIN) != 0 && entry->pollin_enabled) {
        entry->events->in_event();  // ← Called ONCE per FD
    }
    if ((revents & POLLOUT) != 0 && entry->pollout_enabled) {
        entry->events->out_event();  // ← Called ONCE per FD
    }
}
```

수정된 코드:
```cpp
// Windows batching implementation
for (size_t i = 0; i < fds.size(); ++i) {
    poll_entry_t *entry = entries[i];
    if (entry == NULL || entry->fd == retired_fd)
        continue;

    short revents = fds[i].revents;
    if (revents == 0)
        continue;

    //  === POLLIN BATCHING ===
    if ((revents & (POLLIN | POLLERR | POLLHUP)) != 0 && entry->pollin_enabled) {
        static const int max_batch = 64;
        int batch_count = 0;

        //  First call: WSAPoll guarantees data is available
        entry->events->in_event ();
        batch_count++;

        //  Batch additional events while data is available
        while (batch_count < max_batch
               && entry->pollin_enabled
               && entry->fd != retired_fd
               && !_stopping
               && has_pending_data_win (entry->fd)) {
            entry->events->in_event ();
            batch_count++;
        }
    }

    //  === POLLOUT BATCHING ===
    if ((revents & POLLOUT) != 0 && entry->pollout_enabled) {
        static const int max_batch = 64;
        int batch_count = 0;

        //  First call: WSAPoll guarantees socket is writable
        entry->events->out_event ();
        batch_count++;

        //  Batch additional writes while socket is ready
        while (batch_count < max_batch
               && entry->pollout_enabled
               && entry->fd != retired_fd
               && !_stopping
               && has_pending_write_space_win (entry->fd)) {
            entry->events->out_event ();
            batch_count++;
        }
    }
}
```

**핵심 포인트 (Windows):**
- `has_pending_data_win()`: `ioctlsocket(FIONREAD)`로 대기 바이트 확인
- `has_pending_write_space_win()`: `select()` timeout=0으로 writability 확인
- Unix와 동일한 max_batch=64 제한
- 동일한 종료 조건 (pollin_enabled, retired_fd, _stopping)

### Phase 2: 테스트 및 검증

**Step 2.1: 빌드 및 기존 테스트 실행**

```bash
# 빌드
./build-scripts/linux/build.sh x64 ON

# 테스트 실행
cd build/linux-x64 && ctest --output-on-failure
```

**Step 2.2: 벤치마크 실행**

```bash
# 마이크로벤치마크
./build/bin/microbench_epoll_vs_asio

# 전체 벤치마크
cd benchwithzmq && ./run_benchmarks.sh
```

### Phase 3: 성능 튜닝 (선택적)

**Step 3.1: max_batch 값 튜닝**

초기값 64로 시작, 벤치마크 결과에 따라 조정:
- 너무 낮으면 배칭 효과 감소
- 너무 높으면 다른 FD starvation 가능

```cpp
// 튜닝 범위: 16-256
static const int max_batch = 64;
```

---

## 5. 검증 계획 (Validation Plan)

### 5.1 기능 검증

| 테스트 | 목적 | 통과 기준 |
|--------|------|-----------|
| 전체 테스트 스위트 | 기존 기능 유지 | 64/64 통과 |
| transport_matrix | 모든 패턴/전송 | 전체 통과 |
| stress test | 고부하 안정성 | 메모리 누수 없음 |

### 5.2 성능 검증 (현실적 목표)

| 메트릭 | 측정 방법 | 목표 |
|--------|-----------|------|
| TCP 64B 처리량 | bench_pair, bench_router | **+30-50%** (4.1-4.7 M/s) |
| 이벤트당 지연 | microbench_epoll_vs_asio | ~200 ns |
| libzmq 대비 | comparison benchmarks | -5% ~ +10% |

---

## 6. 위험 분석 (Risk Analysis)

### 6.1 기술적 위험

| 위험 | 심각도 | 가능성 | 완화 방안 |
|------|--------|--------|-----------|
| poll() 시스템 콜 오버헤드 | 중간 | 중간 | 배칭 효과가 오버헤드보다 큼 (검증 필요) |
| 무한 루프 | 높음 | 낮음 | max_batch 제한 |
| Starvation | 중간 | 중간 | max_batch 튜닝 |
| 기존 동작 변경 | 중간 | 낮음 | 광범위한 테스트 |

### 6.2 poll() 오버헤드 분석

`has_pending_data()`의 `poll()` 호출 오버헤드:
- 시스템 콜: ~50-100ns
- 배칭으로 절약되는 async_wait 오버헤드: ~200-300ns

**결론:** 배칭이 효과적이려면 평균 배치 크기가 2 이상이어야 함. 고처리량 시나리오에서는 충족될 것으로 예상.

### 6.3 롤백 계획

문제 발생 시:

1. **즉시 롤백:** git revert
2. **부분 롤백:** `max_batch = 1`로 설정하여 기존 동작과 동일하게
3. **기능 플래그:** 환경변수로 배칭 활성화/비활성화

---

## 7. 플랫폼별 적용 범위 - 모든 플랫폼 포함

**중요 수정:** Windows도 배칭 최적화가 필요합니다. 기존 분석 오류 수정.

| 플랫폼 | 배칭 적용 | 구현 위치 | 데이터 확인 방법 |
|--------|-----------|-----------|------------------|
| Linux x64/ARM64 | **O** | `start_wait_read/write` 콜백 | `poll()` timeout=0 |
| macOS x64/ARM64 | **O** | `start_wait_read/write` 콜백 | `poll()` timeout=0 |
| Windows x64/ARM64 | **O** | `loop()` WSAPoll 결과 처리 | `ioctlsocket(FIONREAD)`, `select()` |

### 플랫폼별 구현 차이점

| 구분 | Unix/Linux/macOS | Windows |
|------|------------------|---------|
| 이벤트 감지 | ASIO async_wait | WSAPoll |
| 배칭 위치 | async_wait 콜백 내부 | loop() FD 처리 루프 내부 |
| 읽기 확인 | `poll(fd, POLLIN, 0)` | `ioctlsocket(fd, FIONREAD)` |
| 쓰기 확인 | `poll(fd, POLLOUT, 0)` | `select(fd, NULL, &write_fds, NULL, 0)` |
| 재등록 | `start_wait_read/write()` 호출 | 자동 (WSAPoll 루프) |

### 예상 성능 개선 (모든 플랫폼)

| 플랫폼 | 현재 처리량 | 목표 처리량 | 개선률 |
|--------|-------------|-------------|--------|
| Linux | 3.15 M/s | 4.1-4.7 M/s | **+30-50%** |
| macOS | ~3.0 M/s | ~4.0-4.5 M/s | **+30-50%** |
| Windows | ~2.8 M/s | ~3.6-4.2 M/s | **+30-50%** |

---

## 8. 일정 계획 (Timeline)

| 단계 | 예상 소요 | 산출물 |
|------|-----------|--------|
| Phase 1a: Unix 구현 | 1-2시간 | Unix/Linux/macOS batching 코드 |
| Phase 1b: Windows 구현 | 1시간 | Windows batching 코드 |
| Phase 2: 테스트 | 1-2시간 | 모든 플랫폼 테스트 통과 확인 |
| Phase 3: 벤치마크 | 1시간 | 플랫폼별 성능 측정 결과 |
| **총 예상 시간** | **4-6시간** | 모든 플랫폼 배칭 완료 |

---

## 9. 결론 (Conclusion)

ASIO batching 최적화는 **모든 플랫폼**(Unix/Linux/macOS/Windows)에서 이벤트 처리 효율을 높여 **30-50% 성능 개선**을 목표로 합니다.

**핵심 변경사항:**

| 플랫폼 | 변경 위치 | 변경 내용 |
|--------|-----------|-----------|
| Unix/Linux/macOS | `start_wait_read()` | async_wait 재등록 횟수 감소, `poll()` 기반 배칭 |
| Unix/Linux/macOS | `start_wait_write()` | 동일한 배치 패턴 적용 |
| Windows | `loop()` WSAPoll 처리 | FD당 다중 이벤트 배치 처리 |
| 공통 | `has_pending_data[_win]()` | 플랫폼별 데이터 가용성 확인 |

**플랫폼별 구현:**
1. **Unix/Linux/macOS**: async_wait 콜백에서 `poll()` timeout=0으로 추가 데이터 확인 후 배치
2. **Windows**: WSAPoll 결과 루프에서 `ioctlsocket(FIONREAD)`로 추가 데이터 확인 후 배치

**예상 결과 (현실적):**
- **모든 플랫폼**: +30-50% 처리량 개선
- TCP 64B: 3.15 M/s → 4.1-4.7 M/s
- libzmq 대비: -31% → -5% ~ +10%

**주의사항:**
- `handler_allocator` 사용 안 함 (현재 `poll_entry_t`에 없음)
- 일반 lambda 사용 (현재 패턴 유지)
- `in_event()`는 void 반환 - raw I/O 결과가 아닌 상태 머신 트리거
- max_batch=64로 starvation 방지

---

**Reviewed by:** Gemini (Critical Review)
**Revised by:** User (Windows 배칭 필요성 발견)
**Approved by:** [Pending]
**Implementation:** dev-cxx agent
