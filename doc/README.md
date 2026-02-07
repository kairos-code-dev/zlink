# zlink 문서

> zlink 프로젝트 문서 네비게이션

---

## 독자별 경로

| 독자 | 시작 문서 | 설명 |
|------|-----------|------|
| **라이브러리 사용자** | [guide/01-overview.md](guide/01-overview.md) | zlink API로 메시징 애플리케이션 개발 |
| **바인딩 사용자** | [bindings/overview.md](bindings/overview.md) | C++/Java/.NET/Node.js/Python 바인딩 |
| **라이브러리 개발자** | [internals/architecture.md](internals/architecture.md) | 내부 아키텍처 및 구현 상세 |
| **빌드/배포 담당자** | [build/build-guide.md](build/build-guide.md) | 빌드, 테스트, 패키징 |

---

## 사용자 가이드 (guide/)

### Core
| 문서 | 설명 |
|------|------|
| [01-overview.md](guide/01-overview.md) | zlink 개요 및 시작하기 |
| [02-core-api.md](guide/02-core-api.md) | Core C API 상세 가이드 |
| [03-0-socket-patterns.md](guide/03-0-socket-patterns.md) | 소켓 패턴 개요 및 선택 가이드 |
| [03-1-pair.md](guide/03-1-pair.md) | PAIR 소켓 (1:1 양방향) |
| [03-2-pubsub.md](guide/03-2-pubsub.md) | PUB/SUB/XPUB/XSUB 발행-구독 |
| [03-3-dealer.md](guide/03-3-dealer.md) | DEALER 소켓 (비동기 요청) |
| [03-4-router.md](guide/03-4-router.md) | ROUTER 소켓 (ID 기반 라우팅) |
| [03-5-stream.md](guide/03-5-stream.md) | STREAM 소켓 (RAW 통신) |
| [04-transports.md](guide/04-transports.md) | Transport 가이드 (tcp/ipc/inproc/ws/wss/tls) |
| [05-tls-security.md](guide/05-tls-security.md) | TLS/SSL 설정 및 보안 가이드 |
| [06-monitoring.md](guide/06-monitoring.md) | 모니터링 API 사용법 |

### Services
| 문서 | 설명 |
|------|------|
| [07-0-services.md](guide/07-0-services.md) | 서비스 계층 개요 |
| [07-1-discovery.md](guide/07-1-discovery.md) | Service Discovery 기반 인프라 |
| [07-2-gateway.md](guide/07-2-gateway.md) | Gateway 서비스 (위치투명 요청/응답) |
| [07-3-spot.md](guide/07-3-spot.md) | SPOT 토픽 PUB/SUB (위치투명 발행/구독) |

### Reference
| 문서 | 설명 |
|------|------|
| [08-routing-id.md](guide/08-routing-id.md) | Routing ID 개념 및 사용법 |
| [09-message-api.md](guide/09-message-api.md) | 메시지 API 상세 |
| [10-performance.md](guide/10-performance.md) | 성능 특성 및 튜닝 가이드 |

## 바인딩 가이드 (bindings/)

| 문서 | 설명 |
|------|------|
| [overview.md](bindings/overview.md) | 공통 개요 및 크로스 언어 API 정렬 |
| [cpp.md](bindings/cpp.md) | C++ 바인딩 (header-only RAII) |
| [java.md](bindings/java.md) | Java 바인딩 (FFM API, Java 22+) |
| [dotnet.md](bindings/dotnet.md) | .NET 바인딩 (LibraryImport, .NET 8+) |
| [node.md](bindings/node.md) | Node.js 바인딩 (N-API) |
| [python.md](bindings/python.md) | Python 바인딩 (ctypes/CFFI) |

## 내부 구조 (internals/)

| 문서 | 설명 |
|------|------|
| [architecture.md](internals/architecture.md) | 시스템 아키텍처 전체 (5계층 상세) |
| [protocol-zmp.md](internals/protocol-zmp.md) | ZMP v2.0 프로토콜 상세 |
| [protocol-raw.md](internals/protocol-raw.md) | RAW (STREAM) 프로토콜 상세 |
| [stream-socket.md](internals/stream-socket.md) | STREAM 소켓 WS/WSS 최적화 |
| [threading-model.md](internals/threading-model.md) | 스레딩 및 동시성 모델 |
| [services-internals.md](internals/services-internals.md) | 서비스 계층 내부 설계 |
| [design-decisions.md](internals/design-decisions.md) | 설계 결정 기록 |

## 빌드 및 개발 (build/)

| 문서 | 설명 |
|------|------|
| [build-guide.md](build/build-guide.md) | 빌드 방법 (CMake, 플랫폼별) |
| [cmake-options.md](build/cmake-options.md) | CMake 옵션 상세 |
| [packaging.md](build/packaging.md) | 릴리즈 및 패키징 |
| [testing.md](build/testing.md) | 테스트 전략 및 실행 |
| [platforms.md](build/platforms.md) | 지원 플랫폼 및 컴파일러 |

## 참고 (plan/)

| 문서 | 설명 |
|------|------|
| [feature-roadmap.md](plan/feature-roadmap.md) | 기능 로드맵 |
| [type-segmentation.md](plan/type-segmentation.md) | Discovery 타입 분리 계획 |
