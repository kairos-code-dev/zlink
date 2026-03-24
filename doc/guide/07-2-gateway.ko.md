[English](07-2-gateway.md) | [한국어](07-2-gateway.ko.md)

# Gateway 서비스 (위치투명 요청/응답)

## 1. 개요

Gateway는 Discovery 기반으로 서비스를 자동 발견하고, 로드밸런싱된 메시지
전송을 지원하며 recv 모드로 시작하는 통합 서비스 핸들이다. 하나의 Gateway
핸들이 클라이언트(송신)와 서버(수신) 역할을 모두 수행할 수 있고, receive
callback과 send-ready callback을 독립적으로 선택할 수 있다.

> **명칭에 대하여**: Gateway는 특정 서비스에 대한 접근점(entry point)이자
> 클라이언트 사이드 로드밸런서다. API Gateway(Kong, AWS API Gateway 등)처럼
> 인증·rate limiting·프로토콜 변환을 포함하는 개념이 아니라, 서비스 접근 +
> 로드밸런싱에 집중하는 경량 게이트웨이를 의미한다.

**Gateway는 thread-safe하다.** 하나의 Gateway handle을 여러 스레드에서
동시에 사용할 수 있다. `send` / `send_rid`는 여러 스레드에서 동시 호출을
허용하는 hot path(고빈도 데이터 경로)이고, attach/option/monitor/query 계열은
runtime에 호출 가능한 control path(저빈도 설정/관리 경로)이며,
`destroy`는 fail-fast lifecycle gate(사용 중이면 `EBUSY`, 종료 후 `ESHUTDOWN`)를
가진다.

## 2. Gateway 생성

Gateway는 생성 시점에 서비스 이름만 고정한다. routing id와 I/O 모델 설정은
후속 단계로 분리된다.

Gateway는 **recv 모드**로 시작한다.
- `zlink_recv_handler(gateway, ...)`는 지원되며 receive surface를 callback 모드로 전환한다.
- receive callback attach 이후 direct recv와 data-plane `ZLINK_POLLIN`은 `EBUSY`로 실패한다.
- `zlink_send_ready_handler(gateway, ...)`는 독립적으로 지원된다.
- send-ready attach 이후 data-plane `ZLINK_POLLOUT`은 `EBUSY`로 실패한다.

### Recv 모드

```c
void *gateway = zlink_gateway_new(ctx, "payment-service");
zlink_set_routing_id(gateway, "gateway-1", 9);
/* 콜백 없음 -- recv 모드 유지, zlink_gateway_recv()로 수신 */
```

## 3. 서버 (수신) 측 설정

서버 역할을 하려면 Gateway에 endpoint를 bind하고, Discovery를 통해
Registry에 등록한다. 서버 측 수신은 `zlink_gateway_recv()`로 모델링한다.

### Recv 모드 서버

```c
void *ctx = zlink_ctx_new();

void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

void *server = zlink_gateway_new(ctx, "payment-service");
zlink_set_routing_id(server, "payment-server-1", 16);
/* recv 모드 유지 -- zlink_recv_handler() 호출 없음 */

zlink_gateway_attach_discovery(server, discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* 애플리케이션 루프에서 메시지를 직접 수신 */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
while (zlink_gateway_recv(server, &source_rid, &parts, &part_count, 0) == 0) {
    /* 요청 처리 후 응답 */
    zlink_gateway_send_rid(server, &source_rid, parts, part_count, 0);
}
```

## 4. 클라이언트 (송신) 측 설정

Gateway 클라이언트도 recv 모드를 유지한다. Discovery를 연결하고 send 이후
`zlink_gateway_recv()`로 응답을 수신한다.

### Recv 모드 클라이언트

```c
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

void *client = zlink_gateway_new(ctx, "payment-service");
zlink_set_routing_id(client, "client-1", 8);
/* recv 모드 유지 */

zlink_gateway_attach_discovery(client, discovery);
zlink_gateway_set_lb_strategy(client, ZLINK_GATEWAY_LB_ROUND_ROBIN);
```

## 5. 메시지 전송

### 5.1 로드밸런싱 전송

```c
/* 멀티파트 메시지 구성 및 전송 */
zlink_msg_t part;
zlink_msg_init_size(&part, 7);
memcpy(zlink_msg_data(&part), "request", 7);
zlink_gateway_send(client, &part, 1, 0);
```

### 5.2 특정 피어 전송

```c
/* routing_id로 특정 서버에 직접 전송 */
zlink_gateway_send_rid(client, &target_rid, &part, 1, 0);
```

### 5.3 메시지 수신

Gateway는 recv 모드를 유지한다. 요청/응답은 `zlink_gateway_recv()`로 직접
수신한다.

#### Recv 모드 (기본)

```c
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_gateway_recv(client, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    printf("응답: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

callback 기반 수신이 필요하면 `zlink_recv_handler(gateway, ...)`를 사용한다.
이 모드에서는 `zlink_gateway_recv()` / `zlink_recv()`와 data-plane
`ZLINK_POLLIN`이 `EBUSY`로 실패한다.

## 6. 로드밸런싱

| 전략 | 상수 | 설명 |
|------|------|------|
| Round Robin | `ZLINK_GATEWAY_LB_ROUND_ROBIN` | 순차 선택 (기본) |
| Weighted | `ZLINK_GATEWAY_LB_WEIGHTED` | 가중치 기반 (weight 높을수록 선택 확률 높음) |

### 가중치 갱신

```c
/* 특정 피어의 가중치를 갱신 */
zlink_gateway_update_peer_weight(server, &peer_rid, 5);
```

## 7. Thread-Safety

### 일반 소켓 vs Gateway

| 항목 | 일반 공개 socket handle | Gateway |
|------|---|---|
| **스레드 안전성** | 기본적으로 thread-safe | thread-safe |
| **고빈도 경로** | `send`가 hot path | `send` / `send_rid`가 hot path |
| **저빈도 경로** | bind/connect 등 직렬화 | attach/option 등 직렬화 |
| **종료** | `close` fail-fast gate | `destroy` fail-fast gate |

### Thread-safe API

Gateway는 "모든 API가 같은 비용 모델"인 것은 아니지만, 공개 handle API는
기본적으로 thread-safe다.

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_set_option()`
- `zlink_gateway_attach_discovery()`
- `zlink_gateway_bind()`
- `zlink_gateway_connect()` / `zlink_gateway_disconnect()`
- `zlink_set_tls_client()` / `zlink_set_tls_server()`
- `zlink_get_option(gateway, ZLINK_OPT_LAST_ENDPOINT, ...)`
- open한 gateway monitor에 대한 `zlink_monitor_snapshot()`
- `zlink_gateway_destroy()`

사용자가 외우면 되는 규칙은 네 가지다.

1. Gateway handle은 여러 스레드에서 공유해도 된다.
2. `send` / `send_rid`는 여러 스레드에서 동시 호출이 가능하다.
3. control-path API도 runtime에 호출할 수 있다.
4. `destroy`는 fail-fast이며 admitted API가 있으면 `EBUSY`, accepted 이후 새
   진입은 `ESHUTDOWN`이다.

### 멀티스레드 사용 예제

```c
/* Gateway는 thread-safe하므로 여러 스레드에서 공유 가능 */
void *gateway = zlink_gateway_new(ctx, "my-service");
zlink_set_routing_id(gateway, "gw-1", 4);
zlink_gateway_attach_discovery(gateway, discovery);

/* 워커 스레드 함수 */
void *send_worker(void *arg) {
    void *gw = arg;
    zlink_msg_t part;
    zlink_msg_init_size(&part, 7);
    memcpy(zlink_msg_data(&part), "request", 7);
    /* 여러 스레드에서 동시에 send 호출 — 안전 */
    zlink_gateway_send(gw, &part, 1, 0);
    return NULL;
}

/* 여러 스레드에서 동시 전송 */
for (int i = 0; i < 4; i++)
    zlink_thread_start(&send_worker, gateway);
```

### 장점

**1. Hot path 중심의 낮은 경합**

Gateway의 `send` / `send_rid`는 고빈도 경로를 기준으로 설계되어 있어, control
path와 다른 비용 모델을 사용한다.

**2. 애플리케이션 아키텍처 단순화**

Gateway는 추가 프록시 계층 없이 여러 스레드가 같은 handle에 직접 send를
호출할 수 있다.

```
일반 소켓 (멀티스레드):
  Thread A ──┐
  Thread B ──┼── inproc 큐 ── 전용 I/O 스레드 ── ROUTER 소켓
  Thread C ──┘

Gateway (멀티스레드):
  Thread A ──┐
  Thread B ──┼── Gateway ── send ──→ Server
  Thread C ──┘
```

**3. Discovery 갱신이 send를 블록하지 않음**

서비스 풀 갱신(서버 추가/제거, 연결/재연결)은 전용 백그라운드 워커 스레드가
처리한다. send 호출 중에 Discovery 이벤트가 도착해도 사용자 API가 블록되지 않는다.

**4. 동시 전송과 가중치 갱신이 안전**

여러 스레드가 동시에 메시지를 전송하면서, 동시에 서버가
`zlink_gateway_update_peer_weight()`로 가중치를 갱신해도 데이터 경합 없이
안전하게 처리된다.

> 참고: `core/tests/discovery/test_gateway.cpp` — `test_gateway_concurrent_send_and_updates()`: 다중 스레드 동시 전송 + 가중치 갱신 검증

> 전체 three-tier 계약과 추가 패턴은 [스레드 안전성 가이드](11-thread-safety.ko.md)를 참고.

## 8. 자동 연결/해제

Gateway는 Discovery 이벤트를 받아 자동으로 피어를 연결/해제한다.

- 서버 추가: 신규 서버에 자동 connect
- 서버 제거: 제거된 서버 disconnect

## 내부 모듈 구조

Gateway의 내부 구현은 단일 파일이 아닌 책임별 모듈로 분리되어 있다.
공개 C API는 변경 없이 유지되며, 내부 변경이 좁은 범위에서 이루어진다.

| 모듈 | 역할 |
|------|------|
| `gateway_access` | API 계층과의 seam (service-local access) |
| `gateway_facade` | 외부 API 위임 처리 |
| `gateway_lifecycle` | 생성/종료/attach 시퀀스 |
| `gateway_pool` | 피어 풀 관리, 로드밸런싱 |
| `gateway_socket` | 내부 ROUTER 소켓 wiring |
| `gateway_monitor` | 서비스 모니터 이벤트 발행 |
| `gateway_refresh` | Discovery 기반 피어 갱신 |

멀티파트 송신은 공통 `multipart_send_txn` 모듈을 사용하여
whole-message 보장(전체 성공 또는 전체 실패)을 제공한다.

## 9. End-to-End 예제

### Recv 모드

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* === Server === */
void *server_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(server_discovery, "tcp://127.0.0.1:5551");

void *server = zlink_gateway_new(ctx, "echo-service");
zlink_set_routing_id(server, "echo-server-1", 13);
zlink_gateway_attach_discovery(server, server_discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* === Client === */
void *client_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(client_discovery, "tcp://127.0.0.1:5551");

void *client = zlink_gateway_new(ctx, "echo-service");
zlink_set_routing_id(client, "client-1", 8);
zlink_gateway_attach_discovery(client, client_discovery);

/* gateway monitor로 route readiness 대기 */
/* (ZLINK_GATEWAY_ROUTE_UP 이벤트 또는 zlink_monitor_snapshot 사용) */

/* 요청 전송 */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_gateway_send(client, &part, 1, 0);

/* ... on_request 핸들러에서 수신/응답 처리 ... */

/* 정리 */
zlink_gateway_destroy(&client);
zlink_discovery_destroy(&client_discovery);
zlink_gateway_destroy(&server);
zlink_discovery_destroy(&server_discovery);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

### Recv 모드

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* === Server (recv 모드) === */
void *server_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(server_discovery, "tcp://127.0.0.1:5551");

void *server = zlink_gateway_new(ctx, "echo-service");
zlink_set_routing_id(server, "echo-server-1", 13);
zlink_gateway_attach_discovery(server, server_discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* === Client (recv 모드) === */
void *client_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(client_discovery, "tcp://127.0.0.1:5551");

void *client = zlink_gateway_new(ctx, "echo-service");
zlink_set_routing_id(client, "client-1", 8);
zlink_gateway_attach_discovery(client, client_discovery);

/* gateway monitor로 route readiness 대기 */

/* 요청 전송 */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_gateway_send(client, &part, 1, 0);

/* 응답 수신 */
zlink_routing_id_t source_rid;
zlink_msg_t *reply_parts = NULL;
size_t reply_count = 0;
zlink_gateway_recv(client, &source_rid, &reply_parts, &reply_count, 0);

/* 정리 */
zlink_gateway_destroy(&client);
zlink_discovery_destroy(&client_discovery);
zlink_gateway_destroy(&server);
zlink_discovery_destroy(&server_discovery);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 10. API 요약

| 함수 | 설명 |
|------|------|
| `zlink_gateway_new(ctx, service_name)` | recv 모드 Gateway 생성 |
| `zlink_set_routing_id(gateway, data, size)` | 첫 bind/connect 전 routing id 설정 |
| `zlink_recv_handler(gateway, fn, userdata)` | 수신 callback attach |
| `zlink_gateway_recv(gateway, ...)` | recv 모드에서 메시지 수신 |
| `zlink_gateway_attach_discovery(gateway, discovery)` | Discovery 연결 |
| `zlink_gateway_bind(gateway, endpoint)` | 수신 endpoint bind (서버 역할) |
| `zlink_gateway_send(gateway, ...)` | 멀티파트 메시지 전송 (LB 적용) |
| `zlink_gateway_send_rid(gateway, ...)` | 특정 피어로 전송 |
| `zlink_gateway_set_lb_strategy(gateway, strategy)` | LB 전략 설정 |
| `zlink_set_option(gateway, option, val, len)` | 서비스 옵션 설정 |
| `zlink_set_routing_id(gateway, data, size)` | 라우팅 ID 설정 |
| `zlink_get_routing_id(gateway, &out)` | 라우팅 ID 조회 |
| `zlink_set_tls_client(gateway, ...)` | TLS 클라이언트 설정 |
| `zlink_set_tls_server(gateway, ...)` | TLS 서버 설정 |
| `zlink_get_option(gateway, ...)` | bind된 endpoint 조회 |
| `zlink_monitor_snapshot(monitor, ...)` | readiness 및 queue depth 조회 |
| `zlink_gateway_update_peer_weight(...)` | 피어 가중치 갱신 |
| `zlink_registry_gateway_peers_query(...)` | gateway-peer 상태 조회 |
| `zlink_gateway_destroy(&gateway)` | 종료 |

---
[← Discovery](07-1-discovery.ko.md) | [SPOT →](07-3-spot.ko.md)
