# ASIO 전면 전환 (ASIO-Only Migration) 계획

## 프로젝트 개요

**프로젝트명:** zlink ASIO-Only Migration
**브랜치:** feature/asio-only
**목표:** libzmq의 기존 poll 기반 I/O를 완전히 제거하고 Boost.ASIO로 통합
**작성일:** 2026-01-15

## 현재 상태

### 완료된 작업
- ASIO 기반 poller 구현 완료 (`asio_poller_t`)
- ASIO 기반 engine 구현 완료 (`asio_engine_t`, `asio_ws_engine_t`, `asio_zmtp_engine_t`)
- 모든 transport별 listener/connecter 구현 완료:
  - TCP: `asio_tcp_listener`, `asio_tcp_connecter`
  - IPC: `asio_ipc_listener`, `asio_ipc_connecter`
  - TLS: `asio_tls_listener`, `asio_tls_connecter`
  - WebSocket: `asio_ws_listener`, `asio_ws_connecter`
- Transport 추상화 계층 완료 (`i_asio_transport`, `tcp_transport`, `ipc_transport`, `ssl_transport`, `ws_transport`, `wss_transport`)
- WebSocket ROUTER 패턴 지원 구현 완료
- 기본 테스트 통과 (64 tests, 5 skipped)

### 현재 아키텍처

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
├─────────────────────────────────────────────────────────────┤
│  Socket API (zmq_socket, zmq_bind, zmq_connect, ...)        │
├─────────────────────────────────────────────────────────────┤
│                    socket_base_t                             │
│           (PAIR/PUB/SUB/DEALER/ROUTER/XPUB/XSUB)            │
├─────────────────────────────────────────────────────────────┤
│                    session_base_t                            │
│              (pipe management, ZAP auth)                     │
├─────────────────────────────────────────────────────────────┤
│                      Engines                                 │
│  ┌──────────────┬──────────────┬──────────────┐            │
│  │ asio_engine_t│ asio_ws_     │ asio_zmtp_   │            │
│  │              │ engine_t     │ engine_t     │            │
│  │ (TCP/IPC)    │ (WebSocket)  │ (TLS)        │            │
│  └──────────────┴──────────────┴──────────────┘            │
├─────────────────────────────────────────────────────────────┤
│                  Transport Layer                             │
│  ┌──────────┬──────────┬──────────┬──────────┬──────────┐  │
│  │tcp_      │ipc_      │ssl_      │ws_       │wss_      │  │
│  │transport │transport │transport │transport │transport │  │
│  └──────────┴──────────┴──────────┴──────────┴──────────┘  │
│                  i_asio_transport (interface)                │
├─────────────────────────────────────────────────────────────┤
│              Listener/Connecter Layer                        │
│  ┌──────────┬──────────┬──────────┬──────────┐             │
│  │asio_tcp_ │asio_ipc_ │asio_tls_ │asio_ws_  │             │
│  │listener/ │listener/ │listener/ │listener/ │             │
│  │connecter │connecter │connecter │connecter │             │
│  └──────────┴──────────┴──────────┴──────────┘             │
├─────────────────────────────────────────────────────────────┤
│                    I/O Thread Layer                          │
│  ┌────────────────┐  ┌─────────────────────┐               │
│  │ io_thread_t    │  │   asio_poller_t     │               │
│  │ (command loop) │──│ (reactor pattern,   │               │
│  │                │  │  async_wait)        │               │
│  └────────────────┘  └─────────────────────┘               │
├─────────────────────────────────────────────────────────────┤
│               Boost.ASIO (io_context)                        │
│          (event loop, timers, async operations)              │
└─────────────────────────────────────────────────────────────┘
```

### 문제점 및 전환 필요성

1. **코드 중복 및 복잡성**
   - 현재 `ZMQ_IOTHREAD_POLLER_USE_ASIO` 매크로로 ASIO/non-ASIO 경로 분기
   - 유지보수 부담 증가, 테스트 복잡도 증가

2. **성능 최적화 제한**
   - ASIO의 proactor pattern 장점을 완전히 활용하지 못함
   - 플랫폼별 최적화 (IOCP, epoll, kqueue) 일관성 부족

3. **플랫폼 의존성**
   - Windows/Linux/macOS별 poller 구현이 여전히 존재
   - ASIO를 사용하면 플랫폼 추상화가 자동으로 처리됨

## 목표 및 범위

### 주요 목표

1. **기능적 목표**
   - 모든 transport (TCP/IPC/WS/WSS/TLS)를 ASIO로 완전히 통합
   - 기존 poll 기반 코드 완전 제거
   - API/ABI 호환성 유지 (public API 변경 없음)
   - 모든 소켓 옵션 기능 유지

2. **성능 목표**
   - Windows TCP 성능: 현재 대비 유지 또는 개선
   - WSL/Linux 성능: 현재 대비 유지
   - Latency p99: ±10% 이내 유지
   - Throughput: 현재 대비 유지 또는 개선

3. **품질 목표**
   - 모든 기존 테스트 통과 (64 tests)
   - 코드 복잡도 감소 (매크로 분기 제거)
   - 유지보수성 향상

### 범위

**포함 (In Scope):**
- 모든 transport: TCP, IPC, WebSocket (ws/wss), TLS
- 모든 플랫폼: Windows (x64/ARM64), Linux (x64/ARM64), macOS (x86_64/ARM64)
- 모든 소켓 타입: PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER
- I/O thread, command loop, timer, signaler 통합

**제외 (Out of Scope):**
- Public API 변경
- 소켓 옵션 추가/제거
- 새로운 transport 추가
- ZAP 인증 메커니즘 변경

### 리스크 허용 범위

1. **API/ABI 변경**
   - Public API: 변경 불가 (zmq.h 인터페이스 고정)
   - Internal API: 변경 가능 (리팩토링 허용)
   - ABI: 변경 가능 (라이브러리 버전 업데이트로 대응)

2. **성능 회귀**
   - 일시적 회귀: 각 Phase당 최대 20% 허용 (다음 Phase에서 회복)
   - 최종 성능: baseline 대비 -10% 이내 (허용 가능)
   - 목표: baseline 대비 +0% 이상

3. **기능 제약**
   - 모든 기존 기능 유지 필수
   - 테스트 실패: 0건 (모든 테스트 통과 필수)

## 현재 아키텍처 상세 분석

### 1. Poller/Signaler/Command Loop 구조

#### io_thread_t (I/O Thread)
- **위치:** `src/io_thread.hpp`, `src/io_thread.cpp`
- **역할:**
  - I/O 멀티플렉싱 스레드 관리
  - mailbox를 통한 command 수신
  - poller를 통한 FD 이벤트 처리
- **현재 상태:**
  - ASIO: `asio_poller_t` 사용, `io_context` 접근 제공
  - 이미 ASIO로 전환 완료

#### mailbox_t (Command Mailbox)
- **위치:** `src/mailbox.hpp`, `src/mailbox.cpp`
- **역할:**
  - 스레드 간 명령 전달 (lock-free queue)
  - signaler를 통한 이벤트 통지
- **현재 상태:**
  - ASIO와 독립적으로 동작
  - 유지 가능 (변경 불필요)

#### signaler_t (Thread Signaling)
- **위치:** `src/signaler.hpp`, `src/signaler.cpp`
- **역할:**
  - 스레드 간 wake-up 신호 (eventfd/pipe/socketpair)
- **현재 상태:**
  - ASIO와 독립적
  - 유지 가능 (ASIO async_wait로 감시)

#### asio_poller_t (ASIO Poller)
- **위치:** `src/asio/asio_poller.hpp`, `src/asio/asio_poller.cpp`
- **역할:**
  - Reactor pattern으로 FD readiness 감시
  - `async_wait` 사용 (read/write readiness)
- **현재 상태:**
  - 완전히 구현됨
  - 기존 poller 대체 완료

### 2. Transport 경로별 흐름

#### TCP Transport 흐름
```
zmq_connect("tcp://host:port")
    ↓
socket_base_t::connect()
    ↓
session_base_t::create()
    ↓
asio_tcp_connecter::create()
    ↓
asio_tcp_connecter::start_connecting()
    ↓
boost::asio::async_connect()
    ↓
on_connected() → create asio_engine_t + tcp_transport
    ↓
asio_engine_t::plug() → start_async_read/write
    ↓
async I/O loop (read/write completion handlers)
    ↓
session_base_t::push_msg/pull_msg
    ↓
pipe_t (queue) → socket_base_t
```

#### WebSocket Transport 흐름
```
zmq_connect("ws://host:port")
    ↓
socket_base_t::connect()
    ↓
session_base_t::create()
    ↓
asio_ws_connecter::create()
    ↓
asio_ws_connecter::start_connecting()
    ↓
boost::asio::async_connect() + WebSocket handshake
    ↓
on_handshake_complete() → create asio_ws_engine_t + ws_transport
    ↓
asio_ws_engine_t::plug() → start_async_read/write
    ↓
async I/O loop (WebSocket frame handling)
    ↓
session_base_t::push_msg/pull_msg
    ↓
pipe_t (queue) → socket_base_t
```

### 3. OS 분기 매크로 위치

#### Platform Detection
- **파일:** `builds/cmake/platform.hpp.in`, `src/platform.hpp`
- **매크로:**
  - `ZMQ_HAVE_WINDOWS` (Windows)
  - `ZMQ_HAVE_LINUX` (Linux)
  - `ZMQ_HAVE_OSX` (macOS)
  - `ZMQ_HAVE_IPC` (IPC transport 지원)

#### Poller Selection (제거 대상)
- **파일:** `src/poller.hpp`
- **매크로:**
  - `ZMQ_IOTHREAD_POLLER_USE_ASIO` (현재 강제 사용)
  - 이미 ASIO만 사용하도록 강제됨 (line 7-9)

#### Transport-Specific
- **Windows:**
  - `ZMQ_HAVE_WINDOWS` - IPC 비활성화
  - Winsock 초기화 (이미 ASIO가 처리)
- **POSIX:**
  - `ZMQ_HAVE_IPC` - Unix domain socket 활성화
  - POSIX stream descriptor 사용

### 4. 제거/대체 대상 명확화

#### 제거 대상 (Legacy Poll 코드)
현재 이미 ASIO로 전환되어 있으므로 실제로는 **정리(cleanup)** 작업:

1. **매크로 정리**
   - `ZMQ_IOTHREAD_POLLER_USE_ASIO` 조건부 컴파일 제거
   - ASIO가 기본이므로 분기 불필요

2. **코드 정리**
   - `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` 블록 제거
   - ASIO 경로만 남기고 단순화

#### 유지 대상
1. **Core Infrastructure**
   - `io_thread_t` - 스레드 관리
   - `mailbox_t` - 명령 전달
   - `signaler_t` - 스레드 신호
   - `pipe_t` - 메시지 큐

2. **ASIO Components**
   - `asio_poller_t`
   - `asio_engine_t`, `asio_ws_engine_t`, `asio_zmtp_engine_t`
   - All `asio_*_listener`, `asio_*_connecter`
   - All `*_transport` implementations

## ASIO-Only 목표 아키텍처

### 설계 원칙

1. **단일 I/O 백엔드**
   - Boost.ASIO만 사용
   - 플랫폼별 분기 최소화 (ASIO가 처리)

2. **명확한 계층 분리**
   - Transport layer: 네트워크 I/O
   - Engine layer: 프로토콜 처리 (ZMTP/WebSocket)
   - Session layer: 메시지 라우팅
   - Socket layer: API 제공

3. **성능 최적화**
   - Speculative write (이미 구현됨)
   - Zero-copy 경로 (encoder 버퍼 직접 사용)
   - Proactive async operations

### io_context 설계

#### 스레드 모델
```
Context (ctx_t)
    │
    ├─ I/O Thread 1 (io_thread_t)
    │   └─ io_context 1 (단일 스레드 실행)
    │
    ├─ I/O Thread 2 (io_thread_t)
    │   └─ io_context 2 (단일 스레드 실행)
    │
    └─ I/O Thread N (io_thread_t)
        └─ io_context N (단일 스레드 실행)
```

- **1 io_context per I/O thread**
- **1 thread per io_context** (단순성, 예측 가능성)
- **work_guard 사용** (io_context가 idle 상태에서도 유지)

#### Command 처리 통합

**현재 방식:**
- mailbox_t + signaler_t + poller

**ASIO 통합 방식:**
- mailbox_t는 유지 (lock-free queue)
- signaler FD를 ASIO `async_wait`로 감시
- Command 처리는 기존 방식 유지 (변경 불필요)

**장점:**
- 기존 command loop 재사용
- ASIO와 자연스럽게 통합
- 변경 최소화

#### Timer 통합

**현재:**
- `asio_engine_t`가 `boost::asio::steady_timer` 사용
- Handshake, heartbeat 타이머

**통합 방식:**
- 모든 타이머를 `boost::asio::steady_timer`로 통일
- io_thread 타이머도 ASIO 타이머 사용 가능 (선택적)

#### Backpressure/HWM 처리

**현재 방식:**
- `pipe_t`에서 HWM(High Water Mark) 체크
- `_output_stopped` 플래그로 백프레셔 관리

**유지:**
- ASIO 전환과 무관 (application layer 로직)
- 기존 메커니즘 유지

#### 오류/옵션 매핑

**Error Code 통합:**
- ASIO `boost::system::error_code` → errno
- Windows: Winsock error → errno (ASIO가 처리)
- POSIX: errno 직접 사용

**매핑 위치:**
- `src/asio/asio_error_handler.hpp` (이미 구현됨)
- Transport별 error 처리 일관화

## 제거 대상 컴포넌트 목록

### 실제 제거 대상 (코드 정리)

현재 이미 ASIO로 전환되어 있으므로, **조건부 컴파일 매크로만 정리**:

#### 1. 조건부 컴파일 제거

**파일 목록:**
```
src/session_base.cpp
src/socket_base.cpp
src/io_thread.hpp
src/io_thread.cpp
builds/cmake/platform.hpp.in
acinclude.m4
```

**작업:**
- `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` 제거
- ASIO 경로만 남기고 단순화
- `#else` 분기 제거

#### 2. 불필요한 헤더 정리

**제거 가능한 include:**
```cpp
// ASIO만 사용하므로 조건부 include 단순화
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO  // ← 제거
#include "asio/asio_tcp_connecter.hpp"
...
#endif  // ← 제거
```

**단순화 후:**
```cpp
// 항상 ASIO 사용
#include "asio/asio_tcp_connecter.hpp"
...
```

### 유지 대상 (변경 없음)

- 모든 ASIO 관련 파일
- Core infrastructure (io_thread, mailbox, signaler, pipe)
- Session/Socket layer
- Encoder/Decoder
- Options, context, ZAP

## 단계별 마이그레이션 계획

### Phase 0: 준비 및 기준 설정 (1-2일)

#### 목표
- 현재 상태 snapshot
- 성능 baseline 확정
- 테스트 환경 검증

#### 작업 항목

1. **성능 Baseline 측정**
   ```bash
   # Windows
   .\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
   taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20

   # WSL/Linux
   ./build-scripts/linux/build.sh x64 ON
   taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
   ```

   **기록 항목:**
   - TCP latency p50/p99
   - TCP throughput (msg/s)
   - IPC latency p50/p99
   - WebSocket latency p50/p99
   - CPU 사용률
   - 메모리 사용량

2. **테스트 검증**
   ```bash
   cd build && ctest --output-on-failure
   ```
   - 모든 64 tests 통과 확인
   - 실패 케이스 문서화

3. **코드 분석**
   - `ZMQ_IOTHREAD_POLLER_USE_ASIO` 사용 위치 목록화
   - 제거 가능한 조건부 컴파일 식별

4. **벤치마크 도구 검증** (Codex 권장사항)
   ```bash
   # 벤치마크 스크립트 동작 확인
   benchwithzmq/run_benchmarks.sh --runs 1

   # 결과 분석 도구 확인
   python3 benchwithzmq/analyze_results.py
   ```
   - 도구들이 정상 동작하는지 1회 실행으로 검증
   - 결과 파일 포맷 확인

5. **ASIO 버전 호환성 확인** (Gemini 필수사항 #3)
   ```bash
   # Boost 버전 확인
   grep "BOOST_VERSION" build/CMakeCache.txt

   # 또는 컴파일 타임에 확인
   cat > check_boost.cpp << 'EOF'
   #include <boost/version.hpp>
   #include <iostream>
   int main() {
     std::cout << "Boost version: " << BOOST_VERSION / 100000 << "."
               << BOOST_VERSION / 100 % 1000 << "."
               << BOOST_VERSION % 100 << std::endl;
     return 0;
   }
   EOF
   g++ check_boost.cpp -o check_boost && ./check_boost
   ```

   **최소 요구사항:**
   - Boost 1.70.0+ (ASIO 1.12.0+) 권장
   - Boost 1.66.0 이상 필수

6. **컴파일러 최적화 Baseline** (Gemini 필수사항 #2)
   ```bash
   # 주요 함수 디스어셈블리 추출
   objdump -d build/lib/libzmq.so | \
     grep -A 50 "asio_engine_t.*read_completed" > baseline_disasm.txt

   # 바이너리 크기 기록
   ls -lh build/lib/libzmq.so > baseline_binary_size.txt
   ```
   - 핫패스 함수들의 최적화 상태 기록
   - Phase별 비교 기준점 확보

#### 완료 기준
- [ ] Baseline 성능 데이터 기록 (`docs/team/20260115_asio-only/baseline.md`)
- [ ] 모든 테스트 통과 (64/64)
- [ ] 코드 분석 문서 작성 (`docs/team/20260115_asio-only/code_analysis.md`)
- [ ] 벤치마크 도구 동작 확인 (1회 실행 성공)
- [ ] Boost/ASIO 버전 확인 (1.70.0+ 권장, 1.66.0+ 필수)
- [ ] 컴파일러 최적화 baseline 추출 (디스어셈블리, 바이너리 크기)

### Phase 1: 조건부 컴파일 제거 - Transport Layer (2-3일)

#### 목표
- Transport 관련 조건부 컴파일 제거
- ASIO 경로만 남기고 단순화

#### 작업 항목

1. **session_base.cpp 정리**
   ```cpp
   // 패턴 1: 단순 ASIO 매크로 제거
   // 현재:
   #if defined ZMQ_IOTHREAD_POLLER_USE_ASIO
   #include "asio/asio_tcp_connecter.hpp"
   ...
   #endif

   // 변경 후:
   #include "asio/asio_tcp_connecter.hpp"
   ...

   // 패턴 2: Feature 매크로 조합 처리 (Codex 권장사항)
   // 현재:
   #if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL
   #include "asio/asio_tls_connecter.hpp"
   #endif

   // 변경 후: ASIO 매크로만 제거, Feature 매크로는 유지
   #if defined ZMQ_HAVE_ASIO_SSL
   #include "asio/asio_tls_connecter.hpp"
   #endif
   ```

2. **socket_base.cpp 정리**
   - ASIO 관련 조건부 컴파일 제거
   - Include 단순화
   - Feature 매크로 (`ZMQ_HAVE_ASIO_SSL`, `ZMQ_HAVE_WS`)는 보존

3. **빌드 스크립트 정리**
   - CMakeLists.txt에서 `ZMQ_IOTHREAD_POLLER_USE_ASIO` 강제 설정
   - 관련 주석 업데이트

#### 검증
```bash
# 각 플랫폼에서 빌드 및 테스트
./build-scripts/linux/build.sh x64 ON
./build-scripts/macos/build.sh arm64 ON
.\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

#### 완료 기준
- [ ] 모든 플랫폼 빌드 성공
- [ ] 모든 테스트 통과 (64/64)
- [ ] 조건부 컴파일 50% 이상 제거
- [ ] 성능: baseline 대비 ±5% 이내

### Phase 2: 조건부 컴파일 제거 - I/O Thread Layer (2-3일)

#### 목표
- io_thread_t 관련 조건부 컴파일 제거
- ASIO 전용 코드로 단순화

#### 작업 항목

1. **io_thread.hpp 정리**
   ```cpp
   // 현재:
   #if defined ZMQ_IOTHREAD_POLLER_USE_ASIO
   #include <boost/asio.hpp>
   #endif

   // 변경 후:
   #include <boost/asio.hpp>
   ```

2. **io_thread.cpp 정리**
   - ASIO 전용 경로로 단순화
   - `get_io_context()` 항상 제공

3. **관련 헤더 정리**
   - `poller.hpp` - ASIO만 사용하도록 단순화
   - `poller_base.hpp` - 불필요한 추상화 제거 검토

#### 검증
```bash
# 빌드 및 테스트
./build-scripts/linux/build.sh x64 ON
cd build/linux-x64 && ctest --output-on-failure
```

#### 완료 기준
- [ ] io_thread 관련 조건부 컴파일 100% 제거
- [ ] 모든 테스트 통과 (64/64)
- [ ] 성능: baseline 대비 ±5% 이내
- [ ] 코드 라인 수 10% 이상 감소

### Phase 3: Build System 정리 (1-2일)

#### 목표
- CMake 설정 단순화
- 빌드 플래그 정리

#### 작업 항목

1. **CMakeLists.txt 정리** (Codex 권장사항 반영)
   ```cmake
   # Line 387 개선 (제거 아님, 단순화만)
   # 기존:
   string(TOUPPER ${POLLER} UPPER_POLLER)
   set(ZMQ_IOTHREAD_POLLER_USE_${UPPER_POLLER} 1)

   # 변경 후: 변수명 단순화 및 주석 개선
   # ASIO is the only supported I/O poller backend
   set(ZMQ_IOTHREAD_POLLER_USE_ASIO 1)
   ```

   **주의:** Line 387은 platform.hpp 생성에 필수이므로 **유지**해야 함
   - 단, 변수 기반 설정을 직접 설정으로 단순화
   - 주석 개선으로 의도 명확히

2. **platform.hpp.in 정리**
   - ASIO 관련 매크로 단순화
   - 불필요한 poller 선택 로직 제거

3. **acinclude.m4 정리**
   - Autoconf 관련 정리 (사용 안 함)

#### 검증
```bash
# Clean build
rm -rf build
cmake -B build -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

#### 완료 기준
- [ ] Clean build 성공
- [ ] 모든 테스트 통과 (64/64)
- [ ] CMake 경고 0건
- [ ] 빌드 시간 측정 및 기록

### Phase 4: 문서화 및 주석 정리 (1일)

#### 목표
- 코드 주석 업데이트
- 문서 정리

#### 작업 항목

1. **주석 업데이트**
   - "Phase 1-B", "Phase 1-C" 등 임시 주석 제거
   - ASIO 전용 동작 명확히 설명

2. **CLAUDE.md 업데이트**
   - ASIO-only 아키텍처 반영
   - 빌드 요구사항 업데이트

3. **README.md 업데이트**
   - ASIO 기반 설명
   - 성능 특성 문서화

#### 완료 기준
- [ ] 모든 임시 주석 제거
- [ ] CLAUDE.md 업데이트 완료
- [ ] README.md 업데이트 완료

### Phase 5: 최종 검증 및 성능 측정 (2-3일)

#### 목표
- 전체 플랫폼 검증
- 성능 비교
- 릴리스 준비

#### 작업 항목

1. **전체 플랫폼 빌드 및 테스트**
   ```bash
   # Linux x64/ARM64
   ./build-scripts/linux/build.sh x64 ON
   ./build-scripts/linux/build.sh arm64 ON

   # macOS x86_64/ARM64
   ./build-scripts/macos/build.sh x86_64 ON
   ./build-scripts/macos/build.sh arm64 ON

   # Windows x64/ARM64
   .\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
   .\build-scripts\windows\build.ps1 -Architecture ARM64 -RunTests "OFF"
   ```

2. **성능 벤치마크 (모든 플랫폼)**
   ```bash
   taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
   ```

   **비교 항목:**
   - Baseline vs Phase 5
   - Windows vs WSL vs Linux
   - Transport별 (TCP/IPC/WS)

3. **메모리 프로파일링**
   ```bash
   # Valgrind (Linux)
   valgrind --leak-check=full --show-leak-kinds=all ./test_program

   # Address Sanitizer
   cmake -B build -DENABLE_ASAN=ON
   ```

4. **CI/CD 검증**
   - GitHub Actions workflow 확인
   - 모든 플랫폼 자동 빌드 성공

#### 완료 기준
- [ ] 모든 플랫폼 빌드 성공 (6/6)
- [ ] 모든 테스트 통과 (64/64, 모든 플랫폼)
- [ ] 성능: baseline 대비 ±10% 이내
- [ ] 메모리 누수 0건
- [ ] CI/CD 통과

## 각 단계별 검증 기준

### 기능 검증

#### 모든 Phase 공통
1. **테스트 통과**
   ```bash
   cd build && ctest --output-on-failure
   ```
   - 64 tests 모두 통과 (5 skipped는 정상)
   - 실패 허용: 0건

2. **Transport Matrix 검증**
   ```bash
   ./build/tests/test_transport_matrix
   ```
   - TCP/IPC/WS/WSS/TLS 모든 transport 테스트
   - PAIR, PUB/SUB, ROUTER/DEALER 패턴 검증

3. **메모리 안전성**
   ```bash
   # Address Sanitizer
   cmake -B build -DENABLE_ASAN=ON
   cmake --build build
   cd build && ctest
   ```
   - 메모리 누수: 0건
   - Use-after-free: 0건
   - Buffer overflow: 0건

### 성능 검증

#### Baseline 기준

**측정 환경:**
- CPU: Intel Core i7 또는 동등 (single core pinning)
- OS: Windows 11, WSL2 Ubuntu 22.04, Linux Ubuntu 22.04
- 빌드: Release mode (`-O3`)

**측정 명령:**
```bash
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
```

#### Phase별 성능 기준 (Gemini 필수사항 #1 반영: 누적 회귀 관리)

| Phase | Latency p99 | Throughput | CPU 사용률 | 개별 허용 | **누적 허용 (Baseline 대비)** |
|-------|------------|-----------|-----------|----------|----------------------------|
| Phase 0 (Baseline) | 100% | 100% | 100% | - | - |
| Phase 1 | 95-105% | 95-105% | 95-105% | ±5% | **±5%** |
| Phase 2 | 95-105% | 95-105% | 95-105% | ±5% | **±8%** (누적) |
| Phase 3 | 95-105% | 95-105% | 95-105% | ±5% | **±10%** (누적) |
| Phase 4 | 95-105% | 95-105% | 95-105% | ±5% | **±10%** (누적) |
| Phase 5 (Final) | 90-110% | 90-110% | 90-110% | ±10% | **±10%** (최종) |

**중요:**
- **누적 회귀가 10%를 초과하면 즉시 중단** 및 원인 분석
- 각 Phase는 이전 Phase 대비 ±5%, Baseline 대비는 누적 기준 적용
- Phase 2-4에서 누적 10% 초과 시 최적화 필수

#### 성능 회귀 대응

**Minor 회귀 (5-10%):**
- 문서화 후 다음 Phase에서 개선
- 원인 분석 및 최적화 계획 수립

**Major 회귀 (>10%):**
- 해당 Phase 중단
- 원인 분석 후 수정
- 재측정 후 진행

**성능 개선 (>5%):**
- 원인 분석 및 문서화
- 다른 플랫폼/transport 검증

### 품질 검증

#### 코드 품질

1. **정적 분석**
   ```bash
   # Clang-Tidy
   clang-tidy src/**/*.cpp -- -I./include

   # Cppcheck
   cppcheck --enable=all src/
   ```

2. **코드 복잡도**
   - 조건부 컴파일 블록 감소 확인
   - 중첩 깊이 감소 확인

3. **코드 커버리지**
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Coverage
   cmake --build build
   cd build && ctest
   lcov --capture --directory . --output-file coverage.info
   ```

#### 문서 품질

1. **주석 일관성**
   - ASIO 관련 설명 정확성
   - 임시 주석 제거 확인

2. **문서 완전성**
   - CLAUDE.md 업데이트
   - README.md 업데이트
   - Migration guide 작성

## 성능 벤치마크 기준

### 벤치마크 시나리오

#### 1. TCP Latency (inproc_lat equivalent)
```bash
# Server
./inproc_lat tcp://127.0.0.1:5555 1 100000

# Client
./inproc_lat tcp://127.0.0.1:5555 1 100000
```

**측정 항목:**
- 평균 latency (μs)
- p50, p95, p99, p999 latency
- Round-trip 시간

**목표:**
- p99 latency: baseline ± 10%
- 평균 latency: baseline ± 5%

#### 2. TCP Throughput (inproc_thr equivalent)
```bash
# Server
./inproc_thr tcp://127.0.0.1:5556 1024 1000000

# Client
./inproc_thr tcp://127.0.0.1:5556 1024 1000000
```

**측정 항목:**
- Throughput (msg/s, MB/s)
- CPU 사용률 (%)

**목표:**
- Throughput: baseline ± 10%
- CPU 사용률: baseline ± 15%

#### 3. IPC Performance
```bash
# Server
./local_lat ipc:///tmp/zmq_test.ipc 1 100000

# Client
./local_lat ipc:///tmp/zmq_test.ipc 1 100000
```

**측정 항목:**
- Latency (μs)
- Throughput (msg/s)

**목표:**
- Latency: baseline ± 10%
- Throughput: baseline ± 10%

#### 4. WebSocket Performance
```bash
# Server (WS)
./ws_lat ws://127.0.0.1:8080 1 10000

# Client
./ws_lat ws://127.0.0.1:8080 1 10000
```

**측정 항목:**
- Latency (μs)
- Frame overhead (%)

**목표:**
- Latency: baseline ± 15% (WebSocket overhead 고려)

### 벤치마크 자동화

#### 스크립트 작성
```bash
#!/bin/bash
# benchwithzmq/run_all_benchmarks.sh

RUNS=20
OUTPUT_DIR="docs/team/20260115_asio-only/benchmarks"

echo "Running TCP latency benchmark..."
taskset -c 0 ./run_tcp_lat.sh --runs $RUNS > $OUTPUT_DIR/tcp_lat.txt

echo "Running TCP throughput benchmark..."
taskset -c 0 ./run_tcp_thr.sh --runs $RUNS > $OUTPUT_DIR/tcp_thr.txt

echo "Running IPC benchmark..."
taskset -c 0 ./run_ipc_lat.sh --runs $RUNS > $OUTPUT_DIR/ipc_lat.txt

echo "Running WebSocket benchmark..."
taskset -c 0 ./run_ws_lat.sh --runs $RUNS > $OUTPUT_DIR/ws_lat.txt

# Generate report
python3 generate_benchmark_report.py $OUTPUT_DIR
```

#### 결과 비교
```python
# benchwithzmq/compare_benchmarks.py
import json

baseline = load_benchmark("baseline.json")
current = load_benchmark("phase1.json")

for test in ["tcp_lat", "tcp_thr", "ipc_lat", "ws_lat"]:
    delta = calculate_delta(baseline[test], current[test])
    print(f"{test}: {delta:+.2f}%")

    if abs(delta) > 10:
        print(f"WARNING: {test} regression > 10%")
```

## 리스크 및 롤백 전략

### 리스크 식별

#### 1. 성능 회귀 리스크
**가능성:** Medium
**영향도:** High

**원인:**
- 조건부 컴파일 제거 시 컴파일러 최적화 변화
- ASIO 경로의 숨겨진 성능 문제 노출

**완화 방안:**
- 각 Phase마다 성능 측정
- 회귀 발견 즉시 원인 분석
- 필요시 Phase 롤백

#### 2. 플랫폼별 호환성 문제
**가능성:** Low
**영향도:** High

**원인:**
- Windows/Linux/macOS 차이
- ASIO 버전별 동작 차이

**완화 방안:**
- 각 Phase마다 모든 플랫폼 테스트
- CI/CD 자동화
- 플랫폼별 fallback 경로 유지 (필요시)

#### 3. 숨겨진 의존성 문제
**가능성:** Low
**영향도:** Medium

**원인:**
- 조건부 컴파일 제거 시 누락된 의존성
- Include 순서 변경으로 인한 문제

**완화 방안:**
- 점진적 제거 (Phase별 분리)
- 빌드 경고 모니터링
- Clean build 반복 검증

#### 4. 테스트 실패
**가능성:** Medium
**영향도:** High

**원인:**
- 코드 정리 중 로직 변경
- Race condition 노출

**완화 방안:**
- 테스트 우선 접근 (TDD)
- 각 커밋마다 테스트 실행
- Sanitizer 활용

### 롤백 전략

#### Phase별 롤백 절차

1. **즉시 롤백 조건**
   - 테스트 실패 > 5%
   - 크리티컬 버그 발견
   - 빌드 실패 (모든 플랫폼)

2. **롤백 프로세스**
   ```bash
   # Git revert
   git revert <commit-hash>

   # 또는 branch reset
   git reset --hard <previous-phase-tag>

   # 재빌드 및 검증
   ./build-scripts/linux/build.sh x64 ON
   cd build && ctest --output-on-failure
   ```

3. **롤백 검증**
   - 모든 테스트 통과 확인
   - 성능 baseline 복구 확인
   - 문서 업데이트 (롤백 사유 기록)

#### 부분 롤백 전략

**시나리오:** Phase 2에서 성능 회귀 15% 발생

**대응:**
1. Phase 2의 특정 파일만 revert
2. Phase 1 상태로 부분 복구
3. Phase 2 재설계 후 재시도

**예시:**
```bash
# io_thread.cpp만 롤백
git checkout HEAD~1 -- src/io_thread.cpp

# 재빌드 및 테스트
cmake --build build
cd build && ctest
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20
```

### 긴급 대응 계획

#### Critical Issue 대응

**정의:** 프로덕션 영향 또는 데이터 손실 가능성

**대응 절차:**
1. 작업 즉시 중단
2. main 브랜치로 롤백
3. 이슈 분석 및 핫픽스
4. 재설계 후 재시도

#### 성능 Critical 대응

**정의:** 성능 회귀 > 20%

**대응 절차:**
1. 프로파일링 (perf, gprof)
2. 핫스팟 식별
3. 최적화 패치 적용
4. 재측정

**도구:**
```bash
# Linux perf
perf record -g ./benchmark
perf report

# Valgrind callgrind
valgrind --tool=callgrind ./benchmark
kcachegrind callgrind.out.*
```

### 성공 기준

#### Phase 5 완료 기준

- [ ] **기능:** 모든 테스트 통과 (64/64, 모든 플랫폼)
- [ ] **성능:** Baseline 대비 ±10% 이내 (모든 벤치마크)
- [ ] **품질:** 메모리 누수 0건, Sanitizer 경고 0건
- [ ] **문서:** CLAUDE.md, README.md 업데이트 완료
- [ ] **CI/CD:** 모든 플랫폼 자동 빌드 성공
- [ ] **코드:** 조건부 컴파일 100% 제거 (ASIO 전용)

#### 최종 릴리스 조건

- [ ] Phase 5 완료 기준 충족
- [ ] 성능 보고서 작성 (`docs/team/20260115_asio-only/performance_report.md`)
- [ ] Migration summary 작성 (`docs/team/20260115_asio-only/SUMMARY.md`)
- [ ] CHANGELOG.md 업데이트
- [ ] 버전 태그 생성 (`v0.2.0-asio-only`)

## 타임라인

| Phase | 기간 | 시작일 | 종료일 | 담당 |
|-------|------|--------|--------|------|
| Phase 0: 준비 및 기준 설정 | 1-2일 | 2026-01-15 | 2026-01-16 | dev-cxx |
| Phase 1: Transport Layer 정리 | 2-3일 | 2026-01-17 | 2026-01-19 | dev-cxx |
| Phase 2: I/O Thread Layer 정리 | 2-3일 | 2026-01-20 | 2026-01-22 | dev-cxx |
| Phase 3: Build System 정리 | 1-2일 | 2026-01-23 | 2026-01-24 | dev-cxx |
| Phase 4: 문서화 및 주석 정리 | 1일 | 2026-01-25 | 2026-01-25 | dev-cxx |
| Phase 5: 최종 검증 및 성능 측정 | 2-3일 | 2026-01-26 | 2026-01-28 | dev-cxx |
| **총 기간** | **9-14일** | **2026-01-15** | **2026-01-28** | |

## 참고 문서

- `docs/team/20260114_ASIO-성능개선/plan.md` - 이전 ASIO 성능 개선 계획
- `CLAUDE.md` - 프로젝트 전체 가이드
- `src/asio/asio_engine.hpp` - ASIO 엔진 구현
- `src/asio/asio_poller.hpp` - ASIO poller 구현

## 부록

### A. 빠른 시작 가이드

```bash
# 1. Baseline 측정
git checkout feature/asio-only
./build-scripts/linux/build.sh x64 ON
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20 > baseline.txt

# 2. Phase 1 시작
git checkout -b phase1-transport-cleanup

# 3. 작업 후 검증
./build-scripts/linux/build.sh x64 ON
cd build && ctest --output-on-failure
taskset -c 0 benchwithzmq/run_benchmarks.sh --runs 20 > phase1.txt

# 4. 성능 비교
python3 benchwithzmq/compare_benchmarks.py baseline.txt phase1.txt
```

### B. 트러블슈팅

#### 빌드 실패
```bash
# Clean build
rm -rf build
cmake -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
```

#### 테스트 실패
```bash
# Verbose 모드로 실행
cd build
ctest --output-on-failure --verbose

# 특정 테스트만 실행
./tests/test_transport_matrix
```

#### 성능 회귀
```bash
# 프로파일링
perf record -g ./benchmark
perf report

# CPU pinning 확인
taskset -c 0 ./benchmark
```

### C. 체크리스트

#### Phase 완료 체크리스트

```markdown
## Phase 0: 준비 및 기준 설정
- [ ] Baseline 성능 측정 완료
- [ ] 모든 테스트 통과 확인 (64/64)
- [ ] 코드 분석 문서 작성

## Phase 1: Transport Layer 정리
- [ ] session_base.cpp 조건부 컴파일 제거
- [ ] socket_base.cpp 조건부 컴파일 제거
- [ ] 모든 플랫폼 빌드 성공
- [ ] 모든 테스트 통과 (64/64)
- [ ] 성능 검증 (±5% 이내)

## Phase 2: I/O Thread Layer 정리
- [ ] io_thread.hpp 조건부 컴파일 제거
- [ ] io_thread.cpp 조건부 컴파일 제거
- [ ] poller.hpp 단순화
- [ ] 모든 테스트 통과 (64/64)
- [ ] 성능 검증 (±5% 이내)

## Phase 3: Build System 정리
- [ ] CMakeLists.txt 정리
- [ ] platform.hpp.in 정리
- [ ] Clean build 성공
- [ ] 모든 테스트 통과 (64/64)

## Phase 4: 문서화 및 주석 정리
- [ ] 임시 주석 제거
- [ ] CLAUDE.md 업데이트
- [ ] README.md 업데이트

## Phase 5: 최종 검증 및 성능 측정
- [ ] 모든 플랫폼 빌드 성공 (6/6)
- [ ] 모든 테스트 통과 (64/64, 모든 플랫폼)
- [ ] 성능 검증 (±10% 이내)
- [ ] 메모리 안전성 검증
- [ ] CI/CD 통과
- [ ] 문서 완전성 검증
```

---

**작성자:** Claude (dev-cxx agent)
**작성일:** 2026-01-15
**버전:** 1.0
**상태:** 초안 (Draft)
