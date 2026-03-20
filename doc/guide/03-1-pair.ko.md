[English](03-1-pair.md) | [한국어](03-1-pair.ko.md)

# PAIR 소켓

## 1. 개요

PAIR 소켓은 정확히 하나의 피어와 1:1 양방향 독점 연결을 형성한다. 두 번째 피어가 연결하면 첫 번째 연결은 끊어진다.

**핵심 특성:**
- 단일 파이프만 허용 (1:1 독점)
- 양방향 자유 메시징 (send/recv 순서 무관)
- 가장 단순한 소켓 타입

**유효한 소켓 조합:** PAIR ↔ PAIR

```
┌────────┐              ┌────────┐
│ PAIR A │◄────────────►│ PAIR B │
└────────┘   양방향     └────────┘
```

## 2. 기본 사용법

### 생성 및 연결

```c
void *ctx = zlink_ctx_new();

/* 서버 측 */
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "tcp://*:5555");

/* 클라이언트 측 */
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "tcp://127.0.0.1:5555");
```

### 메시지 교환

```c
/* 수신 핸들러 정의 */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

/* 서버는 recv 모드를 유지 */
void *server = zlink_socket(ctx, ZLINK_PAIR);

/* 클라이언트 (송신 전용) */
void *client = zlink_socket(ctx, ZLINK_PAIR);

/* ... bind/connect ... */

/* 클라이언트 → 서버 */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "Hello", 5);
zlink_send(client, &msg, 1, 0);
/* 서버는 zlink_recv() 또는 poller + zlink_recv()로 수신 */

/* 서버 → 클라이언트 (양방향이지만 클라이언트도 수신하려면 핸들러 필요) */
zlink_msg_t reply;
zlink_msg_init_size(&reply, 5);
memcpy(zlink_msg_data(&reply), "World", 5);
zlink_send(server, &reply, 1, 0);
```

### 멀티파트 데이터 전송

멀티파트 데이터는 단일 `zlink_send` 호출로 parts 배열을 전송한다.

```c
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 3);
memcpy(zlink_msg_data(&parts[0]), "foo", 3);
zlink_msg_init_size(&parts[1], 6);
memcpy(zlink_msg_data(&parts[1]), "foobar", 6);
zlink_send(server, parts, 2, 0);

/* 수신 측은 한 번의 zlink_recv() 호출로 두 프레임을 수신:
   parts[0] = "foo", parts[1] = "foobar", part_count = 2 */
```

> 참고: `core/tests/test_pair_inproc.cpp` — `test_zlink_send_multipart()` 테스트

### 수신 모드

PAIR의 public API는 recv/poller-only다. 완전한 멀티파트 메시지는
`zlink_recv()`로 동기 수신한다. `source_rid`는 단일 피어이므로 항상 비어 있다.

```c
void *pair = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(pair, "tcp://*:5556");

zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(pair, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    /* parts[0..part_count-1] 처리 */
    zlink_multipart_close(parts, part_count);
    free(parts);
}
```

> HWM 도달 시 `zlink_send()`는 블록(기본) 또는 `ZLINK_DONTWAIT`로
> `EAGAIN`을 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

## 3. 메시지 형식

PAIR 소켓은 routing_id 프레임이나 envelope 없이 **애플리케이션 데이터만** 교환한다.

```
단일 프레임:     [데이터]
멀티파트 프레임:  [프레임1][프레임2]...[프레임N]
```

멀티파트 전송:

```c
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(server, parts, 2, 0);
```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 큐 최대 메시지 수 |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 큐 최대 메시지 수 |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 미전송 메시지 대기 시간 (ms), -1=무한 |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms), -1=무한 |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms), -1=무한 |

```c
int hwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

int linger = 0;  /* close 즉시 반환 */
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

## 5. 사용 패턴

### 패턴 1: 스레드 간 시그널링 (inproc)

가장 일반적인 PAIR 사용 사례. inproc transport로 스레드 간 zero-copy 통신.

```c
/* 메인 스레드 */
void *signal = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(signal, "inproc://signal");

/* 워커 스레드 */
void *worker_signal = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(worker_signal, "inproc://signal");

/* 워커 → 메인: 작업 완료 시그널 */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "DONE", 4);
zlink_send(worker_signal, &msg, 1, 0);

/* 메인: on_signal 콜백이 "DONE"을 비동기로 수신 */
```

> 참고: `core/tests/test_pair_inproc.cpp` — bind → connect → bounce 패턴

### 패턴 2: TCP 통신

네트워크를 통한 1:1 통신. 와일드카드 바인드로 포트 자동 할당 가능.

```c
/* 서버: 와일드카드 포트 */
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "tcp://127.0.0.1:*");

/* 할당된 엔드포인트 조회 */
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(server, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

/* 클라이언트: 조회된 엔드포인트로 연결 */
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, endpoint);
```

> 참고: `core/tests/test_pair_tcp.cpp` — `bind_loopback_ipv4()` + 와일드카드 바인드

### 패턴 3: DNS 이름 연결

호스트명으로도 연결 가능하다.

```c
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "tcp://localhost:5555");
```

> 참고: `core/tests/test_pair_tcp.cpp` — `test_pair_tcp_connect_by_name()`

### 패턴 4: IPC 통신

같은 머신의 프로세스 간 통신 (Linux/macOS).

```c
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "ipc:///tmp/myapp.ipc");

void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "ipc:///tmp/myapp.ipc");
```

> 참고: `core/tests/test_pair_ipc.cpp` — IPC 경로 길이 검증 포함

## 6. 주의사항

### 단일 피어만 허용

PAIR 소켓은 하나의 연결만 유지한다. 두 번째 피어가 connect하면 첫 번째 연결이 끊어진다.

```
 허용:  PAIR A ↔ PAIR B      (1:1)
 불가:  PAIR A ← PAIR B      (N:1 시도 시 기존 연결 끊김)
               ← PAIR C
```

N:1 통신이 필요하면 DEALER/ROUTER를 사용한다.

### inproc bind 순서

inproc transport는 **반드시 bind가 connect보다 먼저** 호출되어야 한다.

```c
/* 올바른 순서 */
zlink_bind(a, "inproc://signal");     /* 1. bind 먼저 */
zlink_connect(b, "inproc://signal");  /* 2. connect */

/* 잘못된 순서 — 실패 */
zlink_connect(b, "inproc://signal");  /* bind가 아직 없으므로 실패 */
zlink_bind(a, "inproc://signal");
```

### IPC 경로 길이

IPC 엔드포인트의 파일 경로는 시스템 제한(보통 108자)을 초과할 수 없다.

```c
/* 너무 긴 경로 → ENAMETOOLONG 에러 */
zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
```

> 참고: `core/tests/test_pair_ipc.cpp` — `test_endpoint_too_long()`

### HWM 동작

피어가 없거나 느릴 때, 송신 메시지는 HWM까지 큐잉된다. HWM 초과 시 `zlink_send()`가 블록(기본) 또는 `EAGAIN` 반환(`ZLINK_DONTWAIT`).

### LINGER 설정

`zlink_close()` 호출 시 미전송 메시지가 남아 있으면 LINGER 시간만큼 대기한다. 테스트나 빠른 종료가 필요한 경우:

```c
int linger = 0;
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

---
[← 소켓 패턴](03-0-socket-patterns.ko.md) | [PUB/SUB →](03-2-pubsub.ko.md)
