[English](./08-routing-id.md) | [한국어](./08-routing-id.ko.md)

# Routing ID 개념 및 사용법

## 1. 개요

라우팅 ID(Routing ID)는 zlink에서 소켓과 연결을 식별하는 바이너리 데이터다.
ROUTER 소켓의 메시지 라우팅, STREAM 소켓의 외부 클라이언트 식별,
모니터링의 피어 식별에 활용된다.

## 2. zlink_routing_id_t

```c
typedef struct {
    uint8_t size;       /* 0~255 */
    uint8_t data[255];
} zlink_routing_id_t;
```

## 3. 자동 생성 규칙

| 종류 | 포맷 | 크기 | 설명 |
|------|------|------|------|
| 소켓 자체 라우팅 ID | UUID (binary) | 16B | 모든 소켓에서 자동 생성 |
| STREAM 피어 라우팅 ID | uint32 | 4B | 연결별 자동 할당 |

- 사용자가 `zlink_set_routing_id()`를 호출하지 않으면 자동 생성된다.
- 프로세스 내 전역 카운터 기반으로 유일성이 보장된다.

### own vs peer — 차이점

| 항목 | 자체 라우팅 ID (own) | 피어 라우팅 ID (peer) |
|------|---|---|
| **생성 시점** | 소켓 생성 시 | 피어 연결 시 |
| **크기** | 16B (UUID) | 가변 (ROUTER), 4B (STREAM) |
| **사용** | 핸드셰이크에서 전송 | 수신 메시지에 자동 첨부 |
| **설정** | `zlink_set_routing_id()` | 피어가 설정한 값 사용 |

자체 라우팅 ID는 소켓 생성 시 자동으로 UUID가 할당되며, 핸드셰이크 시 피어에게 전송된다.

피어 라우팅 ID는 피어가 보낸 자체 라우팅 ID이며, ROUTER/STREAM 소켓에서 수신 메시지에 자동으로 첨부된다.

## 4. 사용자 지정 routing_id

### 소켓 Identity 설정

```c
/* Set before bind/connect */
const char *id = "router-A";
zlink_set_routing_id(socket, id, strlen(id));
```

주의사항:
- 반드시 `zlink_bind()` 또는 `zlink_connect()` **이전에** 설정한다.
- 연결 후에는 변경할 수 없다.
- 빈 문자열("")은 허용되지 않는다.
- 같은 ROUTER에 동일 라우팅 ID를 가진 두 피어가 연결되면 충돌이 발생한다.

### 사용자 지정 시 고려사항

```c
/* Good example: meaningful identifiers */
zlink_set_routing_id(dealer, "worker-01", 9);
zlink_set_routing_id(dealer, "D1", 2);

/* Caution: potential collision with auto-generated routing_ids */
/* Avoid UUID format (16B binary) */
```

> 참고: `core/tests/integration/test_router_multiple_dealers.cpp` — `zlink_set_routing_id(dealer1, "D1", 2)`

### 조회

```c
zlink_routing_id_t rid;
zlink_get_routing_id(socket, &rid);

printf("routing_id (%u bytes): ", rid.size);
for (size_t i = 0; i < rid.size; ++i)
    printf("%02x", rid.data[i]);
printf("\n");
```

## 5. 연결 별칭 설정

`ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`는 다음 `zlink_connect()` 호출에
적용되는 연결별 별칭이다.
`zlink_set_router_option()`으로 설정하며,
ROUTER에서 특정 연결을 의미 있는 이름으로 참조할 때 사용한다.

```c
/* Apply alias to the next connect */
const char *alias = "edge-1";
zlink_set_router_option(socket, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, alias, strlen(alias));
zlink_connect(socket, "tcp://server:5555");

/* Different alias for another connection */
const char *alias2 = "edge-2";
zlink_set_router_option(socket, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, alias2, strlen(alias2));
zlink_connect(socket, "tcp://server2:5556");
```

- `zlink_set_routing_id()`는 소켓 전체에 적용된다.
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (`zlink_set_router_option()`으로 설정)는 개별 연결에 적용된다.
- 하나의 소켓에서 여러 연결에 각각 다른 별칭을 지정할 수 있다.
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`는 ROUTER 연결 경로 전용이다.
- `ZLINK_SOCKET_STREAM`에 설정하면 `EOPNOTSUPP`를 반환한다.

## 6. ROUTER 소켓에서 routing_id 사용법

ROUTER 소켓에서 `zlink_recv()`와 recv 콜백은 송신자의 라우팅 ID를
**별도 파라미터**(`source_rid`)로 반환한다. 메시지 프레임에는 데이터만 포함된다.
응답 시 `zlink_send_rid()`에 동일 라우팅 ID를 전달하여 올바른 피어에게 전송한다.

> **libzmq와의 차이:** libzmq ROUTER는 `zmq_recv()`의 첫 프레임으로
> 라우팅 ID를 반환했지만, zlink에서는 모든 소켓 타입에서 라우팅 ID가
> 별도 파라미터로 분리되어 있다.

### 기본 요청-응답

```c
/* ROUTER server (recv 루프 사용) */
void *router = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

/* DEALER client (explicit routing_id) */
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, endpoint);

/* DEALER send */
zlink_msg_t req;
zlink_msg_init_size(&req, 5);
memcpy(zlink_msg_data(&req), "Hello", 5);
zlink_send(dealer, &req, 1, 0);

/* ROUTER: poller 루프에서 router_recv 로 메시지를 꺼낸다.
   source_node_rid = "D1" (2 bytes), parts[0] = "Hello" (5 bytes).
   zlink_send_rid(router, source_node_rid, reply, 1, 0) 로 응답. */
```

### 다중 클라이언트 구분

```c
/* DEALER 1: routing_id = "D1" */
zlink_set_routing_id(dealer1, "D1", 2);
zlink_connect(dealer1, endpoint);

/* DEALER 2: routing_id = "D2" */
zlink_set_routing_id(dealer2, "D2", 2);
zlink_connect(dealer2, endpoint);

/* ROUTER handler distinguishes clients by source_rid */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* source_rid->data contains "D1" or "D2" */
    /* Reply to specific client */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

> 참고: `core/tests/integration/test_router_multiple_dealers.cpp` — 다중 DEALER 예제

### zlink_msg_t를 사용한 routing_id 처리

```c
/* Handler callback provides routing_id and data directly */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* Check routing_id size and content */
    printf("routing_id: %zu bytes\n", source_rid->size);

    /* Reply: use source_rid */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

## 7. STREAM 소켓에서 routing_id 사용법

STREAM 소켓은 4B uint32 피어 라우팅 ID로 외부 클라이언트를 식별한다.

### 기본 사용

```c
/* Callback dispatch */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; ++i) {
        void *data = zlink_msg_data(&parts[i]);
        size_t size = zlink_msg_size(&parts[i]);

        /* Reply: use the same routing_id */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, size);
        memcpy(zlink_msg_data(&reply), data, size);
        zlink_send_rid(stream, source_rid, &reply, 1, 0);
        zlink_msg_close(&parts[i]);
    }
}

zlink_recv_handler(stream, on_message, NULL);
```

### 연결/해제 이벤트의 routing_id

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; ++i) {
        uint8_t *data = (uint8_t *)zlink_msg_data(&parts[i]);
        size_t size = zlink_msg_size(&parts[i]);

        if (size == 1 && data[0] == 0x01) {
            /* New client connected: identify by source_rid */
            printf("Connected: ");
            for (size_t j = 0; j < source_rid->size; j++)
                printf("%02x", source_rid->data[j]);
            printf("\n");
        } else if (size == 1 && data[0] == 0x00) {
            /* Client disconnected: identify by source_rid and clean up */
            printf("Disconnected\n");
        }
        zlink_msg_close(&parts[i]);
    }
}
```

> 참고: `core/tests/integration/test_stream_socket.cpp` — `recv_stream_event()`, `send_stream_msg()`

### ROUTER vs STREAM routing_id 비교

| 항목 | ROUTER | STREAM |
|------|---|---|
| **크기** | 가변 (사용자 설정 또는 16B UUID) | 고정 4B (uint32) |
| **생성** | 피어의 own routing_id | 서버가 자동 할당 |
| **설정 가능** | `zlink_set_routing_id()`로 피어가 설정 | 자동 할당만 (설정 불가) |
| **프레임 위치** | 수신 시 자동 추가 | 수신 시 자동 추가 |

## 8. routing_id 디버깅 팁

### hex 출력

라우팅 ID는 바이너리 데이터이므로 문자열로 출력하면 깨질 수 있다. hex 형식을 사용한다.

```c
void print_routing_id(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    printf("routing_id[%zu]: ", size);
    for (size_t i = 0; i < size; i++)
        printf("%02x", bytes[i]);
    printf("\n");
}

/* In handler callback */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    print_routing_id(source_rid->data, source_rid->size);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

### 문자열 라우팅 ID

사용자가 설정한 라우팅 ID가 ASCII 문자열이면 직접 출력 가능하다.

```c
zlink_set_routing_id(dealer, "D1", 2);

/* In ROUTER handler callback */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    char rid[256];
    memcpy(rid, source_rid->data, source_rid->size);
    rid[source_rid->size] = '\0';
    printf("routing_id: %s\n", rid);  /* "D1" */
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

### 자동 생성 라우팅 ID 확인

```c
/* Query the auto-assigned routing_id after socket creation */
zlink_routing_id_t rid;
zlink_get_routing_id(socket, &rid);
printf("Auto-generated routing_id: %u bytes\n", rid.size);  /* 16 bytes (UUID) */
```

## 9. 바이너리 처리 원칙

- 라우팅 ID는 **바이너리 데이터**로 취급한다.
- 문자열 변환은 애플리케이션 책임이다.
- 자동 생성 라우팅 ID는 내부 포맷이며 숫자 변환 API를 제공하지 않는다.
- 비교 시 `memcmp()`를 사용한다 (문자열 비교 함수 사용 불가).
- 로그 출력 시 hex 포맷을 권장한다.

```c
/* routing_id comparison */
if (rid_size == 2 && memcmp(rid, "D1", 2) == 0) {
    /* Message from client D1 */
}
```

---
[← SPOT](./07-3-spot.ko.md) | [Message API →](./09-message-api.ko.md)
