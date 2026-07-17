[English](03-0-socket-patterns.md) | [한국어](03-0-socket-patterns.ko.md)

<!-- zlink-nav:start -->
[← Core API](02-core-api.ko.md) | [PAIR →](03-1-pair.ko.md)
<!-- zlink-nav:end -->

# 소켓 패턴 개요 및 선택 가이드

## 1. 개요

zlink는 8종의 소켓 타입을 제공한다.
각 소켓은 고유한 메시징 패턴을 구현하며, 유효한 소켓 조합 안에서만 통신한다.

> 이 문서 전체에서 사용되는 **hot path**, **control path**, **admission guard** 등의 용어는 [9절 (용어 정리)](#9-용어-정리)에 정의되어 있다.

## 2. 소켓 요약

| 소켓 | 패턴 | 방향 | 라우팅 전략 | 주요 용도 |
|------|------|------|-------------|-----------|
| **PAIR** | 1:1 양방향 | 양방향 | 단일 파이프 (1:1 독점) | 스레드 간 시그널링, 워커 조정 |
| **PUB** | 발행 | 단방향 (송신) | `dist_t` (Fan-out) | 이벤트 브로드캐스트 |
| **SUB** | 구독 | 단방향 (수신) | `fq_t` (Fair-queue) | 토픽 필터링 수신 |
| **XPUB** | 고급 발행 | 양방향 | `dist_t` + 구독 수신 | 프록시/브로커, 구독 모니터링 |
| **XSUB** | 고급 구독 | 단방향 (수신) | `fq_t` (필터 없이 전체 수신) | 프록시/브로커 |
| **DEALER** | 비동기 요청 | 양방향 | 송신: `lb_t` (Round-robin), 수신: `fq_t` | 로드밸런싱, 비동기 요청 |
| **ROUTER** | ID 라우팅 | 양방향 | routing_id 기반 지정 전송 | 서버, 브로커, 멀티 클라이언트 |
| **STREAM** | RAW 통신 | 양방향 | routing_id 기반 (4B uint32) | 외부 클라이언트 연동 |

## 3. 소켓 호환성 매트릭스

유효한 소켓 조합만 연결된다. 비호환 소켓을 연결하면 핸드셰이크가 실패한다.

| 소켓 | PAIR | PUB | SUB | XPUB | XSUB | DEALER | ROUTER | STREAM |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **PAIR** | **O** | | | | | | | |
| **PUB** | | | **O** | | **O** | | | |
| **SUB** | | **O** | | **O** | | | | |
| **XPUB** | | | **O** | | **O** | | | |
| **XSUB** | | **O** | | **O** | | | | |
| **DEALER** | | | | | | **O** | **O** | |
| **ROUTER** | | | | | | **O** | **O** | |
| **STREAM** | | | | | | | | **외부** |

> STREAM 소켓은 zlink 내부 소켓과 호환되지 않으며, 외부 RAW 클라이언트와만 통신한다.

## 4. 라우팅 전략 요약

| 전략 | 동작 | 사용 소켓 |
|------|------|-----------|
| **단일 파이프** | 하나의 피어와만 통신 (N:1 불가) | PAIR |
| **Round-robin** (`lb_t`) | 연결된 피어에 순환 분배 | DEALER 송신 |
| **Fair-queue** (`fq_t`) | 모든 피어에서 공정하게 수신 | DEALER/SUB 수신 |
| **Fan-out** (`dist_t`) | 모든 구독자에게 복제 전송 | PUB/XPUB |
| **ID 라우팅** | routing_id 프레임으로 특정 피어 지정 | ROUTER/STREAM |

> `lb_t`, `fq_t`, `dist_t`는 소스 트리와 internals 문서에 등장하는 **내부 구현 타입 이름**이다. 괄호 안의 표현(Round-robin, Fair-queue, Fan-out)이 일상적으로 쓰는 기능 설명이며, 소스를 직접 수정하지 않는다면 내부 타입 이름까지 알 필요는 없다.

> 라우팅 전략의 내부 구현 상세는 [architecture.md](../internals/architecture.ko.md)를 참고.

## 5. 패턴 선택 가이드

### 의사결정 플로우

```
Is the communication peer an external client (browser, game)?
+-- Yes → STREAM (ws/wss/tcp/tls)
+-- No → Communication between zlink sockets
         +-- Is it 1:1 exclusive?
         |   +-- Yes → PAIR
         +-- No → N:M communication
              +-- Publish-subscribe (broadcast)?
              |   +-- Proxy/broker needed → XPUB/XSUB
              |   +-- Simple pub-sub → PUB/SUB
              +-- Request-reply / routing?
                  +-- DEALER/ROUTER
```

### 사용 사례별 추천

| 사용 사례 | 추천 패턴 | 설명 |
|-----------|-----------|------|
| 스레드 간 시그널링 | PAIR + inproc | 가장 빠른 1:1 통신 |
| 이벤트 브로드캐스트 | PUB/SUB | 토픽 기반 필터링 |
| 메시지 브로커/프록시 | XPUB/XSUB | 구독 메시지 접근 및 변환 |
| 비동기 요청-응답 서버 | DEALER↔DEALER | 비동기 양방향 통신 |
| 로드밸런싱 | 다중 DEALER → ROUTER | Round-robin 분배 |
| 특정 피어 전송 | ROUTER | routing_id로 대상 지정 |
| 웹 클라이언트 연동 | STREAM + ws/wss | WebSocket RAW 통신 |
| 외부 TCP 클라이언트 | STREAM + tcp/tls | Length-Prefix RAW 통신 |

> 위치 투명성이 필요한 경우(자동 연결 · 로드밸런싱 · topic mesh)에는
> 소켓 대신 서비스 레이어(SPOT)를 사용한다.
> 상세는 [서비스 개요](07-0-services.ko.md)를 참고.

## 6. 하위 문서

각 소켓 타입의 상세 사용법은 개별 문서를 참고한다.

| 문서 | 소켓 | 설명 |
|------|------|------|
| [03-1-pair.ko.md](03-1-pair.ko.md) | PAIR | 1:1 양방향 독점 연결 |
| [03-2-pubsub.ko.md](03-2-pubsub.ko.md) | PUB/SUB/XPUB/XSUB | 발행-구독 패밀리 |
| [03-3-dealer.ko.md](03-3-dealer.ko.md) | DEALER | 비동기 요청, Round-robin |
| [03-4-router.ko.md](03-4-router.ko.md) | ROUTER | ID 기반 라우팅 |
| [03-5-stream.ko.md](03-5-stream.ko.md) | STREAM | 외부 클라이언트 RAW 통신 |
| [03-6-proxy.ko.md](03-6-proxy.ko.md) | (proxy) | XPUB/XSUB·DEALER/ROUTER 메시지 브로커 |

## 7. 피어를 routing id로 끊기

일반적인 연결/해제 수명 주기는 엔드포인트 문자열을 기준으로 동작한다. 그런데
STREAM(또는 ROUTER typed recv)에서 메시지를 수신하면 `source_rid`(송신 피어의 고유
식별자)로 상대방을 직접 특정할 수 있다. 엔드포인트 문자열을 저장하지 않고 수신한
`source_rid`만으로 해당 피어 연결을 끊으려면 `zlink_disconnect_rid()`를 사용한다.

```c
zlink_connect_result_t rc = zlink_disconnect_rid(socket, &source_rid);
```

대상이 없으면 `ZLINK_CONNECT_NOT_FOUND`, 같은 routing id를 가진 peer가 둘
이상이면 `ZLINK_CONNECT_CONFLICT`, 연결 lifecycle을 다른 소유자(상위 runtime)가
관리하는 소켓이면 `ZLINK_CONNECT_BUSY`가 반환된다.

## 8. 공통 수신 인터페이스

zlink는 raw socket family의 기본 수신 모델을 `recv + poller` 조합으로
고정한다. 서버 루프가 poller로 `ZLINK_POLLIN`을 관찰한 뒤, 그 소켓에 맞는
recv 계열 함수로 데이터를 가져오는 방식이 표준이다. `zlink_recv()`는 이
모델의 가장 일반적인 진입점이다.

```c
zlink_recv_result_t zlink_recv (
    void *socket,
    zlink_routing_id_t *source_rid,   /* sender routing_id */
    zlink_msg_t **parts,              /* multipart data */
    size_t *part_count,               /* frame count */
    zlink_recv_flags_t flags);
```

- **`source_rid`**: STREAM 공통 recv에서 송신 피어의 routing_id가 채워진다
  (PAIR/DEALER는 NULL). PUB/SUB/XPUB/XSUB/ROUTER는 이 공통 recv 대신 패턴별 typed
  recv를 쓰며, ROUTER typed recv가 source rid를 별도로 돌려준다. 메시지 프레임이
  아니라 zlink가 피어의 identity를 자동으로 resolve해 전달하는 별도 파라미터다.
- **`parts` / `part_count`**: 모든 소켓에서 멀티파트가 기본이다.
  `part_count=1`이면 단일 프레임, `part_count=2+`이면 멀티파트.

> **libzmq와의 차이:** libzmq ROUTER는 `zmq_recv()` 첫 프레임이
> routing_id였지만, zlink는 routing_id를 별도 파라미터로 분리했다.

**소켓별 전용 수신 표면:**

- **PAIR / DEALER**: `zlink_recv()`로 수신한다. DEALER는 추가로
  `zlink_dealer_request()`의 완료 콜백으로 reply를 받는다.
- **ROUTER**: ROUTER 핸들에 `zlink_recv()`를 호출하면
  `ZLINK_RECV_NOT_SUPPORTED`로 실패한다. ROUTER는 통합된 단일 typed
  표면 — `zlink_router_recv()` — 을 사용하며, `source_node_rid`와
  `request_seq`를 함께 반환한다. request의 reply는 별도 완료 콜백으로
  받는다. 자세한 내용은 [03-4-router.ko.md](03-4-router.ko.md).
- **SUB / XSUB**: `zlink_subscribe()`로 수신한다. recv-only이며, 직접
  토픽 콜백 표면은 제공하지 않는다.
- **STREAM**: 예외 타입이다. `zlink_recv()` (raw recv),
  `zlink_recv_handler()` (raw 콜백), `zlink_stream_packet_handler()`
  (packet 콜백) 세 모델 중 하나를 고른다. 한 핸들에서 두 번째 모델로
  전환하려 하면 `EBUSY`로 실패한다.
- **MeshNode/Spot/Actor**: ready handler 또는 poller로 readiness를 통합 수신하고, ready/claim/receive batch로 record를 읽는다([07-3 SPOT](07-3-spot.ko.md) §5).
- **monitor / 타이머**: recv와 콜백 두 방식을 모두 지원한다.

data-plane 수신은 `recv + poller`가 기본이며, 콜백은 `STREAM`, monitor/timer처럼 사용 패턴이 분명한 예외 타입에만 쓴다. MeshNode ready handler는 wakeup 신호일 뿐이고 payload는 receive API로 읽는다. request completion
콜백은 data-plane 수신이 아니라 비동기 작업 완료 통지임에 유의한다.

## 9. 용어 정리

문서 전반에서 사용되는 전문 용어:

| 용어 | 의미 |
|------|------|
| **핫 패스(hot path, 고빈도 데이터 경로)** | 고빈도로 호출되는 경로. `send`, `publish` 등 데이터 전송 API. 동시 호출에 최적화되어 있다 |
| **제어 경로(control path)** | 저빈도로 호출되는 경로. `bind`, `connect`, `set_option`, `monitor` 등 설정/관리 API. 내부 직렬화로 correctness를 보장한다 |
| **correctness** | 여러 스레드가 같은 handle을 동시에 사용해도 데이터 손상이나 크래시 없이 올바르게 동작하는 성질 |
| **fail-fast lifecycle gate** | `close`/`destroy` 호출 시 다른 스레드가 사용 중이면 즉시 `EBUSY`를 반환하고, close가 수락된 뒤 새 API 진입은 `ESHUTDOWN`을 반환하는 종료 계약 |
| **admission guard** | API 진입 시 handle이 유효한지, 이미 종료 중인지를 검사하는 내부 게이트 |
| **approximate limit** | 정확한 hard limit이 아닌 근사치 제한. HWM(High-Water Mark, 큐 최대 허용 메시지 수)은 락-프리 성능을 위해 소폭 초과를 허용한다 |

> 스레드 안전성 계약의 전체 설명은 [스레드 안전성 가이드](11-thread-safety.ko.md)를 참고.

## 10. 기본 사용 흐름

모든 소켓 타입에 공통되는 기본 패턴은 `recv + poller` 루프다. 예를 들어
DEALER에서 응답을 받는 서버는 아래와 같은 형태를 쓴다.

```c
/* 1. Create Context */
void *ctx = zlink_ctx_new();

/* 2. Create Socket */
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);

/* 3. Set options (before bind/connect) */
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

/* 4. Establish connection */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* 5. Poll and recv */
void *poller = zlink_poller_new();
zlink_poller_add(poller, socket, user_data, ZLINK_POLLIN);

while (running) {
    zlink_poller_event_t ev;
    if (zlink_poller_wait(poller, &ev, 1, timeout_ms, NULL) <= 0) continue;
    if (ev.events & ZLINK_POLLIN) {
        zlink_routing_id_t rid;
        zlink_msg_t *parts = NULL;
        size_t n = 0;
        if (zlink_recv(socket, &rid, &parts, &n, 0) == ZLINK_RECV_OK) {
            /* process parts, then close each */
            zlink_multipart_close(parts, n);
        }
    }
}

/* 6. Cleanup */
zlink_poller_destroy(&poller);
zlink_close(socket);
zlink_ctx_term(ctx);
```

> 다음 옵션은 핸드셰이크/연결 과정에서 사용되므로
> `zlink_bind()`/`zlink_connect()` **이전에** 설정해야 한다:
>
> - `zlink_set_routing_id()`
> - `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (`zlink_set_router_option()` 사용)
> - `ZLINK_ROUTER_OPT_PROBE` (`zlink_set_router_option()` 사용)
> - `zlink_set_tls_server()` / `zlink_set_tls_client()`
>
> 그 외 옵션(`SNDHWM`, `RCVHWM`, `LINGER`, `SNDTIMEO` 등)은
> bind/connect 이후에도 변경 가능하다.

> **콜백이 기본이 아닌 이유:** raw `PAIR`, `DEALER`, `SUB`, `XSUB`,
> `ROUTER`는 동기 pull-mode 루프로 수신한다(recv 콜백 없음). 타입이
> 허용하는 한 송신은 그대로 가능하다. 여러 소켓, monitor, 타이머를 같은 poller에서
> 다루기 쉽고, 호출자가 실행 스레드와 순서를 직접 통제할 수 있기 때문이다.
> 콜백은 `STREAM`, monitor/타이머, SPOT dispatch event, request
> completion처럼 사용 패턴이 분명한 경우에만 쓴다.

---
<!-- zlink-nav:bottom:start -->
[← Core API](02-core-api.ko.md) | [PAIR →](03-1-pair.ko.md)
<!-- zlink-nav:bottom:end -->
