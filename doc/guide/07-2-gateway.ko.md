# Gateway 서비스 (위치투명 요청/응답)

## 1. 개요

Gateway는 Discovery 기반으로 서비스 Receiver에 위치투명하게 메시지를 전송하고, Receiver로부터 응답을 수신하는 클라이언트 컴포넌트이다. 로드밸런싱과 자동 연결/해제를 처리한다.

> **명칭에 대하여**: Gateway는 특정 서비스에 대한 접근점(entry point)이자 클라이언트 사이드 로드밸런서다. API Gateway(Kong, AWS API Gateway 등)처럼 인증·rate limiting·프로토콜 변환을 포함하는 개념이 아니라, 서비스 접근 + 로드밸런싱에 집중하는 경량 게이트웨이를 의미한다.

**Gateway는 thread-safe하다.** 일반 zlink 소켓(PAIR, DEALER, ROUTER 등)은 단일 스레드에서만 사용해야 하지만, Gateway는 내부 mutex 보호를 통해 여러 스레드에서 안전하게 동시 사용할 수 있다.

## 2. Receiver 설정

```c
void *receiver = zlink_receiver_new(ctx, "payment-receiver-1");

/* 비즈니스 소켓 bind */
zlink_receiver_bind(receiver, "tcp://*:5555");

/* Registry 연결 */
zlink_receiver_connect_registry(receiver, "tcp://registry1:5551");

/* 서비스 등록 (advertise_endpoint 자동 감지) */
zlink_receiver_register(receiver, "payment-service", NULL, 1);

/* 등록 결과 확인 */
int status;
char resolved[256], error_msg[256];
zlink_receiver_register_result(receiver, "payment-service",
    &status, resolved, error_msg);
if (status != 0) {
    fprintf(stderr, "등록 실패: %s\n", error_msg);
    return -1;
}

/* 비즈니스 메시지 처리 */
void *router = zlink_receiver_router(receiver);
/* router에서 [routing_id][msgId][payload...] 수신/응답 */

zlink_receiver_destroy(&receiver);
```

### Endpoint 설정

| bind_endpoint | advertise_endpoint | 결과 |
|---------------|-------------------|------|
| `tcp://*:5555` | `NULL` | 로컬 IP 자동 감지 |
| `tcp://*:5555` | `tcp://payment-server:5555` | DNS 이름으로 광고 |

> NAT/컨테이너 환경에서는 advertise_endpoint를 명시적으로 설정해야 한다.

## 3. Gateway 설정

```c
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_subscribe(discovery, "payment-service");

void *gateway = zlink_gateway_new(ctx, discovery, "gateway-1");

/* 로드밸런싱 설정 */
zlink_gateway_set_lb_strategy(gateway, "payment-service",
    ZLINK_GATEWAY_LB_ROUND_ROBIN);
```

## 4. 메시지 전송

### 4.1 기본 전송

```c
/* Gateway에서 Receiver로 요청 전송 */
zlink_msg_t req;
zlink_msg_init_data(&req, data, size, NULL, NULL);
zlink_gateway_send(gateway, "payment-service", &req, 1, 0);
```

### 4.2 응답 수신 (Gateway)

```c
/* Gateway에서 Receiver 응답 수신 */
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char service_name[256];
int rc = zlink_gateway_recv(gateway, &parts, &part_count, 0, service_name);
if (rc != -1) {
    /* parts[0..part_count-1] 처리 */
    zlink_msgv_close(parts, part_count);
}
```

### 4.3 Receiver 측 수신/응답

```c
/* Receiver의 ROUTER 소켓에서 수신 및 응답 */
void *router = zlink_receiver_router(receiver);
/* [routing_id][msgId][payload...] 수신 후 응답 처리 */
```

## 5. 로드밸런싱

| 전략 | 상수 | 설명 |
|------|------|------|
| Round Robin | `ZLINK_GATEWAY_LB_ROUND_ROBIN` | 순차 선택 (기본) |
| Weighted | `ZLINK_GATEWAY_LB_WEIGHTED` | 가중치 기반 (weight 높을수록 선택 확률 높음) |

### 가중치 갱신

```c
zlink_receiver_update_weight(receiver, "payment-service", 5);
```

## 6. Thread-Safety

### 일반 소켓 vs Gateway

| | 일반 소켓 (PAIR, DEALER, ROUTER 등) | Gateway |
|---|---|---|
| **스레드 안전성** | 단일 스레드에서만 사용 | **Thread-safe** — 여러 스레드에서 동시 사용 |
| **외부 동기화** | 멀티스레드 시 애플리케이션이 직접 동기화 필요 | 불필요 — 내부 mutex 보호 |
| **백그라운드 작업** | 없음 | Discovery 갱신을 백그라운드 워커가 처리 |

### Thread-safe API

Gateway의 모든 공개 API는 내부 mutex로 보호된다. 여러 스레드에서 동시에 호출해도 안전하다.

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_recv()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_gateway_setsockopt()`
- `zlink_gateway_set_tls_client()`
- `zlink_gateway_connection_count()`

### 멀티스레드 사용 예제

```c
/* Gateway는 thread-safe하므로 여러 스레드에서 공유 가능 */
void *gateway = zlink_gateway_new(ctx, discovery, "gw-1");

/* 워커 스레드 함수 */
void *send_worker(void *arg) {
    void *gw = arg;
    zlink_msg_t req;
    zlink_msg_init_data(&req, "request", 7, NULL, NULL);
    /* 여러 스레드에서 동시에 send 호출 — 안전 */
    zlink_gateway_send(gw, "my-service", &req, 1, 0);
    return NULL;
}

/* 여러 스레드에서 동시 전송 */
for (int i = 0; i < 4; i++)
    zlink_threadstart(&send_worker, gateway);
```

### 장점

**1. Send 전용 설계로 낮은 경합**

Gateway의 send/recv는 내부 mutex로 보호되어 thread-safe하며, lock 오버헤드를 최소화하도록 설계되어 있다.

**2. 애플리케이션 아키텍처 단순화**

일반 소켓은 단일 스레드 소유 원칙을 지켜야 하므로, 멀티스레드 환경에서 별도의 메시지 큐나 프록시 패턴이 필요하다. Gateway는 이런 추가 구성 없이 여러 스레드가 직접 send를 호출할 수 있다.

```
일반 소켓 (멀티스레드):
  Thread A ──┐
  Thread B ──┼── inproc 큐 ── 전용 I/O 스레드 ── ROUTER 소켓
  Thread C ──┘

Gateway (멀티스레드):
  Thread A ──┐
  Thread B ──┼── Gateway (내부 mutex 보호) ── send ──→ Receiver
  Thread C ──┘
```

**3. Discovery 갱신이 send를 블록하지 않음**

서비스 풀 갱신(Receiver 추가/제거, 연결/재연결)은 전용 백그라운드 워커 스레드가 처리한다. send 호출 중에 Discovery 이벤트가 도착해도 사용자 API가 블록되지 않는다.

**4. 동시 전송과 가중치 갱신이 안전**

여러 스레드가 동시에 메시지를 전송하면서, 동시에 Receiver가 `zlink_receiver_update_weight()`로 가중치를 갱신해도 데이터 경합 없이 안전하게 처리된다.

> 참고: `core/tests/discovery/test_gateway.cpp` — `test_gateway_concurrent_send_and_updates()`: 다중 스레드 동시 전송 + 가중치 갱신 검증

## 7. 자동 연결/해제

Gateway는 Discovery 이벤트를 받아 자동으로 Receiver를 연결/해제한다.

- `RECEIVER_ADDED`: 신규 Receiver에 ROUTER connect
- `RECEIVER_REMOVED`: 제거된 Receiver disconnect

## 8. End-to-End 예제

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_set_endpoints(registry, "tcp://*:5550", "tcp://*:5551");
zlink_registry_start(registry);

/* === Receiver === */
void *receiver = zlink_receiver_new(ctx, "echo-receiver-1");
zlink_receiver_bind(receiver, "tcp://*:5555");
zlink_receiver_connect_registry(receiver, "tcp://127.0.0.1:5551");
zlink_receiver_register(receiver, "echo-service", NULL, 1);

/* === Client === */
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5550");
zlink_discovery_subscribe(discovery, "echo-service");

void *gateway = zlink_gateway_new(ctx, discovery, "client-1");

/* 서비스 가용 대기 */
while (!zlink_discovery_service_available(discovery, "echo-service"))
    sleep(1);

/* 요청/응답 */
zlink_msg_t req;
zlink_msg_init_data(&req, "hello", 5, NULL, NULL);
zlink_gateway_send(gateway, "echo-service", &req, 1, 0);

/* ... Receiver에서 수신/응답 처리 ... */

/* 정리 */
zlink_gateway_destroy(&gateway);
zlink_discovery_destroy(&discovery);
zlink_receiver_destroy(&receiver);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 9. API 요약

### Gateway API
| 함수 | 설명 |
|------|------|
| `zlink_gateway_new(ctx, discovery, routing_id)` | Gateway 생성 |
| `zlink_gateway_send(...)` | 메시지 전송 (LB 적용) |
| `zlink_gateway_recv(...)` | 메시지 수신 (Receiver 응답) |
| `zlink_gateway_send_rid(...)` | 특정 Receiver로 전송 |
| `zlink_gateway_set_lb_strategy(...)` | LB 전략 설정 |
| `zlink_gateway_setsockopt(...)` | 소켓 옵션 설정 |
| `zlink_gateway_set_tls_client(...)` | TLS 클라이언트 설정 |
| `zlink_gateway_router(...)` | ROUTER 소켓 획득 |
| `zlink_gateway_connection_count(...)` | 연결 Receiver 수 |
| `zlink_gateway_destroy(...)` | 종료 |

### Receiver API
| 함수 | 설명 |
|------|------|
| `zlink_receiver_new(ctx, routing_id)` | Receiver 생성 |
| `zlink_receiver_bind(...)` | ROUTER bind |
| `zlink_receiver_connect_registry(...)` | Registry 연결 |
| `zlink_receiver_register(...)` | 서비스 등록 |
| `zlink_receiver_register_result(...)` | 등록 결과 확인 |
| `zlink_receiver_update_weight(...)` | 가중치 갱신 |
| `zlink_receiver_unregister(...)` | 서비스 해제 |
| `zlink_receiver_set_tls_server(...)` | TLS 서버 설정 |
| `zlink_receiver_setsockopt(...)` | 소켓 옵션 설정 |
| `zlink_receiver_router(...)` | ROUTER 소켓 획득 |
| `zlink_receiver_destroy(...)` | 종료 |
