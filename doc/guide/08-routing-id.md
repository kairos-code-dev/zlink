# Routing ID 개념 및 사용법

## 1. 개요

Routing ID는 zlink에서 소켓과 연결을 식별하는 바이너리 데이터이다. ROUTER 소켓에서 메시지 라우팅에 사용되며, STREAM 소켓에서 외부 클라이언트 식별에, 모니터링에서 피어 식별에 활용된다.

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
| 소켓 own routing_id | UUID (binary) | 16B | 모든 소켓에서 자동 생성 |
| STREAM peer routing_id | uint32 | 4B | 연결별 자동 할당 |

- 사용자가 `ZLINK_ROUTING_ID`를 설정하지 않으면 자동 생성
- 프로세스 내 전역 카운터 기반으로 유일성 보장

### own vs peer — 사용자가 알아야 할 차이

| | own routing_id | peer routing_id |
|---|---|---|
| **생성 시점** | 소켓 생성 시 | 피어 연결 시 |
| **크기** | 16B (UUID) | 가변 (ROUTER), 4B (STREAM) |
| **사용** | 핸드셰이크에서 전송 | 수신 메시지에 자동 추가 |
| **설정** | `ZLINK_ROUTING_ID` | 피어가 설정한 값 사용 |

own routing_id는 소켓이 생성될 때 자동으로 UUID가 할당되며, 피어에게 핸드셰이크 시 전송된다. peer routing_id는 피어가 보낸 own routing_id이며, ROUTER/STREAM 소켓에서 수신 메시지의 첫 프레임에 자동으로 추가된다.

## 4. 사용자 지정 routing_id

### 소켓 Identity 설정

```c
/* bind/connect 전에 설정 */
const char *id = "router-A";
zlink_setsockopt(socket, ZLINK_ROUTING_ID, id, strlen(id));
```

주의사항:
- 반드시 `zlink_bind()` 또는 `zlink_connect()` **이전에** 설정
- 연결 후 변경 불가
- 빈 문자열("")은 허용되지 않음
- 같은 ROUTER에 동일 routing_id를 가진 두 피어가 연결되면 충돌 발생

### 사용자 지정 시 고려사항

```c
/* 좋은 예: 의미 있는 식별자 */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "worker-01", 9);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);

/* 주의: 자동 생성 routing_id와 충돌 가능성 */
/* UUID 형식(16B 바이너리)은 피해야 함 */
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2)`

### 조회

```c
uint8_t buf[255];
size_t size = sizeof(buf);
zlink_getsockopt(socket, ZLINK_ROUTING_ID, buf, &size);

printf("routing_id (%zu bytes): ", size);
for (size_t i = 0; i < size; ++i)
    printf("%02x", buf[i]);
printf("\n");
```

## 5. Connection Alias 설정

`ZLINK_CONNECT_ROUTING_ID`는 다음 `zlink_connect()` 호출에 적용되는 연결별 별칭이다. ROUTER에서 특정 연결을 의미 있는 이름으로 참조할 때 사용한다.

```c
/* 다음 connect에 alias 적용 */
const char *alias = "edge-1";
zlink_setsockopt(socket, ZLINK_CONNECT_ROUTING_ID, alias, strlen(alias));
zlink_connect(socket, "tcp://server:5555");

/* 다른 연결에 다른 alias */
const char *alias2 = "edge-2";
zlink_setsockopt(socket, ZLINK_CONNECT_ROUTING_ID, alias2, strlen(alias2));
zlink_connect(socket, "tcp://server2:5556");
```

- `ZLINK_ROUTING_ID`는 소켓 전체에 적용
- `ZLINK_CONNECT_ROUTING_ID`는 개별 연결에 적용
- 하나의 소켓에서 여러 연결에 각각 다른 alias 가능

## 6. ROUTER 소켓에서 routing_id 사용법

ROUTER 소켓은 수신 메시지에 routing_id 프레임을 자동으로 앞에 추가한다. 응답 시 동일 routing_id를 사용하여 올바른 피어에게 전송한다.

### 기본 요청-응답

```c
/* ROUTER 서버 */
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(router, ZLINK_LAST_ENDPOINT, endpoint, &len);

/* DEALER 클라이언트 (명시적 routing_id) */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, endpoint);

/* DEALER 전송 */
zlink_send(dealer, "Hello", 5, 0);

/* ROUTER 수신: [routing_id="D1"] + [data="Hello"] */
char identity[32], data[256];
int id_size = zlink_recv(router, identity, sizeof(identity), 0);
int data_size = zlink_recv(router, data, sizeof(data), 0);
/* identity = "D1" (2 bytes), data = "Hello" (5 bytes) */

/* ROUTER 응답: routing_id를 첫 프레임으로 전송 */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);
zlink_send(router, "World", 5, 0);

/* DEALER 수신: "World" (routing_id 프레임은 자동 제거) */
zlink_recv(dealer, data, sizeof(data), 0);
```

### 다중 클라이언트 구분

```c
/* DEALER 1: routing_id = "D1" */
zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer1, endpoint);

/* DEALER 2: routing_id = "D2" */
zlink_setsockopt(dealer2, ZLINK_ROUTING_ID, "D2", 2);
zlink_connect(dealer2, endpoint);

/* ROUTER에서 특정 클라이언트에게만 응답 */
zlink_send(router, "D1", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d1", 11, 0);

zlink_send(router, "D2", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d2", 11, 0);
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — 다중 DEALER 예제

### zlink_msg_t를 사용한 routing_id 처리

```c
/* 수신 */
zlink_msg_t rid, data;
zlink_msg_init(&rid);
zlink_msg_init(&data);
zlink_msg_recv(&rid, router, 0);   /* routing_id 프레임 */
zlink_msg_recv(&data, router, 0);  /* 데이터 프레임 */

/* routing_id 크기 및 내용 확인 */
printf("routing_id: %zu bytes\n", zlink_msg_size(&rid));

/* 응답: 수신한 routing_id 재사용 */
zlink_msg_send(&rid, router, ZLINK_SNDMORE);
zlink_send(router, "reply", 5, 0);

zlink_msg_close(&rid);
zlink_msg_close(&data);
```

## 7. STREAM 소켓에서 routing_id 사용법

STREAM 소켓은 4B uint32 peer routing_id로 외부 클라이언트를 식별한다.

### 기본 사용

```c
/* 수신: [routing_id (4B)] + [payload] */
unsigned char rid[4];
char payload[4096];

zlink_recv(stream, rid, 4, 0);         /* 항상 4바이트 */
int size = zlink_recv(stream, payload, sizeof(payload), 0);

/* 응답: 동일 routing_id 사용 */
zlink_send(stream, rid, 4, ZLINK_SNDMORE);
zlink_send(stream, response, resp_len, 0);
```

### 연결/해제 이벤트의 routing_id

```c
unsigned char rid[4];
unsigned char code;

zlink_recv(stream, rid, 4, 0);
zlink_recv(stream, &code, 1, 0);

if (code == 0x01) {
    /* 새 클라이언트 연결: rid를 저장하여 이후 통신에 사용 */
    printf("연결: %02x%02x%02x%02x\n", rid[0], rid[1], rid[2], rid[3]);
} else if (code == 0x00) {
    /* 클라이언트 해제: rid로 식별하여 정리 */
    printf("해제: %02x%02x%02x%02x\n", rid[0], rid[1], rid[2], rid[3]);
}
```

> 참고: `core/tests/test_stream_socket.cpp` — `recv_stream_event()`, `send_stream_msg()`

### ROUTER vs STREAM routing_id 비교

| | ROUTER | STREAM |
|---|---|---|
| **크기** | 가변 (사용자 설정 또는 16B UUID) | 고정 4B (uint32) |
| **생성** | 피어의 own routing_id | 서버가 자동 할당 |
| **설정 가능** | ZLINK_ROUTING_ID로 피어가 설정 | 자동 할당만 (설정 불가) |
| **프레임 위치** | 수신 시 자동 추가 | 수신 시 자동 추가 |

## 8. routing_id 디버깅 팁

### hex 출력

routing_id는 바이너리 데이터이므로 문자열로 출력하면 깨질 수 있다. hex 형식을 사용한다.

```c
void print_routing_id(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    printf("routing_id[%zu]: ", size);
    for (size_t i = 0; i < size; i++)
        printf("%02x", bytes[i]);
    printf("\n");
}

/* 사용 */
char rid[255];
int rid_size = zlink_recv(router, rid, sizeof(rid), 0);
print_routing_id(rid, rid_size);
```

### 문자열 routing_id

사용자가 설정한 routing_id가 ASCII 문자열이면 직접 출력 가능.

```c
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);

/* ROUTER에서 수신 시 */
char rid[32];
int rid_size = zlink_recv(router, rid, sizeof(rid), 0);
rid[rid_size] = '\0';
printf("routing_id: %s\n", rid);  /* "D1" */
```

### 자동 생성 routing_id 확인

```c
/* 소켓 생성 후 자동 할당된 routing_id 조회 */
uint8_t own_id[255];
size_t own_size = sizeof(own_id);
zlink_getsockopt(socket, ZLINK_ROUTING_ID, own_id, &own_size);
printf("자동 생성 routing_id: %zu bytes\n", own_size);  /* 16 bytes (UUID) */
```

## 9. 바이너리 처리 원칙

- routing_id는 **바이너리 데이터**로 취급
- 문자열 변환은 애플리케이션 책임
- 자동 생성 routing_id는 내부 포맷이며 숫자 변환 API 미제공
- 비교 시 `memcmp()` 사용 (문자열 비교 함수 사용 불가)
- 로그 출력 시 hex 포맷 권장

```c
/* routing_id 비교 */
if (rid_size == 2 && memcmp(rid, "D1", 2) == 0) {
    /* D1 클라이언트의 메시지 */
}
```
