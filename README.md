# zlink

> [libzlink](https://github.com/zlink/libzlink) v4.3.5 기반의 현대적 메시징 라이브러리 — 핵심 패턴에 집중하고, Boost.Asio 기반 I/O와 개발 친화적 API를 제공합니다.

[![Build](https://github.com/ulala-x/zlink/actions/workflows/build.yml/badge.svg)](https://github.com/ulala-x/zlink/actions/workflows/build.yml)
[![License: MPL 2.0](https://img.shields.io/badge/License-MPL%202.0-brightgreen.svg)](LICENSE)

---

## 왜 zlink인가?

libzlink는 강력하지만 수십 년간 축적된 복잡성을 안고 있습니다 — 레거시 프로토콜, 거의 사용되지 않는 소켓 타입, 그리고 과거 시대에 설계된 I/O 엔진.

**zlink는 libzlink의 핵심만 남기고 현대적으로 재구축합니다:**

| | libzlink | zlink |
|---|--------|-------|
| **Socket Types** | 17종 (draft 포함) | **7종** — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM |
| **I/O Engine** | 자체 poll/epoll/kqueue | **Boost.Asio** (번들, 외부 의존성 없음) |
| **암호화** | CURVE (libsodium) | **TLS** (OpenSSL) — `tls://`, `wss://` |
| **Transport** | 10종+ (PGM, TIPC, VMCI 등) | **6종** — `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` |
| **의존성** | libsodium, libbsd 등 | **OpenSSL만** |

---

## 주요 특징

### 간소화된 Core

REQ/REP, PUSH/PULL, 모든 draft socket을 제거했습니다. 남은 7종의 socket type — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM — 으로 실전 메시징 패턴의 대부분을 커버하면서, 복잡성에 의한 실수를 줄입니다. STREAM 소켓은 외부 클라이언트와의 RAW 통신을 위해 tcp, tls, ws, wss transport를 지원합니다.

### Boost.Asio 기반 I/O Engine

전체 I/O 계층을 **Boost.Asio**로 재작성했습니다 (header만 번들 — 외부 Boost 의존성 없음). 검증된 비동기 기반 위에 TLS와 WebSocket transport를 네이티브로 통합할 수 있습니다.

### 네이티브 TLS & WebSocket

외부 프록시 없이 암호화된 transport를 직접 지원합니다:

```c
// TLS 서버
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/cert.pem", ...);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/key.pem", ...);
zlink_bind(socket, "tls://*:5555");

// TLS 클라이언트
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.pem", ...);
zlink_connect(socket, "tls://server.example.com:5555");
```

---

## 아키텍처

zlink는 5개의 명확히 분리된 계층으로 구성됩니다:

```
┌──────────────────────────────────────────────────────┐
│  Application Layer                                    │
│  zlink_ctx_new() · zlink_socket() · zlink_send/recv()      │
├──────────────────────────────────────────────────────┤
│  Socket Logic Layer                                   │
│  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM  │
│  라우팅 전략: lb_t(Round-robin) · fq_t · dist_t       │
├──────────────────────────────────────────────────────┤
│  Engine Layer (Boost.Asio)                            │
│  asio_zmp_engine — ZMP v2.0 Protocol (8B 고정 헤더)   │
│  Proactor 패턴 · Speculative I/O · Backpressure       │
├──────────────────────────────────────────────────────┤
│  Transport Layer                                      │
│  tcp · ipc · ws — 평문                                │
│  tls · wss      — OpenSSL 암호화                      │
├──────────────────────────────────────────────────────┤
│  Core Infrastructure                                  │
│  msg_t(64B 고정) · pipe_t(Lock-free YPipe)            │
│  ctx_t(I/O Thread Pool) · session_base_t(Bridge)      │
└──────────────────────────────────────────────────────┘
```

### 핵심 설계

| 설계 원칙 | 설명 |
|-----------|------|
| **Zero-Copy** | 메시지 복사 최소화 — VSM(33B 이하)은 inline 저장, 대용량은 참조 카운팅 |
| **Lock-Free** | Thread 간 통신에 YPipe(CAS 기반 FIFO) 사용, mutex 없음 |
| **True Async** | Proactor 패턴 기반 비동기 I/O + Speculative I/O 최적화 |
| **Protocol Agnostic** | Transport와 Protocol의 명확한 분리 — 자체 ZMP v2.0 프로토콜 사용 |

### Thread 모델

- **Application Thread**: `zlink_send()`/`zlink_recv()` 호출
- **I/O Thread**: Boost.Asio `io_context` 기반 비동기 네트워크 처리
- **Reaper Thread**: 종료된 소켓/세션의 자원 정리
- Thread 간 통신은 Lock-free YPipe + Mailbox 시스템으로 처리

> 상세한 내부 아키텍처는 [Architecture Document](doc/arch/ARCHITECTURE.md)를 참고하세요.

---

## 개발 편의 기능 (계획)

간소화된 core를 넘어, 실전 분산 시스템을 위한 **고수준 메시징 스택**을 구축합니다:

| 기능 | 설명 | 스펙 |
|------|------|:----:|
| **Routing ID 통합** | `zlink_routing_id_t` 표준 타입, 자동 생성 포맷 통일 | [00](doc/plan/00-routing-id-unification.md) |
| **모니터링 강화** | Routing-ID 기반 이벤트 식별, Callback API, Socket별 메트릭스 | [01](doc/plan/01-enhanced-monitoring.md) |
| **Thread-safe Socket** | Asio Strand 직렬화로 여러 thread에서 단일 socket 공유 | [02](doc/plan/02-thread-safe-socket.md) |
| **Request/Reply API** | DEALER/ROUTER 기반 비동기 요청-응답 (REQ/REP 대체) | [03](doc/plan/03-request-reply-api.md) |
| **Service Discovery** | Registry 클러스터, Client-side Load Balancing, Health Monitoring | [04](doc/plan/04-service-discovery.md) |
| **SPOT Topic PUB/SUB** | 위치 투명한 토픽 메시징, 클러스터 전체 PUB/SUB | [05](doc/plan/05-spot-topic-pubsub.md) |
| **PGM/EPGM Transport** | PUB/SUB 소켓용 멀티캐스트 프로토콜 지원 (계획) | - |

> 전체 기능 로드맵과 의존성 그래프는 [Feature Roadmap](doc/plan/feature-roadmap.md)을 참고하세요.

---

## 시작하기

### 요구 사항

- **CMake** 3.10+
- **C++17** 컴파일러 (GCC 7+, Clang 5+, MSVC 2017+)
- **OpenSSL** (TLS/WSS 지원)

### 빌드

```bash
# Linux
./build-scripts/linux/build.sh x64 ON

# macOS
./build-scripts/macos/build.sh arm64 ON

# Windows (PowerShell)
.\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

### CMake 직접 빌드

```bash
cmake -B build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### CMake 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `WITH_TLS` | `ON` | OpenSSL을 통한 TLS/WSS 활성화 |
| `BUILD_TESTS` | `ON` | 테스트 빌드 |
| `BUILD_BENCHMARKS` | `OFF` | 벤치마크 빌드 |
| `BUILD_SHARED` | `ON` | Shared Library 빌드 |
| `ZLINK_CXX_STANDARD` | `17` | C++ 표준 (11, 14, 17, 20, 23) |

### OpenSSL 설치

```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# macOS
brew install openssl@3

# Windows (vcpkg)
vcpkg install openssl:x64-windows
```

---

## 지원 플랫폼

| 플랫폼 | Architecture | 상태 |
|--------|:------------:|:----:|
| Linux | x64, ARM64 | Stable |
| macOS | x64, ARM64 | Stable |
| Windows | x64, ARM64 | Stable |

---

## 성능

모든 transport에서 소형 메시지(64B) 기준 **4~6 M msg/s** 처리량을 달성하며, 전 패턴에서 **deadlock 없음**이 확인되었습니다.

| 패턴 | TCP | IPC | inproc |
|------|-----|-----|--------|
| DEALER↔DEALER | 6.03 M/s | 5.96 M/s | 5.96 M/s |
| PAIR | 5.78 M/s | 5.65 M/s | 6.09 M/s |
| PUB/SUB | 5.76 M/s | 5.70 M/s | 5.71 M/s |
| DEALER↔ROUTER | 5.40 M/s | 5.55 M/s | 5.40 M/s |
| ROUTER↔ROUTER | 5.03 M/s | 5.12 M/s | 4.71 M/s |

> 표준 libzlink 대비 ~99% 처리량 동등성. 상세 분석은 [성능 리포트](doc/report/performance/tag_0.5_performance_report.md)를 참고하세요.

---

## 문서

| 문서 | 설명 |
|------|------|
| [Feature Roadmap](doc/plan/feature-roadmap.md) | 계획 중인 기능과 의존성 그래프 |
| [Architecture](doc/arch/ARCHITECTURE.md) | 내부 아키텍처 상세 문서 |
| [TLS Usage Guide](doc/TLS_USAGE_GUIDE.md) | TLS/WSS 설정 가이드 |
| [C++ Standard Build Guide](CXX_BUILD_EXAMPLES.md) | C++ 표준별 빌드 방법 |

---

## 라이선스

[Mozilla Public License 2.0](LICENSE)

[Zlink (libzlink)](https://github.com/zlink/libzlink) 기반 — Copyright (c) 2007-2024 Contributors as noted in the AUTHORS file.
