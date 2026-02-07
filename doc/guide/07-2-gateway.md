# Gateway 서비스 (위치투명 요청/응답)

## 1. 개요

Gateway는 Discovery 기반으로 서비스 Receiver에 위치투명하게 요청을 전송하고 응답을 수신하는 클라이언트 컴포넌트이다. 로드밸런싱, 자동 연결/해제, 요청-응답 매핑을 처리한다.

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
void *discovery = zlink_discovery_new(ctx);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_subscribe(discovery, "payment-service");

void *gateway = zlink_gateway_new(ctx, discovery, "gateway-1");

/* 로드밸런싱 설정 */
zlink_gateway_set_lb_strategy(gateway, "payment-service",
    ZLINK_GATEWAY_LB_ROUND_ROBIN);
```

## 4. 메시지 송수신

### 4.1 기본 송수신

```c
/* 요청 */
zlink_msg_t req;
zlink_msg_init_data(&req, data, size, NULL, NULL);
zlink_gateway_send(gateway, "payment-service", &req, 1, 0);

/* 응답 */
zlink_msg_t *reply_parts = NULL;
size_t reply_count = 0;
char svc_name[256];
zlink_gateway_recv(gateway, &reply_parts, &reply_count, 0, svc_name);
/* 사용 후 정리 */
zlink_msgv_close(reply_parts, reply_count);
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

## 6. 자동 연결/해제

Gateway는 Discovery 이벤트를 받아 자동으로 Receiver를 연결/해제한다.

- `RECEIVER_ADDED`: 신규 Receiver에 ROUTER connect
- `RECEIVER_REMOVED`: 제거된 Receiver disconnect

## 7. End-to-End 예제

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
void *discovery = zlink_discovery_new(ctx);
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

## 8. API 요약

### Gateway API
| 함수 | 설명 |
|------|------|
| `zlink_gateway_new(ctx, discovery, routing_id)` | Gateway 생성 |
| `zlink_gateway_send(...)` | 메시지 전송 (LB 적용) |
| `zlink_gateway_recv(...)` | 응답 수신 |
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
