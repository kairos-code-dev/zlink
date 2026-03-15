[English](07-2-gateway.md) | [한국어](07-2-gateway.ko.md)

# Gateway 서비스 (위치투명 요청/응답)

## 1. 개요

Gateway는 Discovery 기반으로 서비스를 자동 발견하고, 로드밸런싱된 메시지
전송과 직접 콜백 수신을 지원하는 통합 서비스 핸들이다. 하나의 Gateway 핸들이
클라이언트(송신)와 서버(수신) 역할을 모두 수행할 수 있다.

> **명칭에 대하여**: Gateway는 특정 서비스에 대한 접근점(entry point)이자
> 클라이언트 사이드 로드밸런서다. API Gateway(Kong, AWS API Gateway 등)처럼
> 인증·rate limiting·프로토콜 변환을 포함하는 개념이 아니라, 서비스 접근 +
> 로드밸런싱에 집중하는 경량 게이트웨이를 의미한다.

**Gateway는 thread-safe하다.** 공개 Gateway handle API는 기본적으로
same-handle operational use 기준 thread-safe다. `send` / `send_rid`는
동시 호출을 허용하는 hot path이고, attach/option/monitor/query 계열은
runtime에 호출 가능한 control path이며, `destroy`는 fail-fast lifecycle gate를
가진다.

## 2. Gateway 생성

Gateway는 생성 시점에 서비스 이름, 라우팅 ID, 수신 핸들러를 고정한다.

```c
/* 수신 핸들러 정의 */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count)
{
    /* 수신 메시지 처리 */
    printf("수신: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    /* parts는 핸들러 반환 후 자동 정리됨 */
}

void *gateway = zlink_gateway_new(ctx, "payment-service",
                                   "gateway-1", on_message);
```

## 3. 서버 (수신) 측 설정

서버 역할을 하려면 Gateway에 endpoint를 bind하고, Discovery를 통해
Registry에 등록한다.

```c
void *ctx = zlink_ctx_new();

/* Discovery 설정 */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* Gateway 생성 (수신 핸들러 등록) */
void *server = zlink_gateway_new(ctx, "payment-service",
                                  "payment-server-1", on_request);

/* Discovery 연결 */
zlink_gateway_attach_discovery(server, discovery);

/* 비즈니스 소켓 bind */
zlink_gateway_bind(server, "tcp://*:5555");
```

## 4. 클라이언트 (송신) 측 설정

```c
/* Discovery 설정 */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* Gateway 생성 */
void *client = zlink_gateway_new(ctx, "payment-service",
                                  "client-1", on_reply);

/* Discovery 연결 */
zlink_gateway_attach_discovery(client, discovery);

/* 로드밸런싱 설정 */
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

수신은 생성 시 등록한 핸들러 콜백으로 자동 dispatch된다. 별도의 `recv()` 호출은 없다.

```c
void on_reply(const zlink_routing_id_t *source_rid,
              zlink_msg_t *parts, size_t part_count)
{
    /* 응답 처리 */
}
```

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

| | 일반 공개 socket handle | Gateway |
|---|---|---|
| **스레드 안전성** | 기본적으로 thread-safe | **Thread-safe** — same-handle operational use 허용 |
| **고빈도 경로** | `send`가 hot path | `send` / `send_rid`가 hot path |
| **저빈도 경로** | bind/connect/monitor/query는 correctness 우선 직렬화 | attach/option/monitor/query는 correctness 우선 직렬화 |
| **종료** | `close`는 fail-fast lifecycle gate | `destroy`는 fail-fast lifecycle gate |

### Thread-safe API

Gateway는 "모든 API가 같은 비용 모델"인 것은 아니지만, 공개 handle API는
기본적으로 thread-safe다.

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_gateway_set_option()`
- `zlink_gateway_attach_discovery()`
- `zlink_gateway_bind()`
- `zlink_gateway_connect()` / `zlink_gateway_disconnect()`
- `zlink_gateway_set_tls_client()`
- `zlink_gateway_last_endpoint()`
- open한 gateway monitor에 대한 `zlink_monitor_snapshot()`
- `zlink_gateway_destroy()`

사용자가 외우면 되는 규칙은 네 가지다.

1. Gateway handle은 여러 스레드에서 공유해도 된다.
2. `send` / `send_rid`는 same-handle concurrent 호출이 가능하다.
3. control-path API도 runtime에 호출할 수 있다.
4. `destroy`는 fail-fast이며 admitted API가 있으면 `EBUSY`, accepted 이후 새
   진입은 `ESHUTDOWN`이다.

### 멀티스레드 사용 예제

```c
/* Gateway는 thread-safe하므로 여러 스레드에서 공유 가능 */
void *gateway = zlink_gateway_new(ctx, "my-service", "gw-1", on_reply);
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

## 8. 자동 연결/해제

Gateway는 Discovery 이벤트를 받아 자동으로 피어를 연결/해제한다.

- 서버 추가: 신규 서버에 자동 connect
- 서버 제거: 제거된 서버 disconnect

## 9. End-to-End 예제

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* === Server === */
void *server_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(server_discovery, "tcp://127.0.0.1:5551");

void *server = zlink_gateway_new(ctx, "echo-service",
                                  "echo-server-1", on_request);
zlink_gateway_attach_discovery(server, server_discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* === Client === */
void *client_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(client_discovery, "tcp://127.0.0.1:5551");

void *client = zlink_gateway_new(ctx, "echo-service",
                                  "client-1", on_reply);
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

## 10. API 요약

| 함수 | 설명 |
|------|------|
| `zlink_gateway_new(ctx, service_name, routing_id, handler)` | Gateway 생성 |
| `zlink_gateway_attach_discovery(gateway, discovery)` | Discovery 연결 |
| `zlink_gateway_bind(gateway, endpoint)` | 수신 endpoint bind (서버 역할) |
| `zlink_gateway_send(gateway, parts, count, flags)` | 멀티파트 메시지 전송 (LB 적용) |
| `zlink_gateway_send_rid(gateway, rid, parts, count, flags)` | 특정 피어로 전송 |
| `zlink_gateway_set_lb_strategy(gateway, strategy)` | LB 전략 설정 |
| `zlink_gateway_set_option(gateway, option, val, len)` | 서비스 옵션 설정 |
| `zlink_gateway_set_routing_id(gateway, data, size)` | 라우팅 ID 설정 |
| `zlink_gateway_routing_id(gateway, out)` | 라우팅 ID 조회 |
| `zlink_gateway_set_tls_client(gateway, ca, host, trust)` | TLS 클라이언트 설정 |
| `zlink_gateway_set_tls_server(gateway, cert, key)` | TLS 서버 설정 |
| `zlink_gateway_last_endpoint(gateway, buf, size)` | bind된 endpoint 조회 |
| `zlink_monitor_snapshot(monitor, &snapshot)` | 로컬 bind/send readiness 및 queue depth 조회 |
| `zlink_gateway_update_peer_weight(gateway, rid, weight)` | 피어 가중치 갱신 |
| `zlink_registry_gateway_peers_query(registry, &filter, entries, &count)` | 운영용 gateway-peer 상태 조회 |
| `zlink_gateway_destroy(&gateway)` | 종료 |

---
[← Discovery](07-1-discovery.ko.md) | [SPOT →](07-3-spot.ko.md)
