# zlink

> [libzmq](https://github.com/zeromq/libzmq) v4.3.5 기반의 현대적 메시징 라이브러리 — 핵심 패턴에 집중하고, Boost.Asio 기반 I/O와 개발 친화적 API를 제공합니다.

[![Build](https://github.com/ulala-x/zlink/actions/workflows/build.yml/badge.svg)](https://github.com/ulala-x/zlink/actions/workflows/build.yml)
[![License: MPL 2.0](https://img.shields.io/badge/License-MPL%202.0-brightgreen.svg)](LICENSE)

---

## 왜 zlink인가?

libzmq는 강력하지만 수십 년간 축적된 복잡성을 안고 있습니다 — 레거시 프로토콜, 거의 사용되지 않는 소켓 타입, 그리고 과거 시대에 설계된 I/O 엔진.

**zlink는 libzmq의 핵심만 남기고 현대적으로 재구축합니다:**

| | libzmq | zlink |
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
│  tcp · ipc · inproc · ws — 평문                       │
│  tls · wss             — OpenSSL 암호화               │
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

> 상세한 내부 아키텍처는 [Architecture Document](doc/internals/architecture.md)를 참고하세요.

---

## 개발 편의 기능

간소화된 core를 넘어, 실전 분산 시스템을 위한 **고수준 메시징 스택**을 구축합니다:

| 기능 | 설명 | 가이드 |
|------|------|:------:|
| **Routing ID 통합** | `zlink_routing_id_t` 표준 타입, own 16B UUID / peer 4B uint32 | [Routing ID](doc/guide/08-routing-id.md) |
| **모니터링 강화** | Routing-ID 기반 이벤트 식별, Polling 방식 모니터 API | [Monitoring](doc/guide/06-monitoring.md) |
| **Service Discovery** | Registry 클러스터, Client-side Load Balancing, Health Monitoring | [Discovery](doc/guide/07-1-discovery.md) |
| **Gateway** | Discovery 기반 위치투명 요청/응답, 로드밸런싱 | [Gateway](doc/guide/07-2-gateway.md) |
| **SPOT Topic PUB/SUB** | 위치 투명한 토픽 메시징, Discovery 기반 자동 Mesh | [SPOT](doc/guide/07-3-spot.md) |

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
./core/build-scripts/linux/build.sh x64 ON

# macOS
./core/build-scripts/macos/build.sh arm64 ON

# Windows (PowerShell)
.\core\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

### CMake 직접 빌드

```bash
cmake -S . -B core/build/local -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build core/build/local
ctest --test-dir core/build/local --output-on-failure
```

### CMake 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `WITH_TLS` | `ON` | OpenSSL을 통한 TLS/WSS 활성화 |
| `BUILD_TESTS` | `ON` | 테스트 빌드 |
| `BUILD_BENCHMARKS` | `OFF` | 벤치마크 빌드 |
| `BUILD_SHARED` | `ON` | Shared Library 빌드 |
| `BUILD_STATIC` | `ON` | Static Library 빌드 |
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

> 표준 libzmq 대비 ~99% 처리량 동등성. 상세 분석은 [성능 가이드](doc/guide/10-performance.md)를 참고하세요.

---

## 문서

| 문서 | 설명 |
|------|------|
| [문서 네비게이션](doc/README.md) | 전체 문서 목차 및 독자별 경로 |
| [사용자 가이드](doc/guide/01-overview.md) | zlink API 가이드 (12편) |
| [바인딩 가이드](doc/bindings/overview.md) | C++/Java/.NET/Node.js/Python 바인딩 |
| [내부 아키텍처](doc/internals/architecture.md) | 시스템 아키텍처 및 내부 구현 |
| [빌드 가이드](doc/build/build-guide.md) | 빌드, 테스트, 패키징 |
| [Feature Roadmap](doc/plan/feature-roadmap.md) | 기능 로드맵과 의존성 그래프 |

---

## 라이선스

[Mozilla Public License 2.0](LICENSE)

[libzmq](https://github.com/zeromq/libzmq) 기반 — Copyright (c) 2007-2024 Contributors as noted in the AUTHORS file.
