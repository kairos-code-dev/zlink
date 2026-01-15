# ASIO 배칭 최적화 실험 결과

## 날짜
2026-01-16

## 실험 목표
ASIO poller의 콜백에서 이벤트를 배칭 처리하여 handler dispatch overhead를 줄이고 성능을 향상시킴

**목표 성능**:
- TCP 64B: 3.15 M/s → 4.10-4.72 M/s (+30-50%)
- Micro-benchmark에서 73% 성능 향상 확인됨

## 실험 가설

### 가설 1: ASIO Handler Dispatch Overhead
- ASIO는 Proactor 패턴으로 epoll을 추상화
- 각 `async_wait()` 호출마다 handler dispatch overhead 발생 (~167ns/event)
- Micro-benchmark 검증:
  - Raw epoll: 226.8 ns/event (baseline)
  - ASIO lambda: 394.1 ns/event (+73.7%)
  - ASIO batching: 104.5 ns/event (-53.9% vs raw epoll, **73.5% improvement**)

### 가설 2: 배칭으로 Overhead 상쇄 가능
- `async_wait()` 콜백에서 첫 `in_event()` 호출 보장됨
- 추가로 `has_pending_data()` 체크하며 반복 호출
- 한 번의 handler dispatch로 여러 이벤트 처리

## Micro-benchmark 재검증 (공정한 비교)

### 문제점 발견
초기 micro-benchmark는 불공정한 비교였음:
- Raw epoll: 한 번에 1개 이벤트 처리 (배칭 없음)
- ASIO batching: EAGAIN까지 모든 이벤트 배칭 처리

실제 ZMQ는 `in_event()`에서 이미 EAGAIN까지 배칭하므로, **공정한 비교를 위해 raw epoll에도 동일한 배칭 추가**

### 업데이트된 테스트 (epoll batching 추가)

**위치**: `tests/microbench_epoll_vs_asio.cpp`

```cpp
// Raw epoll with batching (EAGAIN pattern - mimics in_event())
double benchmark_raw_epoll_batching() {
    // ... epoll setup ...

    while (events_processed < NUM_EVENTS) {
        int n = epoll_wait(epoll_fd, events, 1, -1);

        if (n > 0) {
            // Batch read: read ALL until EAGAIN (like in_event() does)
            while (true) {
                ssize_t bytes = read(fds[0], &byte, 1);
                if (bytes == 1) {
                    events_processed++;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // No more data
                }
            }
        }
    }
}
```

### 🎯 재검증 결과 (2026-01-16)

```
Events: 10,000

** WITHOUT BATCHING **
Raw epoll (no batch):    233.4 ns/event (baseline)
ASIO (lambda):            401.4 ns/event (+72.0%)

ASIO abstraction overhead: +168.0 ns/event (+72.0%)

** WITH BATCHING (EAGAIN pattern) **
Raw epoll (batching):     106.2 ns/event (-54.5%)
ASIO (batching):          106.0 ns/event (-54.6%)

ASIO abstraction overhead: -0.15 ns/event (-0.14%)
```

### 🔥 핵심 발견

**배칭 없이**:
- ASIO 추상화 오버헤드: **+168 ns/event (+72%)**
- 이것이 초기 가설의 근거였음

**배칭 적용 시 (공정한 비교)**:
- ASIO 추상화 오버헤드: **-0.15 ns/event (-0.14%)**
- **오버헤드가 사실상 0%로 사라짐!** ✨

**성능 향상**:
- Raw epoll: 233.4 ns → 106.2 ns (**-54.5%**)
- ASIO: 401.4 ns → 106.0 ns (**-73.6%**)
- 배칭한 ASIO가 배칭 안 한 raw epoll보다 **54% 빠름**

### 💡 결론: ASIO 추상화는 성능 저하 원인이 아님

**이전 오해**:
- "ASIO의 Proactor 추상화가 72% 오버헤드를 만든다"

**진실**:
- **배칭을 하면 ASIO 오버헤드는 0%** (106.0 vs 106.2 ns)
- 초기 비교가 불공정했음 (epoll 배칭 없음 vs ASIO 배칭)
- **ASIO 자체는 문제가 아님**

**그렇다면 왜 zlink는 느린가?**
- ZMQ는 **이미** `in_event()`에서 EAGAIN까지 배칭 수행
- 우리가 추가한 외부 배칭 = 중복 + syscall overhead
- 성능 차이의 진짜 원인은 다른 곳 (zero-copy, 메모리 할당, 캐시 등)

## 구현 방법

### Unix (Linux/macOS)
**위치**: `src/asio/asio_poller.cpp:185-236` (read), `246-292` (write)

```cpp
// start_wait_read() 콜백에서
entry_->descriptor.async_wait([this, entry_] (const boost::system::error_code &ec) {
    // First in_event() call (async_wait guarantees data available)
    entry_->events->in_event();
    batch_count++;

    // Batch additional events
    while (batch_count < max_batch
           && has_pending_data(entry_)) {
        entry_->events->in_event();
        batch_count++;
    }
});

// has_pending_data() 구현
bool has_pending_data(poll_entry_t *entry_) {
    struct pollfd pfd;
    pfd.fd = entry_->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll(&pfd, 1, 0);  // timeout=0 (non-blocking)
    return (rc > 0 && (pfd.revents & POLLIN));
}
```

### Windows
**위치**: `src/asio/asio_poller.cpp:441-485`

```cpp
// WSAPoll() 결과 처리 루프에서
if ((revents & (POLLIN | POLLERR | POLLHUP)) != 0 && entry->pollin_enabled) {
    // First in_event() call (WSAPoll guarantees data available)
    entry->events->in_event();
    batch_count++;

    // Batch additional events
    while (batch_count < max_batch
           && has_pending_data_win(entry->fd)) {
        entry->events->in_event();
        batch_count++;
    }
}

// has_pending_data_win() 구현
bool has_pending_data_win(fd_t fd_) {
    u_long bytes_available = 0;
    int rc = ioctlsocket(fd_, FIONREAD, &bytes_available);
    return (rc == 0 && bytes_available > 0);
}
```

**설정**: `max_batch = 64`

## 실험 결과

### 빌드 및 테스트
- ✅ 빌드: 성공
- ✅ 테스트: 56/56 통과

### 벤치마크 결과 (DEALER_ROUTER, TCP 64B)

| 구분 | Throughput | vs libzmq | 개선폭 |
|------|-----------|-----------|--------|
| libzmq (baseline) | 4.61 M/s | - | - |
| zlink (배칭 전) | ~3.15 M/s | -31% | - |
| zlink (배칭 후) | **3.21 M/s** | **-30.3%** | **+1.9%** |

**결과**: 거의 효과 없음 (목표 +30-50% 대비 실제 +1.9%)

### 전체 Transport 결과

**TCP**:
- 64B: 3.21 M/s (-30.3%) [이전: 3.15 M/s, -31%]
- 256B: 2.02 M/s (-37.9%)
- 1024B: 0.82 M/s (-40.4%)

**Inproc**:
- 64B: 4.91 M/s (-33.1%)
- 256B: 4.26 M/s (-43.4%)
- 1024B: 2.77 M/s (-41.4%)

**IPC**:
- 64B: 3.24 M/s (-33.2%)
- 256B: 2.28 M/s (-37.2%)
- 1024B: 0.93 M/s (-43.3%)

## 실패 원인 분석

### Codex 코드 리뷰 결과
**위치**: `docs/team/20260116_asio-batching-optimization/review.md`

#### 1. 추가 Syscall Overhead
- 배칭 루프마다 `poll()`/`ioctlsocket()`/`select()` syscall 호출
- 메시지 처리 비용보다 readiness 체크 오버헤드가 더 큼
- 이득이 오버헤드에 의해 상쇄됨

#### 2. **in_event() 내부 배칭 중복 (결정적 원인)**
- `in_event()`가 이미 내부에서 "가능한 만큼 처리"
- 외부에서 반복 호출해도 실효 없음
- Micro-benchmark(작은 고정 payload)에서는 효과 있었지만 실제 워크로드에서는 효과 없음

#### 3. 배칭 기회 부족
- 실제 벤치마크에서 연속 이벤트가 충분하지 않음
- `max_batch=64`여도 실제 배치 길이가 작음

### 결정적 증거: in_event()는 이미 배칭 수행

**위치**: `src/io_thread.cpp:54-69`

```cpp
void zmq::io_thread_t::in_event()
{
    //  TODO: Do we want to limit number of commands I/O thread can
    //  process in a single go?

    command_t cmd;
    int rc = _mailbox.recv(&cmd, 0);

    while (rc == 0 || errno == EINTR) {
        if (rc == 0)
            cmd.destination->process_command(cmd);
        rc = _mailbox.recv(&cmd, 0);  // 계속 읽음
    }

    errno_assert(rc != 0 && errno == EAGAIN);  // EAGAIN까지 처리!
}
```

**핵심**:
- `in_event()`는 단일 이벤트만 처리하는 것이 아님
- **EAGAIN이 나올 때까지 while 루프로 모든 메시지를 처리**
- ZMQ의 설계 철학: "한 번 호출되면 가능한 모든 것을 처리"

### 중복 배칭 구조

```
asio_poller (우리가 추가한 배칭)
  └── while (has_pending_data())  // 외부 배칭 루프 + syscall overhead
       └── in_event()
            └── while (EAGAIN)  // 내부 배칭 루프 (이미 존재!)
                 └── process all messages
```

**실제 동작**:
1. 첫 `in_event()` 호출 → 내부에서 모든 데이터 처리 (EAGAIN까지)
2. 두 번째 `has_pending_data()` → 항상 false (모든 데이터가 이미 처리됨)
3. 배치 루프는 항상 1회만 실행
4. `has_pending_data()`의 syscall overhead만 추가됨

## Micro-benchmark와 실제 차이

### Micro-benchmark에서 효과가 있었던 이유
- 단순한 read/write 테스트로 `in_event()` 내부 로직을 우회
- 실제 ZMQ의 복잡한 메시지 처리 파이프라인과 다름
- 추상화 오버헤드만 측정했지만 실제 메시지 처리는 다른 구조

### 실제 워크로드와의 차이
- ZMQ는 이미 각 이벤트 핸들러에서 최적화된 배칭 수행
- `io_thread_t::in_event()`: 명령 처리 배칭
- 엔진/세션 레벨: 메시지 처리 배칭
- 외부에서 추가 배칭 = 중복 체크 + syscall overhead

## 결론

### ❌ 실험 실패
1. **성능 개선 없음**: +1.9% (목표 +30-50%)
2. **근본 원인**: `in_event()`가 이미 내부 배칭 수행
3. **부작용**: 불필요한 syscall overhead 추가

### ✅ 학습 내용

#### 1. ASIO 추상화는 성능 문제가 아님
- **초기 가정**: "ASIO Proactor 추상화가 72% 오버헤드" ❌
- **재검증 결과**: 배칭 시 ASIO 오버헤드는 0.14% (사실상 0%) ✅
- **핵심**: 공정한 비교 필요 (둘 다 배칭 or 둘 다 배칭 없음)

#### 2. ZMQ 아키텍처 이해
- 각 이벤트 핸들러는 이미 EAGAIN까지 처리
- "한 번 호출되면 가능한 모든 것을 처리" 철학
- 외부 최적화 시도 전 내부 구현 확인 필수
- **우리 배칭 = 중복 배칭 + syscall overhead**

#### 3. Micro-benchmark의 교훈
- **불공정한 비교의 위험**: epoll(배칭X) vs ASIO(배칭O) → 왜곡된 결론
- **공정한 비교**: epoll(배칭O) vs ASIO(배칭O) → 정확한 분석
- 추상화 오버헤드만 측정 가능 (실제 워크로드와 다름)
- **비교 조건의 동등성**이 가장 중요
- 실제 시스템에서 검증 필수

#### 4. 최적화 접근법
- 코드 변경 전 아키텍처 이해 필요
- Micro-benchmark는 **동등한 조건**에서 비교
- Codex 리뷰가 정확히 문제점 지적 (중복 배칭)
- 팀 워크플로우의 가치 확인

### 🔍 향후 방향

**ASIO 성능 차이의 진짜 원인**:
- 배칭 문제가 아님 (이미 최적화됨)
- 다른 원인 탐색 필요:
  1. ASIO의 추상화 레이어 자체 오버헤드
  2. Zero-copy 기회 손실
  3. 메모리 할당 패턴
  4. Context switching 차이
  5. 캐시 효율성

**다음 실험 후보**:
1. `io_context.poll()` vs `io_context.run()` 성능 비교
2. Handler allocator 커스터마이징
3. Buffer 관리 전략 최적화
4. Profiling으로 실제 병목 지점 식별

## 참고 문서

- Plan: `docs/team/20260116_asio-batching-optimization/plan.md`
- Codex Review: `docs/team/20260116_asio-batching-optimization/review.md`
- Micro-benchmark: `tests/microbench_epoll_vs_asio.cpp`
- 핵심 코드: `src/io_thread.cpp:54-69` (in_event 배칭 로직)

## 이 실험의 가치

실패한 실험이지만 중요한 학습:
1. ✅ ZMQ 아키텍처의 깊은 이해 획득
2. ✅ Micro-benchmark와 실제의 차이 학습
3. ✅ 코드 리뷰의 중요성 확인 (Codex가 정확히 예측)
4. ✅ 최적화 접근법에 대한 교훈
5. ✅ 문서화와 검증 프로세스 개선

**"빠른 실패, 빠른 학습"** - 이 실험은 성공적인 실패입니다.
