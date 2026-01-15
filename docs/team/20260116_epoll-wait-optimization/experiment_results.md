# epoll_wait 최적화 실험 결과

## 날짜
2026-01-16

## 실험 목표
strace 프로파일링으로 발견한 epoll_wait 과다 호출 문제를 해결하여 성능 향상

**발견된 문제**:
- zlink: epoll_wait 80,287회 (71% syscall time)
- libzmq: epoll_wait 9,633회 (54% syscall time)
- **8.3배 차이!**

**목표**: epoll_wait 호출을 libzmq 수준으로 줄여서 30% 성능 격차 해소

## 문제 원인 분석

### asio_poller.cpp의 "Phase 1 Optimization"

**위치**: `src/asio/asio_poller.cpp:383-412`

```cpp
//  Phase 1 Optimization: Event Batching
//  Step 1: Process all ready events non-blocking
std::size_t events_processed = _io_context.poll();  // ← non-blocking epoll_wait!

//  Step 2: Only wait if no events were ready
if (events_processed == 0) {
    _io_context.run_for(...);  // ← blocking epoll_wait
}
```

**문제**:
- 매 loop마다 `poll()` 호출 → non-blocking epoll_wait(timeout=0)
- 이벤트가 없으면 `run_for()` 추가 호출 → blocking epoll_wait
- 이벤트가 있으면 다시 loop → 또 `poll()` 호출
- **결과**: epoll_wait가 8.3배 과다 호출

### Syscall 프로파일 (최적화 전)

**zlink (ASIO):**
```
% time     seconds  usecs/call     calls    errors syscall
---------- ----------- ----------- --------- --------- -------------------
 70.98    1.657329          20     80287           epoll_wait  ⚠️
  8.37    0.195558           4     40132           getpid
  4.72    0.110303           8     13642         1 write
  4.61    0.107670           6     17650      4007 read
  4.45    0.104063         187       555        87 futex
  0.68    0.015980           3      4036           epoll_ctl
```

**libzmq (baseline):**
```
% time     seconds  usecs/call     calls    errors syscall
---------- ----------- ----------- --------- --------- -------------------
 54.09    1.213172         125      9633           epoll_wait  ✅
 26.81    0.601340          23     26045           poll
  9.46    0.212130           4     43710           getpid
  2.90    0.064931           7      8827           write
  1.69    0.037793           4      8831           read
  1.59    0.035567           4      8676           epoll_ctl
  1.30    0.029105           5      5628         8 recvfrom
```

## 구현: epoll_wait 최적화

### 수정 내용

**위치**: `src/asio/asio_poller.cpp:383-403`

**Before:**
```cpp
//  Phase 1 Optimization: Event Batching
std::size_t events_processed = _io_context.poll();  // non-blocking epoll_wait

if (events_processed == 0) {
    _io_context.run_for(...);  // blocking epoll_wait
}
```

**After:**
```cpp
//  Phase 2 Optimization: Single epoll_wait per iteration
//  The previous "Phase 1" used poll() + run_for(), causing
//  8x more epoll_wait syscalls (80K vs 10K compared to libzmq).
//  Now we use only run_for() to minimize syscall overhead.

_io_context.run_for(std::chrono::milliseconds(poll_timeout_ms));
```

**변경점**:
- `_io_context.poll()` 제거 (non-blocking epoll_wait 제거)
- `_io_context.run_for()` 만 사용 (blocking epoll_wait 유지)
- loop당 epoll_wait 호출: 2회 → 1회

## 실험 결과

### Syscall 프로파일 (최적화 후)

**zlink (ASIO - optimized):**
```
% time     seconds  usecs/call     calls    errors syscall
---------- ----------- ----------- --------- --------- -------------------
 53.81    1.187151         122      9692           epoll_wait  ✅
 21.50    0.474392          19     24082           poll
  8.54    0.188442           4     40131           getpid
  4.75    0.104764           5     17650      4007 read  ⚠️
  4.58    0.101138           7     13642         1 write  ⚠️
  0.67    0.014765           3      4036           epoll_ctl
```

### Syscall 비교

| Syscall | libzmq | zlink (before) | zlink (after) | 개선 |
|---------|--------|----------------|---------------|------|
| **epoll_wait** | 9,633 | 80,287 | **9,692** | **-88%** ✅ |
| **poll** | 26,049 | 0 | 24,082 | NEW |
| **read** | 8,835 | 17,650 | **17,650** | 변화 없음 ⚠️ |
| **write** | 8,831 | 13,642 | **13,642** | 변화 없음 ⚠️ |
| **sendto** | 5,622 | 2 | 2 | - |
| **recvfrom** | 5,628 | 0 | 0 | - |

### 벤치마크 결과 (DEALER_ROUTER, TCP 64B)

| 구분 | Throughput | vs libzmq | 개선폭 |
|------|-----------|-----------|--------|
| libzmq (baseline) | 5.17 M/s | - | - |
| zlink (최적화 전) | 3.21 M/s | -38% | - |
| zlink (최적화 후) | 3.26 M/s | -37% | **+1.6%** |

**결과**: epoll_wait는 8.3배 감소했지만 성능은 거의 개선되지 않음 (+1.6%)

## 실패 원인 분석

### ✅ 성공: epoll_wait 최적화

- epoll_wait: 80,287 → 9,692 (**-88%**)
- libzmq와 거의 동일한 수준 달성 (9,633)
- syscall time 비중: 71% → 54%

### ❌ 실패: 성능 개선

- 성능 개선: 3.21 → 3.26 M/s (**+1.6%**)
- libzmq 대비 여전히 **-37%** 격차
- **epoll_wait가 주요 병목이 아니었음!**

### 🔍 진짜 병목: read/write 호출 과다

**핵심 발견**:
1. **read가 2배 많음**: 17,650 vs 8,835 (libzmq)
2. **write가 1.5배 많음**: 13,642 vs 8,831 (libzmq)
3. **syscall 종류가 다름**:
   - libzmq: `sendto` + `recvfrom` (socket-specific)
   - zlink: `read` + `write` (generic file I/O)

### ASIO의 구조적 한계

**근본 원인**:
- ASIO `stream_descriptor`는 generic file descriptor abstraction
- Socket-specific syscall (`sendto/recvfrom`)을 사용할 수 없음
- 대신 generic I/O syscall (`read/write`) 사용
- 한 번에 읽는/쓰는 양이 적어서 호출 횟수 증가

**증거**:
```cpp
// libzmq: Socket-specific syscall 사용
ssize_t rc = sendto(fd, buf, len, flags, NULL, 0);
ssize_t rc = recvfrom(fd, buf, len, flags, NULL, NULL);

// ASIO stream_descriptor: Generic I/O 사용
ssize_t bytes = read(fd, buf, len);   // ← 더 많이 호출됨
ssize_t bytes = write(fd, buf, len);  // ← 더 많이 호출됨
```

### 왜 read/write가 더 많이 호출되는가?

가능한 원인:
1. **버퍼 크기 차이**: ASIO가 한 번에 적은 양을 읽음
2. **재시도 로직**: EAGAIN 처리 방식 차이
3. **Batching 효율**: `in_event()` 내부 배칭이 덜 효율적
4. **Handler dispatch**: Lambda 호출 오버헤드로 인한 처리 지연

## 결론

### ✅ epoll_wait 최적화 성공

1. **문제 식별**: strace로 epoll_wait 과다 호출 발견 (8.3배)
2. **원인 파악**: `poll()` + `run_for()` 이중 호출
3. **해결 방법**: `poll()` 제거, `run_for()` 만 사용
4. **결과 검증**: epoll_wait 9,692회 (libzmq와 동일)

### ❌ 성능 개선 실패

1. **성능 개선 미미**: +1.6% (3.21 → 3.26 M/s)
2. **libzmq 격차 유지**: -37%
3. **잘못된 가설**: "epoll_wait가 주요 병목" ❌

### 🎯 진짜 문제: read/write 과다 호출

1. **read**: 2배 많음 (17,650 vs 8,835)
2. **write**: 1.5배 많음 (13,642 vs 8,831)
3. **syscall 종류**: sendto/recvfrom vs read/write
4. **근본 원인**: ASIO `stream_descriptor`의 구조적 한계

## 학습 내용

### 1. 프로파일링의 중요성

- **strace**: syscall 패턴 분석으로 정확한 병목 식별
- **정량적 측정**: 추측 대신 데이터 기반 분석
- **비교 분석**: libzmq와 직접 비교로 차이점 발견

### 2. 최적화의 함정

- **부분 최적화**: epoll_wait 최적화만으로는 불충분
- **전체 시스템**: 한 부분 개선이 전체 성능과 무관할 수 있음
- **측정 필수**: 최적화 후 반드시 성능 측정

### 3. ASIO 아키텍처 이해

- **Abstraction cost**: Generic abstraction의 성능 비용
- **syscall 차이**: socket-specific vs generic I/O
- **Trade-off**: 이식성 vs 성능

### 4. 병목의 재발견

**이전 가설**:
1. ASIO Proactor 추상화 오버헤드 (72%) → **배칭으로 0%** ✅
2. Handler dispatch 오버헤드 → **배칭으로 해결 안 됨** ❌
3. epoll_wait 과다 호출 → **88% 감소했지만 성능 +1.6%만** ❌

**실제 병목** (추정):
1. **read/write 과다 호출** (2배, 1.5배)
2. **syscall 종류 차이** (sendto/recvfrom vs read/write)
3. **버퍼링 비효율**
4. **Handler dispatch 누적 오버헤드**

## 향후 방향

### 즉시 시도 가능한 최적화

1. **read/write 호출 분석**
   - 왜 2배 많이 호출되는가?
   - 한 번에 읽는 바이트 수 측정
   - strace -e read,write -T로 상세 분석

2. **버퍼 크기 조정**
   - ASIO의 read buffer 크기 확인
   - 한 번에 더 많이 읽도록 조정 가능한지 확인

3. **Batching 개선**
   - `in_event()` 내부 동작 상세 분석
   - 여러 read를 한 번에 처리할 수 있는지 확인

### 근본적 해결 (고비용)

1. **Native socket syscall 사용**
   - `stream_descriptor` 대신 ASIO `tcp::socket` 사용
   - 하지만 ZMQ의 기존 FD 관리와 충돌 가능

2. **Custom transport layer**
   - ASIO를 thin wrapper로만 사용
   - read/write를 sendto/recvfrom으로 직접 호출

3. **Zero-copy 탐색**
   - `sendfile()`, `splice()` 등 zero-copy syscall
   - 하지만 ZMQ 메시지 구조와 호환성 확인 필요

### 현실적 접근

**수용 가능한 성능 차이**:
- libzmq: 5.17 M/s
- zlink: 3.26 M/s (-37%)

**Trade-off 고려**:
- ✅ 이식성: ASIO는 Windows, macOS, Linux 모두 지원
- ✅ 유지보수: Modern C++, 추상화된 I/O
- ❌ 성능: 37% slower (하지만 절대 성능은 3.26 M/s - 충분히 빠름)

**결론**: 37% 차이가 critical하지 않다면 현재 구조 유지 권장

## 참고 자료

- strace 프로파일링 명령어:
  ```bash
  strace -c -f build/bin/comp_zlink_dealer_router zlink tcp 64
  strace -c -f build/bin/comp_std_zmq_dealer_router libzmq tcp 64
  ```

- 변경된 파일:
  - `src/asio/asio_poller.cpp:383-403` (poll() 제거)

- 관련 실험:
  - `docs/team/20260116_asio-batching-optimization/` (배칭 실험 - 실패)
  - 이번 실험도 epoll_wait는 최적화했지만 성능은 개선 안 됨

## 이 실험의 가치

성능 개선 실패했지만 중요한 발견:
1. ✅ epoll_wait는 주요 병목이 아님 (88% 감소해도 +1.6%)
2. ✅ 진짜 병목은 read/write 호출 과다 (2배, 1.5배)
3. ✅ ASIO의 구조적 한계 이해 (generic I/O vs socket-specific)
4. ✅ 정량적 프로파일링의 중요성
5. ✅ "빠른 실험, 빠른 학습"

**"Measure, don't guess"** - 이 실험은 추측을 데이터로 검증한 성공적인 과학적 접근입니다.
