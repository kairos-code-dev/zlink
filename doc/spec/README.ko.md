[English](./README.md) | [한국어](./README.ko.md)

# zlink 라이브러리 스펙

이 스펙은 zlink 라이브러리의 공개 인터페이스를 정의한다.
적합한 구현체는 이 문서에 기술된 모든 함수, 타입, 상수를
명시된 의미론대로 제공해야 한다(MUST).

## 스펙 구조

| 섹션 | 경로 | 설명 |
|------|------|------|
| **코어 스펙** | [core/](./core/README.ko.md) | C 라이브러리 스펙 (`zlink.h`) |
| **바인딩 스펙** | [bindings/](./bindings/README.ko.md) | 언어별 바인딩 계약 및 API 스펙 |

## 코어 스펙 (core/)

코어 스펙은 C 라이브러리 인터페이스를 정의한다.
이 섹션의 모든 요구사항을 충족하는 구현체는 적합한 zlink C 라이브러리를 구성한다.

| 문서 | 설명 |
|------|------|
| [errors.ko.md](./core/errors.ko.md) | 에러 코드, 에러 문자열, 버전 조회 |
| [errno-map.ko.md](./core/errno-map.ko.md) | send, request, reply 함수별 errno 매트릭스 |
| [context.ko.md](./core/context.ko.md) | Context 생성, 종료, 옵션 설정 |
| [message.ko.md](./core/message.ko.md) | 메시지 생명주기, 데이터 접근, ownership, 속성 |
| [socket/](./core/socket/README.ko.md) | 소켓 스펙 (공통 + 타입별) |
| [monitoring.ko.md](./core/monitoring.ko.md) | 소켓 모니터, monitor snapshot, 피어 검사 |
| [events.ko.md](./core/events.ko.md) | canonical 이벤트 카탈로그와 readiness 의미 |
| [service/README.ko.md](./core/service/README.ko.md) | 서비스 계층 공통 개념과 문서 책임 분리 |
| [registry.ko.md](./core/service/registry.ko.md) | 서비스 레지스트리 생성, 구성, 클러스터링 |
| [discovery.ko.md](./core/service/discovery.ko.md) | 서비스 디스커버리, 구독, 피어 조회 |
| [spot.ko.md](./core/service/spot.ko.md) | SPOT 토픽 기반 PUB/SUB, routed 메시징 |
| [polling.ko.md](./core/polling.ko.md) | 프록시 헬퍼 및 기능 조회 |
| [utilities.ko.md](./core/utilities.ko.md) | 타이머, 스레드, 스톱워치, 아토믹 |

## 바인딩 스펙 (bindings/)

바인딩 스펙은 코어 C 계약이 각 대상 언어로 어떻게 투영되는지 정의한다.
cross-language 정책과 언어별 스펙을 충족하는 구현체는 적합한 zlink 바인딩을 구성한다.
바인딩 라이브러리를 설계할 때는
[POSD 설계 원칙](../principal/software-design-principles.ko.md)을 따른다.
이는 언어별 API가 내부 구현 세부사항을 드러내지 않고, 호출자가 알아야 할
개념을 줄이며, 깊은 모듈과 낮은 변경 파급을 유지하도록 하기 위한 기준이다.

| 문서 | 설명 |
|------|------|
| [정책](./bindings/README.ko.md) | Cross-language 바인딩 계약 (POSD, 역할 matrix, naming, domain object) |
| [C](./bindings/c/README.ko.md) | C 바인딩 스펙 |
| [C++](./bindings/cpp/README.ko.md) | C++ 바인딩 스펙 |
| [Java](./bindings/java/README.ko.md) | Java 바인딩 스펙 |
| [.NET](./bindings/dotnet/README.ko.md) | .NET 바인딩 스펙 |
| [Node.js](./bindings/node/README.ko.md) | Node.js 바인딩 스펙 |
| [Python](./bindings/python/README.ko.md) | Python 바인딩 스펙 |
| [Go](./bindings/go/README.ko.md) | Go 바인딩 스펙 |
| [Rust](./bindings/rust/README.ko.md) | Rust 바인딩 스펙 |

## 적합성 (Conformance)

적합한 구현체는:

1. 코어 스펙의 모든 함수, 타입, 상수를 문서화된 시그니처와 의미론으로 구현해야 한다 (**MUST**).
2. 각 API에 명시된 보장(guarantee)과 제약(constraint)을 충족해야 한다 (**MUST**).
3. 내부 구현 세부사항을 공개 인터페이스에 노출하면 안 된다 (**MUST NOT**).
4. 언어 바인딩은 cross-language 정책과 해당 언어별 스펙을 따라야 한다 (**MUST**).

## 용어

- **MUST** / **MUST NOT**: 절대 요구사항.
- **SHOULD** / **SHOULD NOT**: 강한 권고; 이탈 시 사유가 필요.
- **MAY**: 선택적 동작.
