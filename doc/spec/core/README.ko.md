[English](README.md) | [한국어](README.ko.md)

[스펙 목차](../README.ko.md)

# zlink 코어 스펙

이 스펙은 zlink 라이브러리의 공개 C 인터페이스를 정의한다.
이 섹션의 모든 요구사항을 충족하는 구현체는 적합한 zlink C 라이브러리를 구성한다.
공개 인터페이스는 `core/include/zlink.h`에 정의된다.

## 스펙 문서

| 문서 | 설명 |
|------|------|
| [errors.ko.md](errors.ko.md) | 에러 코드, 에러 문자열, 버전 조회 |
| [errno-map.ko.md](errno-map.ko.md) | send, request, reply 함수별 errno 매트릭스 |
| [context.ko.md](context.ko.md) | Context 생성, 종료, 옵션 설정 |
| [message.ko.md](message.ko.md) | 메시지 생명주기, 데이터 접근, ownership, 속성 |
| [socket/](socket/README.ko.md) | 소켓 스펙 (공통 + 타입별) |
| [monitoring.ko.md](monitoring.ko.md) | 소켓 모니터, 서비스 모니터, 피어 검사 |
| [events.ko.md](events.ko.md) | canonical 이벤트 카탈로그와 readiness 의미 |
| [service/README.ko.md](service/README.ko.md) | 서비스 계층 공통 개념과 문서 책임 분리 |
| [service/registry.ko.md](service/registry.ko.md) | 서비스 레지스트리 생성, 구성, 클러스터링 |
| [service/discovery.ko.md](service/discovery.ko.md) | 서비스 디스커버리, 구독, 피어 조회 |
| [service/spot.ko.md](service/spot.ko.md) | SPOT 토픽 기반 PUB/SUB, routed 메시징 |
| [polling.ko.md](polling.ko.md) | 프록시 헬퍼 및 기능 조회 |
| [utilities.ko.md](utilities.ko.md) | 타이머, 스레드, 스톱워치, 아토믹 |

## 타입

| 타입 | 정의 위치 | 설명 |
|------|-----------|------|
| [`zlink_msg_t`](message.ko.md) | message.ko.md | 불투명 메시지 컨테이너 (64B, 스택 할당 가능) |
| [`zlink_routing_id_t`](message.ko.md) | message.ko.md | 피어 라우팅 아이덴티티 |
| `zlink_socket_msg_handler_fn` | [socket/](socket/README.ko.md) | raw `STREAM` raw 수신 콜백 |
| [`zlink_monitor_event_t`](monitoring.ko.md) | monitoring.ko.md | 모니터 이벤트 구조체 (이벤트, 값, 주소) |
| [`zlink_monitor_snapshot_t`](monitoring.ko.md) | monitoring.ko.md | monitor snapshot (state, queue depth) |
| [`zlink_service_event_t`](events.ko.md) | events.ko.md | 서비스 모니터 이벤트 구조체 |
| [`zlink_fd_t`](polling.ko.md) | polling.ko.md | 플랫폼 의존적 파일 디스크립터 타입 |

## 콜백 타입

| 타입 | 정의 위치 | 설명 |
|------|-----------|------|
| [`zlink_socket_msg_handler_fn`](socket/README.ko.md) | socket/ | raw `STREAM`의 raw 수신 콜백 타입 |
| [`zlink_stream_packet_handler_fn`](socket/README.ko.md) | socket/ | raw `STREAM`의 packet 수신 콜백 타입 |
| [`zlink_reply_handler_fn`](socket/README.ko.md) | socket/ | 비동기 request-reply 완료 콜백 |
| [`zlink_spot_handler_fn`](service/spot.ko.md) | service/spot.ko.md | SPOT routed 메시지 dispatch 콜백 |
| [`zlink_spot_dispatch_event_handler_fn`](service/spot.ko.md) | service/spot.ko.md | SPOT dispatch 이벤트 콜백 |
| [`zlink_monitor_handler_fn`](monitoring.ko.md) | monitoring.ko.md | 소켓 모니터 이벤트 콜백 |
| [`zlink_service_monitor_handler_fn`](monitoring.ko.md) | monitoring.ko.md | 서비스 모니터 이벤트 콜백 |
| [`zlink_send_ready_handler_fn`](socket/README.ko.md) | socket/ | send-ready 전환 콜백 |
| [`zlink_free_fn`](message.ko.md) | message.ko.md | 제로카피 메시지를 위한 해제 콜백 |
| [`zlink_timer_handler_fn`](utilities.ko.md) | utilities.ko.md | 타이머 만료 콜백 |
| [`zlink_thread_fn`](utilities.ko.md) | utilities.ko.md | 스레드 진입점 함수 |

## 내부 아키텍처

공개 C API는 `core/include/zlink.h`에 정의되며, bindings를 포함한 외부 계약이다.
내부 구현은 POSD(Philosophy of Software Design) 원칙에 따라 다음 계층으로
구성되어 있다.

```
Public API Facade  →  Service Access Layer  →  Service/Socket Runtime
     (api/)            (*_access.hpp)            (services/, sockets/)
                                                      ↓
                                              Runtime Core (core/)
                                              Engine (engine/asio/)
                                              Transport/Protocol
```

| 계층 | 소스 위치 | 역할 |
|------|-----------|------|
| API Facade | `core/src/api/` | C API entrypoint; validate + delegate |
| Service Access | `core/src/services/*/` | service-local access seam (`*_access.hpp`) |
| Socket Runtime | `core/src/sockets/` | socket semantic + runtime component 분리 |
| Runtime Core | `core/src/core/` | ctx, options dispatch, multipart send, close/drain |
| Engine | `core/src/engine/` | Boost.Asio 기반 poller, io_context |

Option dispatch는 세 카테고리로 분리되어 각 도메인 소유자가 validation/apply를
담당한다: core_socket, transport_network, protocol_metadata.

내부 아키텍처 상세는 [POSD 모듈 구조 문서](../../internals/posd-module-structure.ko.md)를
참조하세요.

---

개념 가이드와 튜토리얼은 [사용자 가이드](../../guide/01-overview.ko.md)를 참조하세요.
