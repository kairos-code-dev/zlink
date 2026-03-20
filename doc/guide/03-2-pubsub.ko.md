[English](03-2-pubsub.md) | [한국어](03-2-pubsub.ko.md)

# PUB/SUB/XPUB/XSUB 발행-구독

## 1. 개요

발행-구독(Publish-Subscribe) 패턴은 메시지를 토픽 기반으로 분배한다. zlink는 기본 PUB/SUB과 고급 XPUB/XSUB 두 가지 레벨을 제공한다.

| 소켓 | 역할 | 특성 |
|------|------|------|
| **PUB** | 발행자 | 모든 구독자에게 브로드캐스트. 수신 불가. |
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
zlink_msg_t part;
zlink_msg_init_size(&part, 14);
memcpy(zlink_msg_data(&part), "weather: sunny", 14);
zlink_publish(pub, NULL, &part, 1, 0);
```

### 구독자 (SUB)

```c
void on_topic(const zlink_routing_id_t *source_rid,
              const char *topic, size_t topic_len,
              zlink_msg_t *parts, size_t part_count,
              void *userdata)
{
    printf("Topic: %.*s, Data: %.*s\n",
           (int)topic_len, topic,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub, "tcp://127.0.0.1:5556");

/* 토픽 구독 — connect 후 설정 */
zlink_set_subscription(sub, "weather");

/* recv 모드를 유지하고 zlink_subscribe() 또는 zlink_recv()로 수신 */
```

> 참고: `core/tests/test_pubsub.cpp` — 빈 구독("") → 모든 메시지 수신

### 송수신 요약

| 소켓 | 방향 | 등록 호출 | 비고 |
|------|------|----------|------|
| PUB | 송신 전용 | N/A | 수신 불가; 핸들러를 받지 않음 |
| SUB | 수신 전용 | `zlink_subscribe()` / `zlink_recv()` pull | raw SUB는 recv-only |
| XPUB | 양방향 | `zlink_subscription_event()` pull | 데이터가 아닌 구독 이벤트 수신 |
| XSUB | 양방향 | `zlink_subscribe()` / `zlink_recv()` pull | 구독 프레임 송신; fair-queue로 데이터 수신 |

raw PUB/SUB 소켓의 canonical 모델은 recv/poller다. topic-aware pull은
`zlink_subscribe()`로 제공되고, callback topic dispatch는 raw SUB/XSUB가
아니라 `spot` / `spot_node`에 남아 있다.

**Pull 모드**도 SUB에서 사용 가능하다: 핸들러를 부착하지 않고
`zlink_recv()`를 호출하면 토픽 분리 없이 멀티파트 메시지를 수신한다.

> PUB의 송신 큐가 가득 차면(HWM) 블로킹 대신 메시지를 **드롭**한다.
> 상세는 [성능 가이드](10-performance.ko.md)를 참고.

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
zlink_set_subscription(sub, "weather");
zlink_set_subscription(sub, "sports");

/* 구독 해제 */
zlink_unset_subscription(sub, "sports");
```

### 빈 구독 (모든 메시지)

```c
/* 빈 문자열 구독 — 모든 메시지 수신 */
zlink_set_subscription(sub, "");
```

> 참고: `core/tests/test_pubsub.cpp` — `zlink_set_subscription(subscriber, "")`

## 4. 메시지 형식

PUB/SUB 메시지는 두 가지 형식을 사용할 수 있다.

### 단일 프레임 (토픽 포함)

토픽이 데이터에 포함된 형태. 간단하지만 파싱이 필요하다.

```c
/* 발행 */
zlink_msg_t part;
zlink_msg_init_size(&part, 14);
memcpy(zlink_msg_data(&part), "weather: sunny", 14);
zlink_publish(pub, NULL, &part, 1, 0);

/* SUB 핸들러 콜백 수신:
   topic = "weather: sunny" (전체 매치 prefix)
   parts[0] = "weather: sunny" (전체 데이터) */
```

### 멀티파트 프레임 (토픽 + 데이터 분리)

토픽과 데이터를 별도 프레임으로 전송. 파싱 불필요.

```c
/* 발행: [topic][payload] */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 7);
memcpy(zlink_msg_data(&parts[0]), "weather", 7);
zlink_msg_init_size(&parts[1], 5);
memcpy(zlink_msg_data(&parts[1]), "sunny", 5);
zlink_publish(pub, NULL, parts, 2, 0);

/* SUB 핸들러 콜백 수신:
   topic = "weather"
   parts[0] = "sunny" (페이로드 프레임만) */
```

## 5. PUB/SUB 소켓 옵션

### SUB 전용 함수

| 함수 | 설명 |
|------|------|
| `zlink_set_subscription()` | 토픽 구독 추가 (prefix match) |
| `zlink_unset_subscription()` | 토픽 구독 해제 |

### 공통 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 HWM (PUB) |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 HWM (SUB) |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 대기 시간 (ms) |

## 6. PUB/SUB 사용 패턴

### 패턴 1: 기본 PUB/SUB

```c
/* PUB */
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

/* SUB — 모든 메시지 수신 */
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_set_subscription(sub, "");

msleep(100);  /* 구독이 PUB에 도달할 시간 */

zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "test", 4);
zlink_publish(pub, NULL, &msg, 1, 0);

/* on_topic 콜백이 비동기로 수신 */
```

> 참고: `core/tests/test_pubsub.cpp` — `test_tcp()`

### 패턴 2: 다중 SUB

하나의 PUB에 여러 SUB가 연결. 각 SUB는 자신의 토픽만 수신.

```c
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

void *sub_weather = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub_weather, "tcp://127.0.0.1:5556");
zlink_set_subscription(sub_weather, "weather");

void *sub_sports = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub_sports, "tcp://127.0.0.1:5556");
zlink_set_subscription(sub_sports, "sports");

/* weather만 sub_weather가 수신, sports만 sub_sports가 수신 */
```

### 패턴 3: 다중 PUB → SUB

SUB는 여러 PUB에 connect 가능. Fair-queue로 모든 PUB의 메시지를 수신.

```c
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_set_subscription(sub, "");
zlink_connect(sub, "tcp://pub1:5556");
zlink_connect(sub, "tcp://pub2:5557");
```

## 7. PUB/SUB 주의사항

### Slow Subscriber (HWM 초과 시 drop)

PUB/XPUB는 기본적으로 **lossy mode**로 동작한다. 느린 subscriber의
send queue가 HWM에 도달하면 해당 subscriber에게 보내는 message를
**silent drop**한다 (error 반환 없음).

```c
/* 대응 1: HWM 조정으로 buffer 확대 */
int hwm = 100000;
zlink_set_option(pub, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
```

#### XPUB_NODROP — drop 대신 backpressure

`ZLINK_PUB_OPT_NODROP`을 활성화하면 lossy mode가 꺼진다. HWM 도달 시
message를 drop하지 않고 `EAGAIN`을 반환하여 caller가 직접
backpressure를 제어할 수 있다.

```c
/* XPUB에서 NODROP 활성화 */
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
int nodrop = 1;
zlink_set_pub_option(xpub, ZLINK_PUB_OPT_NODROP, &nodrop, sizeof(nodrop));

/* send 시 HWM 도달하면 EAGAIN 반환 (drop 아님) */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "hello", 5);
int rc = zlink_publish(xpub, NULL, &msg, 1, ZLINK_DONTWAIT);
if (rc == -1 && zlink_errno() == EAGAIN) {
    /* HWM 도달 — retry 또는 backpressure 로직 */
    zlink_msg_close(&msg);
}
```

| Mode | HWM 도달 시 동작 | 사용 시점 |
|------|------------------|-----------|
| 기본 (lossy) | Silent drop — error 없이 message 유실 | 최신 data만 중요한 경우 (sensor, tick) |
| `XPUB_NODROP=1` | `EAGAIN` 반환 — caller가 제어 | Message 유실이 허용되지 않는 경우 |

> `ZLINK_PUB_OPT_NODROP`은 XPUB socket 전용 option이다.
> 일반 PUB에서는 사용할 수 없다.

### Late Joiner (구독 전 메시지 유실)

SUB가 connect한 뒤 구독 메시지가 PUB에 도달하기 전에 발행된 메시지는 유실된다.

```c
/* 구독이 PUB에 전파될 시간 필요 */
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_set_subscription(sub, "topic");
msleep(100);  /* 구독 전파 대기 */
/* 이후 발행된 메시지부터 수신 가능 */
```

### 방향 제약

PUB/SUB는 각각 전용 API만 사용 가능하다:

```c
/* PUB: zlink_publish()로 송신. recv handler 부착 불가 */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "sunny", 5);
zlink_publish(pub, "weather", &part, 1, 0);  /* OK */

/* PUB에 zlink_send() 사용 → ENOTSUP */
zlink_send(pub, &part, 1, 0);  /* errno = ENOTSUP */

/* SUB: zlink_subscribe() / zlink_recv()로 수신. send/publish 불가 */
zlink_publish(sub, "weather", &part, 1, 0);  /* errno = ENOTSUP */
zlink_send(sub, &part, 1, 0);               /* errno = ENOTSUP */
```

---

# Part II: XPUB/XSUB

## 8. XPUB/XSUB 개요

XPUB/XSUB는 subscription frame을 application에서 직접 다룰 수 있는
고급 publish-subscribe socket이다. Proxy/broker 구축, subscription
monitoring, Last-Value Caching에 사용된다.

### SUB vs XSUB — 핵심 차이

| | SUB | XSUB |
|---|-----|------|
| **Subscribe** | `zlink_set_subscription()` (내부 자동 전송) | `zlink_set_subscription()` 또는 직접 send |
| **Send** | 불가 (`ENOTSUP`) | 가능 — subscription frame을 upstream으로 전달 |
| **Recv** | topic + payload 분리 수신 | 동일 |
| **구현** | `xsub_t` subclass, `xsend()` 차단 | base class |

SUB는 send가 불가능하므로, downstream SUB에서 올라온 subscription
request를 upstream PUB로 **forward할 수 없다**. 이것이 XSUB가 필요한
이유이다.

### PUB vs XPUB — 핵심 차이

| | PUB | XPUB |
|---|-----|------|
| **Recv** | 불가 | `zlink_subscription_event()`로 subscription event 수신 |
| **Send** | 동일 (topic broadcast) | 동일 |
| **Handler** | N/A | N/A |

XPUB는 어떤 client가 어떤 topic을 구독/해지했는지 알 수 있다.

### Proxy에서의 XSUB/XPUB 역할

```
          data flow ───────────────────────►
          subscription flow ◄──────────────

┌─────┐              Proxy               ┌─────┐
│ PUB │──connect──►┌──────┐◄──connect──  │ SUB │
│     │            │ XSUB │   ┌──────┐   │     │
└─────┘            │  │   │   │ XPUB │   └─────┘
┌─────┐            │  │   │   │      │   ┌─────┐
│ PUB │──connect──►│  ▼   │   │      │◄──│ SUB │
│     │            │ data ├──►│ data │   │     │
└─────┘            │      │   │      │   └─────┘
                   └──────┘   └──┬───┘
                      ▲          │
                      │          ▼
                   subscribe  이벤트
                   forward    수신
```

**Subscription 전파 흐름:**

1. SUB가 `"weather"` topic 구독
2. 애플리케이션이 `zlink_subscription_event()`로 XPUB 이벤트를 pull하여
   `(subscribed=1, topic="weather")`를 수신
3. Proxy가 XSUB에 `zlink_set_subscription(xsub, "weather")` 호출 →
   upstream PUB에 subscription frame `[0x01 "weather"]` 전달
4. PUB가 `"weather"` data를 publish
5. XSUB가 data recv → XPUB가 matching SUB에게 전달

**일반 SUB로는 3번이 불가능하다** — SUB는 send가 차단되어 있어
subscription을 upstream으로 forward할 수 없다. 이것이 proxy의
frontend에 반드시 XSUB를 사용하는 이유이다.

### Proxy가 필요한 이유

PUB/SUB를 직접 연결하면 구조적 한계가 있다:

```
직접 연결 (proxy 없음)               proxy 사용
─────────────────────               ──────────────

┌─────┐     ┌─────┐                 ┌─────┐     ┌───────────┐     ┌─────┐
│PUB 1│────►│SUB 1│                 │PUB 1│──►  │           │  ──►│SUB 1│
└─────┘     └─────┘                 └─────┘     │   XSUB    │     └─────┘
┌─────┐     ┌─────┐                 ┌─────┐     │     │     │     ┌─────┐
│PUB 2│────►│SUB 2│                 │PUB 2│──►  │     ▼     │  ──►│SUB 2│
└─────┘     └─────┘                 └─────┘     │   XPUB    │     └─────┘
                                                │  (Proxy)  │
 N PUB × M SUB = N×M 연결                       └───────────┘
 PUB/SUB가 서로의 주소를 알아야 함
                                                 N + M 연결
                                                 PUB/SUB는 proxy 주소만 알면 됨
```

**Proxy의 주요 용도:**

| 용도 | 설명 |
|------|------|
| **연결 수 감소** | N×M 직접 연결 → N+M (PUB→proxy, proxy→SUB) |
| **주소 decoupling** | PUB와 SUB가 서로의 endpoint를 알 필요 없음. Proxy 주소만 알면 됨 |
| **동적 확장** | PUB/SUB가 독립적으로 추가·제거 가능. 상대방에 영향 없음 |
| **Subscription 변환** | XPUB MANUAL mode로 topic remapping, filtering 가능 |
| **Network bridging** | 서로 다른 network segment 간 PUB/SUB 연결 (예: inproc ↔ tcp) |
| **Monitoring** | Capture socket으로 통과하는 message를 기록 |

`zlink_proxy()`는 XSUB↔XPUB 간 message와 subscription을
양방향으로 자동 전달하는 built-in proxy이다:

```c
/* frontend: PUB들이 connect */
void *xsub = zlink_socket(ctx, ZLINK_XSUB);
zlink_bind(xsub, "tcp://*:5556");

/* backend: SUB들이 connect */
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_bind(xpub, "tcp://*:5557");

/* capture (optional): 통과하는 모든 message를 기록 */
void *capture = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(capture, "tcp://*:5558");

zlink_proxy(xsub, xpub, capture);  /* blocking — 별도 thread에서 실행 */
```

## 9. 구독 프레임 형식

XPUB/XSUB 간의 구독/해제 프레임은 다음 형식을 따른다:

| 바이트 | 의미 |
|--------|------|
| `0x01` + topic | 구독 요청 |
| `0x00` + topic | 구독 해제 |

```c
/* XSUB에서 구독 */
zlink_set_subscription(xsub, "A");

/* XSUB에서 구독 해제 */
zlink_unset_subscription(xsub, "A");
```

XPUB는 `zlink_subscription_event()`로 구독 프레임을 수신한다:

```c
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_bind(xpub, "tcp://*:5557");

zlink_routing_id_t source_rid;
int subscribed = 0;
char topic[256];
size_t topic_len = sizeof(topic);

zlink_subscription_event(
  xpub, &source_rid, &subscribed, topic, &topic_len, 0);
```

> 참고: `core/tests/test_xpub_manual.cpp` — `subscription1[] = {1, 'A'}`, `unsubscription1[] = {0, 'A'}`

## 10. XPUB 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_PUB_OPT_MANUAL` | int | 0 | 수동 구독 관리 모드 활성화 |
| `ZLINK_PUB_OPT_VERBOSE` | int | 0 | 중복 구독 메시지도 전달 |
| `zlink_set_subscription()` | -- | -- | (MANUAL 모드) 현재 파이프에 구독 추가 |
| `zlink_unset_subscription()` | -- | -- | (MANUAL 모드) 현재 파이프에서 구독 해제 |

### XPUB_MANUAL 모드

기본적으로 XPUB는 SUB의 구독을 자동 처리한다. MANUAL 모드에서는 구독 프레임을 수신한 후, 애플리케이션이 직접 `zlink_set_subscription()` / `zlink_unset_subscription()`로 실제 구독을 결정한다.

```c
/* MANUAL 모드 활성화 */
int manual = 1;
zlink_set_pub_option(xpub, ZLINK_PUB_OPT_MANUAL, &manual, sizeof(manual));

/* zlink_subscription_event()로 subscribed=1, topic="A"를 받은 뒤
   변환된 구독 적용: */
zlink_set_subscription(xpub, "XA");

/* 발행 */
zlink_msg_t msg_a;
zlink_msg_init_size(&msg_a, 1);
memcpy(zlink_msg_data(&msg_a), "A", 1);
zlink_publish(xpub, NULL, &msg_a, 1, 0);   /* 구독자에게 도달하지 않음 */

zlink_msg_t msg_xa;
zlink_msg_init_size(&msg_xa, 2);
memcpy(zlink_msg_data(&msg_xa), "XA", 2);
zlink_publish(xpub, NULL, &msg_xa, 1, 0);  /* 구독자가 수신 */
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

/* 프록시 실행 (메시지와 구독을 양방향으로 전달) */
zlink_proxy(xsub, xpub, NULL);
```

### 패턴 2: MANUAL 모드 프록시 (구독 변환)

구독 요청을 변환하거나 필터링하는 고급 프록시.

```c
int manual = 1;
zlink_set_pub_option(xpub, ZLINK_PUB_OPT_MANUAL, &manual, sizeof(manual));

for (;;) {
    zlink_routing_id_t source_rid;
    int subscribed = 0;
    char topic[256];
    size_t topic_len = sizeof(topic);

    zlink_subscription_event(
      xpub, &source_rid, &subscribed, topic, &topic_len, 0);

    if (subscribed) {
        /* 구독 등록 */
        zlink_set_subscription(xpub, topic);

        /* 업스트림에 구독 전파 (XSUB) */
        zlink_set_subscription(xsub, topic);
    } else {
        /* 구독 해제 */
        zlink_unset_subscription(xpub, topic);

        zlink_unset_subscription(xsub, topic);
    }
}
```

> 참고: `core/tests/test_xpub_manual.cpp` — `test_xpub_proxy_unsubscribe_on_disconnect()`

### 패턴 3: 구독 모니터링

XPUB로 어떤 클라이언트가 어떤 토픽을 구독하는지 관찰.

```c
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_bind(xpub, "tcp://*:5557");

for (;;) {
    zlink_routing_id_t source_rid;
    int subscribed = 0;
    char topic[256];
    size_t topic_len = sizeof(topic);

    zlink_subscription_event(
      xpub, &source_rid, &subscribed, topic, &topic_len, 0);
    printf("%s: %.*s\n", subscribed ? "새 구독" : "구독 해제",
           (int) topic_len, topic);
}
```

### 패턴 4: 구독자 해제 시 자동 unsubscribe

SUB가 연결을 끊으면 XPUB에 자동으로 unsubscribe 프레임이 전달된다.

```c
/* SUB 연결 해제 후 */
zlink_close(sub);

/* 이어지는 zlink_subscription_event() 호출이
   subscribed=0과 기존 구독 토픽을 반환 */
```

> 참고: `core/tests/test_xpub_manual.cpp` — `test_xpub_proxy_unsubscribe_on_disconnect()`

## 12. 주의사항

### 구독 전파 타이밍

구독 메시지는 비동기로 전파된다. 구독 직후 발행된 메시지는 수신하지 못할 수 있다.

```c
zlink_connect(sub, endpoint);
zlink_set_subscription(sub, "topic");
/* 이 시점에 "topic" 메시지를 발행하면 유실 가능 */
msleep(100);  /* 구독 전파 대기 */
/* 이후 발행 시 수신 가능 */
```

### XPUB MANUAL 모드에서 구독 관리

MANUAL 모드에서 구독 프레임을 수신한 후 `zlink_set_subscription()`를 호출하지 않으면 해당 구독은 등록되지 않는다. 반드시 명시적으로 구독을 처리해야 한다.

### 다중 구독자 → 단일 XPUB

여러 SUB가 같은 토픽을 구독하면, 모든 SUB가 해제될 때까지 XPUB의 구독이 유지된다.

> 참고: `core/tests/test_xpub_manual.cpp` — `test_missing_subscriptions()`: 두 구독자를 순차 처리하여 누락 방지

---
[← PAIR](03-1-pair.ko.md) | [DEALER →](03-3-dealer.ko.md)
