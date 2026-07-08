[English](README.md) | [한국어](README.ko.md)

[스펙 목차](../README.ko.md)

# zlink 코어 스펙

이 스펙은 zlink 라이브러리의 공개 core C ABI를 정의한다.
이 섹션의 모든 요구사항을 충족하는 구현체는 적합한 zlink C 라이브러리를 구성한다.
공개 ABI 표면은 `core/include/zlink.h`와 `core/include/zlink/` 아래 도메인별
헤더에 정의된다.

`core/include/zlink.h`는 기존 호환성을 위한 aggregate header로 유지한다. 새 코드는
특정 API 영역만 확인하거나 의존하고 싶을 때 도메인별 헤더를 직접 include할 수
있다. 도메인별 헤더도 내부 helper가 아니라 public ABI header다.

## 공개 ABI 헤더 구조

| 헤더 | 공개 ABI 영역 |
|------|---------------|
| `core/include/zlink.h` | 모든 도메인 헤더를 포함하는 aggregate public header |
| `core/include/zlink/common.h` | 버전 매크로, 공통 include, export 매크로, enum/error include |
| `core/include/zlink/core/api.h` | errno/string/version helper, context lifecycle, proxy, 기능(capability) 조회, atomic, stopwatch, sleep, thread utility |
| `core/include/zlink/message/api.h` | 메시지 저장소, routing id, zero-copy free callback, message lifecycle, multipart close |
| `core/include/zlink/service/actor.h` | Actor 값 타입과 Actor result 구조체 |
| `core/include/zlink/socket/api.h` | socket 생성, option, TLS, bind/connect, send/recv part substrate, request/reply, pub/sub, stream, dispatch event handler, socket callback type |
| `core/include/zlink/eventing/api.h` | socket monitor, monitor snapshot, poll/poller, timer |
| `core/include/zlink/service/spot.h` | SPOT handle, SPOT node, Actor operation, route bridge, publisher handle |
| `core/include/zlink/service/common.h` | service 계층 공통 조회 타입 |
| `core/include/zlink_enum.h` | 공개 enum domain |
| `core/include/zlink_errno.h` | 공개 errno domain |

`core/src/`는 runtime implementation이다. `core/src/` 아래 헤더는 여러 core
translation unit이 함께 include하더라도 내부 구현 계약이며 public ABI가 아니다.

## 스펙 문서

| 문서 | 설명 |
|------|------|
| [errors.ko.md](errors.ko.md) | 에러 코드, 에러 문자열, 버전 조회 |
| [errno-map.ko.md](errno-map.ko.md) | send, request, reply 함수별 errno 매트릭스 |
| [context.ko.md](context.ko.md) | Context 생성, 종료, 옵션 설정 |
| [message.ko.md](message.ko.md) | 메시지 생명주기, 데이터 접근, ownership, 속성 |
| [socket/](socket/README.ko.md) | 소켓 스펙 (공통 + 타입별) |
| [monitoring.ko.md](monitoring.ko.md) | 소켓 모니터, monitor snapshot, 피어 검사 |
| [events.ko.md](events.ko.md) | canonical 이벤트 카탈로그와 readiness 의미 |
| [service/README.ko.md](service/README.ko.md) | 서비스 계층 공통 개념과 문서 책임 분리 |
| [service/spot.ko.md](service/spot.ko.md) | SPOT 토픽 기반 PUB/SUB, route bridge, routed 메시징 |
| [polling.ko.md](polling.ko.md) | 프록시 헬퍼 및 기능 조회 |
| [utilities.ko.md](utilities.ko.md) | 타이머, 스레드, 스톱워치, 아토믹 |

## 타입

| 타입 | 정의 위치 | 설명 |
|------|-----------|------|
| [`zlink_msg_t`](message.ko.md) | message.ko.md | 불투명 메시지 컨테이너 (64B, 스택 할당 가능) |
| [`zlink_routing_id_t`](message.ko.md) | message.ko.md | 피어 라우팅 아이덴티티 |
| `zlink_socket_msg_handler_fn` | [socket/](socket/README.ko.md) | raw `STREAM` raw 수신 콜백 |
| [`zlink_monitor_event_t`](monitoring.ko.md) | monitoring.ko.md | 모니터 이벤트 구조체 (이벤트, 값, 주소) |
| [`zlink_monitor_status_t`](monitoring.ko.md) | monitoring.ko.md | monitor snapshot (state, queue depth) |
| [`zlink_fd_t`](polling.ko.md) | polling.ko.md | 플랫폼 의존적 파일 디스크립터 타입 |

## 콜백 타입

| 타입 | 정의 위치 | 설명 |
|------|-----------|------|
| [`zlink_socket_msg_handler_fn`](socket/README.ko.md) | socket/ | raw `STREAM`의 raw 수신 콜백 타입 |
| [`zlink_stream_packet_handler_fn`](socket/README.ko.md) | socket/ | raw `STREAM`의 packet 수신 콜백 타입 |
| [`zlink_reply_handler_fn`](socket/README.ko.md) | socket/ | 비동기 request-reply 완료 콜백 |
| [`zlink_spot_dispatch_event_handler_fn`](service/spot.ko.md) | service/spot.ko.md | SPOT dispatch 이벤트 콜백 |
| [`zlink_monitor_handler_fn`](monitoring.ko.md) | monitoring.ko.md | 소켓 모니터 이벤트 콜백 |
| [`zlink_send_ready_handler_fn`](socket/README.ko.md) | socket/ | send-ready 전환 콜백 |
| [`zlink_free_fn`](message.ko.md) | message.ko.md | 제로카피 메시지를 위한 해제 콜백 |
| [`zlink_timer_handler_fn`](utilities.ko.md) | utilities.ko.md | 타이머 만료 콜백 |
| [`zlink_thread_fn`](utilities.ko.md) | utilities.ko.md | 스레드 진입점 함수 |

## 내부 아키텍처

공개 C ABI는 `core/include/`에 정의되며, bindings가 사용하는 외부 core 계약이다.
내부 구현은 POSD(Philosophy of Software Design) 원칙에 따라 다음 계층으로 구성되어
있다.

```text
Public Contract  ->  API Facade  ->  Runtime Implementation
 (include/)             (api/)            (runtime/)
```

| 계층 | 소스 위치 | 역할 |
|------|-----------|------|
| Public Contract | `core/include/` | bindings와 사용자가 보는 공개 C ABI 계약 |
| API Facade | `core/src/api/` | 공개 C ABI 함수 구현체. 입력 검증, 결과 변환, runtime 호출 |
| Runtime Implementation | `core/src/runtime/` | socket, service, engine, transport, protocol 등 내부 구현 |

`core/src/api/`는 `core/include/`에 선언된 공개 C ABI 함수에 대응하는 구현체
계층이다. 이 계층은 외부 입력을 검증하고 공개 result 타입으로 결과를 변환한 뒤,
실제 동작은 `core/src/runtime/` 아래 내부 구현으로 위임한다. `core/src/api/` 자체도
public header가 아니며 설치 대상이 아니다.

`core/src/api/` 아래 구조는 공개 contract 헤더의 도메인과 대응되도록 다음
카테고리로 고정한다.

```text
core/src/api/
|-- actor/
|-- core/
|-- discovery/
|-- message/
|-- monitoring/
|-- registry/
|-- service/
|-- socket/
`-- spot/
```

`core/src/runtime/` 아래 구조는 다음 카테고리로 고정한다.

```text
core/src/runtime/
|-- core/
|-- engine/
|-- protocol/
|-- services/
|   |-- actor/
|   |-- common/
|   |-- control/
|   |-- discovery/
|   |-- registry/
|   `-- spot/
|-- sockets/
|   |-- common/
|   |-- dealer/
|   |-- internal/
|   |-- pair/
|   |-- proxy/
|   |-- pubsub/
|   |-- router/
|   `-- stream/
|-- transports/
`-- utils/
```

Option dispatch는 세 카테고리로 분리되어 각 도메인 소유자가 validation/apply를
담당한다: core_socket, transport_network, protocol_metadata.

내부 아키텍처 상세는 [POSD 모듈 구조 문서](../../internals/posd-module-structure.ko.md)를
참조하세요.

---

개념 가이드와 튜토리얼은 [사용자 가이드](../../guide/01-overview.ko.md)를 참조하세요.
