# PUB/SUB/XPUB/XSUB 발행-구독

## 1. 개요

발행-구독(Publish-Subscribe) 패턴은 메시지를 토픽 기반으로 분배한다. zlink는 기본 PUB/SUB과 고급 XPUB/XSUB 두 가지 레벨을 제공한다.

| 소켓 | 역할 | 특성 |
|------|------|------|
| **PUB** | 발행자 | 모든 구독자에게 브로드캐스트. 수신(recv) 불가. |
| **SUB** | 구독자 | 토픽 prefix match 필터링. 송신(send) 불가. |
| **XPUB** | 고급 발행자 | PUB + 구독 프레임 수신 가능 |
| **XSUB** | 고급 구독자 | SUB + 구독 프레임 직접 송신 |

**유효한 소켓 조합:**
- PUB → SUB, PUB → XSUB
- XPUB → SUB, XPUB → XSUB

```
              ┌─────┐
         ┌───►│SUB 1│ (topic: "weather")
┌─────┐  │   └─────┘
│ PUB │──┤
└─────┘  │   ┌─────┐
         └───►│SUB 2│ (topic: "sports")
              └─────┘
```

---

# Part I: PUB/SUB

## 2. PUB/SUB 기본 사용법

### 발행자 (PUB)

```c
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

/* 메시지 발행 — 구독자가 없으면 드롭 */
zlink_send(pub, "weather: sunny", 14, 0);
```

### 구독자 (SUB)

```c
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub, "tcp://127.0.0.1:5556");

/* 토픽 구독 — connect 후 설정 */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "weather", 7);

/* 수신 (토픽 prefix 포함) */
char buf[256];
int size = zlink_recv(sub, buf, sizeof(buf), 0);
/* buf = "weather: sunny" */
```

> 참고: `core/tests/test_pubsub.cpp` — 빈 구독("") → 모든 메시지 수신

## 3. 토픽 필터링

SUB 소켓의 토픽 필터링은 **prefix match** 방식이다.

| 구독 토픽 | 수신 메시지 | 매칭 |
|-----------|-------------|:----:|
| `"weather"` | `"weather: sunny"` | O |
| `"weather"` | `"weathering storm"` | O |
| `"weather"` | `"sports: baseball"` | X |
| `""` (빈 문자열) | 모든 메시지 | O |

### 다중 토픽 구독

```c
/* 여러 토픽 구독 */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "weather", 7);
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "sports", 6);

/* 구독 해제 */
zlink_setsockopt(sub, ZLINK_UNSUBSCRIBE, "sports", 6);
```

### 빈 구독 (모든 메시지)

```c
/* 빈 문자열 구독 — 모든 메시지 수신 */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);
```

> 참고: `core/tests/test_pubsub.cpp` — `zlink_setsockopt(subscriber, ZLINK_SUBSCRIBE, "", 0)`

## 4. 메시지 형식

PUB/SUB 메시지는 두 가지 형식을 사용할 수 있다.

### 단일 프레임 (토픽 포함)

토픽이 데이터에 포함된 형태. 간단하지만 파싱이 필요하다.

```c
/* 발행 */
zlink_send(pub, "weather: sunny", 14, 0);

/* 수신: 전체 문자열에서 토픽과 데이터를 파싱 */
char buf[256];
int size = zlink_recv(sub, buf, sizeof(buf), 0);
/* buf = "weather: sunny" */
```

### 멀티파트 프레임 (토픽 + 데이터 분리)

토픽과 데이터를 별도 프레임으로 전송. 파싱 불필요.

```c
/* 발행: [topic][payload] */
zlink_send(pub, "weather", 7, ZLINK_SNDMORE);
zlink_send(pub, "sunny", 5, 0);

/* 수신: 프레임별 처리 */
char topic[64], payload[256];
zlink_recv(sub, topic, sizeof(topic), 0);    /* "weather" */
zlink_recv(sub, payload, sizeof(payload), 0); /* "sunny" */
```

## 5. PUB/SUB 소켓 옵션

### SUB 전용 옵션

| 옵션 | 타입 | 설명 |
|------|------|------|
| `ZLINK_SUBSCRIBE` | binary | 토픽 구독 추가 (prefix match) |
| `ZLINK_UNSUBSCRIBE` | binary | 토픽 구독 해제 |

### 공통 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_SNDHWM` | int | 1000 | 송신 HWM (PUB) |
| `ZLINK_RCVHWM` | int | 1000 | 수신 HWM (SUB) |
| `ZLINK_LINGER` | int | -1 | close 시 대기 시간 (ms) |

## 6. PUB/SUB 사용 패턴

### 패턴 1: 기본 PUB/SUB

```c
/* PUB */
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

/* SUB — 모든 메시지 수신 */
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);

msleep(100);  /* 구독이 PUB에 도달할 시간 */

zlink_send(pub, "test", 4, 0);

char buf[64];
int size = zlink_recv(sub, buf, sizeof(buf), 0);
```

> 참고: `core/tests/test_pubsub.cpp` — `test_tcp()`

### 패턴 2: 다중 SUB

하나의 PUB에 여러 SUB가 연결. 각 SUB는 자신의 토픽만 수신.

```c
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

void *sub_weather = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub_weather, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub_weather, ZLINK_SUBSCRIBE, "weather", 7);

void *sub_sports = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub_sports, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub_sports, ZLINK_SUBSCRIBE, "sports", 6);

/* weather만 sub_weather가 수신, sports만 sub_sports가 수신 */
```

### 패턴 3: 다중 PUB → SUB

SUB는 여러 PUB에 connect 가능. Fair-queue로 모든 PUB의 메시지를 수신.

```c
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);
zlink_connect(sub, "tcp://pub1:5556");
zlink_connect(sub, "tcp://pub2:5557");
```

## 7. PUB/SUB 주의사항

### Slow Subscriber (HWM 초과 시 드롭)

PUB은 느린 구독자에게 메시지를 드롭한다. 수신 속도가 발행 속도보다 느리면 HWM 도달 후 메시지 유실.

```c
/* HWM 조정으로 버퍼 확대 */
int hwm = 100000;
zlink_setsockopt(pub, ZLINK_SNDHWM, &hwm, sizeof(hwm));
```

### Late Joiner (구독 전 메시지 유실)

SUB가 connect한 뒤 구독 메시지가 PUB에 도달하기 전에 발행된 메시지는 유실된다.

```c
/* 구독이 PUB에 전파될 시간 필요 */
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "topic", 5);
msleep(100);  /* 구독 전파 대기 */
/* 이후 발행된 메시지부터 수신 가능 */
```

### 방향 제약

```c
/* PUB은 recv 불가 → ENOTSUP */
zlink_recv(pub, buf, sizeof(buf), 0);  /* errno = ENOTSUP */

/* SUB는 send 불가 → ENOTSUP */
zlink_send(sub, "data", 4, 0);  /* errno = ENOTSUP */
```

---

# Part II: XPUB/XSUB

## 8. XPUB/XSUB 개요

XPUB/XSUB는 구독 프레임을 애플리케이션에서 직접 다룰 수 있는 고급 발행-구독 소켓이다. 프록시/브로커 구축, 구독 모니터링, Last-Value Caching에 사용된다.

```
┌─────┐     ┌──────────────┐     ┌─────┐
│ PUB │────►│ XSUB ── XPUB │────►│ SUB │
└─────┘     │   (Proxy)    │     └─────┘
┌─────┐     │              │     ┌─────┐
│ PUB │────►│              │────►│ SUB │
└─────┘     └──────────────┘     └─────┘
```

## 9. 구독 프레임 형식

XPUB/XSUB 간의 구독/해제 프레임은 다음 형식을 따른다:

| 바이트 | 의미 |
|--------|------|
| `0x01` + topic | 구독 요청 |
| `0x00` + topic | 구독 해제 |

```c
/* XSUB에서 구독 전송 */
const uint8_t subscribe[] = {0x01, 'A'};    /* "A" 토픽 구독 */
zlink_send(xsub, subscribe, 2, 0);

/* XSUB에서 구독 해제 */
const uint8_t unsubscribe[] = {0x00, 'A'};  /* "A" 토픽 해제 */
zlink_send(xsub, unsubscribe, 2, 0);
```

XPUB는 구독 프레임을 `zlink_recv()`로 수신한다:

```c
uint8_t buf[256];
int size = zlink_recv(xpub, buf, sizeof(buf), 0);
if (buf[0] == 0x01) {
    /* 구독 요청: buf+1 = 토픽 */
} else if (buf[0] == 0x00) {
    /* 구독 해제: buf+1 = 토픽 */
}
```

> 참고: `core/tests/test_xpub_manual.cpp` — `subscription1[] = {1, 'A'}`, `unsubscription1[] = {0, 'A'}`

## 10. XPUB 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_XPUB_MANUAL` | int | 0 | 수동 구독 관리 모드 활성화 |
| `ZLINK_XPUB_VERBOSE` | int | 0 | 중복 구독 메시지도 전달 |
| `ZLINK_SUBSCRIBE` | binary | — | (MANUAL 모드) 현재 파이프에 구독 추가 |
| `ZLINK_UNSUBSCRIBE` | binary | — | (MANUAL 모드) 현재 파이프에서 구독 해제 |

### XPUB_MANUAL 모드

기본적으로 XPUB는 SUB의 구독을 자동 처리한다. MANUAL 모드에서는 구독 프레임을 수신한 후, 애플리케이션이 직접 `ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`로 실제 구독을 결정한다.

```c
/* MANUAL 모드 활성화 */
int manual = 1;
zlink_setsockopt(xpub, ZLINK_XPUB_MANUAL, &manual, sizeof(manual));

/* 구독 프레임 수신 */
uint8_t buf[256];
int size = zlink_recv(xpub, buf, sizeof(buf), 0);
/* buf = {0x01, 'A'} — "A" 토픽 구독 요청 */

/* 원본 토픽 대신 다른 토픽으로 구독 변환 */
zlink_setsockopt(xpub, ZLINK_SUBSCRIBE, "XA", 2);

/* 발행 */
zlink_send(xpub, "A", 1, 0);   /* 구독자에게 도달하지 않음 */
zlink_send(xpub, "XA", 2, 0);  /* 구독자가 수신 */
```

> 참고: `core/tests/test_xpub_manual.cpp` — `test_basic()`: A 구독 요청 → B로 변환

## 11. XPUB/XSUB 사용 패턴

### 패턴 1: 프록시/브로커 구축

XSUB(프론트엔드) + XPUB(백엔드)로 PUB/SUB 프록시를 구축한다.

```c
/* 프록시 프론트엔드: PUB들이 연결 */
void *xsub = zlink_socket(ctx, ZLINK_XSUB);
zlink_bind(xsub, "tcp://*:5556");

/* 프록시 백엔드: SUB들이 연결 */
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_bind(xpub, "tcp://*:5557");

/* 프록시 루프: 양방향으로 메시지 전달 */
zlink_pollitem_t items[] = {
    {xsub, 0, ZLINK_POLLIN, 0},
    {xpub, 0, ZLINK_POLLIN, 0},
};

while (1) {
    zlink_poll(items, 2, -1);

    if (items[0].revents & ZLINK_POLLIN) {
        /* 데이터 메시지: XSUB → XPUB */
        zlink_msg_t msg;
        zlink_msg_init(&msg);
        zlink_msg_recv(&msg, xsub, 0);
        int more = zlink_msg_more(&msg);
        zlink_msg_send(&msg, xpub, more ? ZLINK_SNDMORE : 0);
        zlink_msg_close(&msg);
    }
    if (items[1].revents & ZLINK_POLLIN) {
        /* 구독 프레임: XPUB → XSUB */
        zlink_msg_t msg;
        zlink_msg_init(&msg);
        zlink_msg_recv(&msg, xpub, 0);
        zlink_msg_send(&msg, xsub, 0);
        zlink_msg_close(&msg);
    }
}
```

### 패턴 2: MANUAL 모드 프록시 (구독 변환)

구독 요청을 변환하거나 필터링하는 고급 프록시.

```c
int manual = 1;
zlink_setsockopt(xpub, ZLINK_XPUB_MANUAL, &manual, sizeof(manual));

/* 구독 수신 */
uint8_t sub_frame[256];
int size = zlink_recv(xpub, sub_frame, sizeof(sub_frame), 0);

if (sub_frame[0] == 0x01) {
    /* 구독 요청: 원본 토픽을 변환하여 등록 */
    char *topic = (char *)(sub_frame + 1);
    int topic_len = size - 1;
    zlink_setsockopt(xpub, ZLINK_SUBSCRIBE, topic, topic_len);

    /* 업스트림(XSUB)에 구독 전파 */
    zlink_send(xsub, sub_frame, size, 0);
} else if (sub_frame[0] == 0x00) {
    /* 구독 해제 */
    char *topic = (char *)(sub_frame + 1);
    int topic_len = size - 1;
    zlink_setsockopt(xpub, ZLINK_UNSUBSCRIBE, topic, topic_len);

    zlink_send(xsub, sub_frame, size, 0);
}
```

> 참고: `core/tests/test_xpub_manual.cpp` — `test_xpub_proxy_unsubscribe_on_disconnect()`

### 패턴 3: 구독 모니터링

XPUB로 어떤 클라이언트가 어떤 토픽을 구독하는지 관찰.

```c
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_bind(xpub, "tcp://*:5557");

/* 구독 프레임 수신 */
uint8_t buf[256];
int size = zlink_recv(xpub, buf, sizeof(buf), ZLINK_DONTWAIT);
if (size > 0) {
    if (buf[0] == 0x01)
        printf("새 구독: %.*s\n", size - 1, buf + 1);
    else if (buf[0] == 0x00)
        printf("구독 해제: %.*s\n", size - 1, buf + 1);
}
```

### 패턴 4: 구독자 해제 시 자동 unsubscribe

SUB가 연결을 끊으면 XPUB에 자동으로 unsubscribe 프레임이 전달된다.

```c
/* SUB 연결 해제 후 */
zlink_close(sub);

/* XPUB에서 unsubscribe 프레임 수신 */
uint8_t buf[256];
int size = zlink_recv(xpub, buf, sizeof(buf), 0);
/* buf[0] == 0x00: 해제 프레임 */
```

> 참고: `core/tests/test_xpub_manual.cpp` — `test_xpub_proxy_unsubscribe_on_disconnect()`

## 12. 주의사항

### 구독 전파 타이밍

구독 메시지는 비동기로 전파된다. 구독 직후 발행된 메시지는 수신하지 못할 수 있다.

```c
zlink_connect(sub, endpoint);
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "topic", 5);
/* 이 시점에 "topic" 메시지를 발행하면 유실 가능 */
msleep(100);  /* 구독 전파 대기 */
/* 이후 발행 시 수신 가능 */
```

### XPUB MANUAL 모드에서 구독 관리

MANUAL 모드에서 구독 프레임을 수신한 후 `ZLINK_SUBSCRIBE`를 호출하지 않으면 해당 구독은 등록되지 않는다. 반드시 명시적으로 구독을 처리해야 한다.

### 다중 구독자 → 단일 XPUB

여러 SUB가 같은 토픽을 구독하면, 모든 SUB가 해제될 때까지 XPUB의 구독이 유지된다.

> 참고: `core/tests/test_xpub_manual.cpp` — `test_missing_subscriptions()`: 두 구독자를 순차 처리하여 누락 방지
