[English](03-2-pubsub.md) | [한국어](03-2-pubsub.ko.md)

# PUB/SUB/XPUB/XSUB 발행-구독

## 1. 개요

발행-구독(Publish-Subscribe) 패턴은 메시지를 토픽 기반으로 분배한다.
zlink는 기본 PUB/SUB과 고급 XPUB/XSUB 두 가지 레벨을 제공한다.

| 소켓 | 역할 | 특성 |
|------|------|------|
| **PUB** | 발행자 | 모든 구독자에게 브로드캐스트. 수신 불가. |
| **SUB** | 구독자 | 토픽 prefix match 필터링. 송신 불가. |
| **XPUB** | 고급 발행자 | PUB + 구독 이벤트 수신 가능 |
| **XSUB** | 고급 구독자 | 로컬 필터링 없이 모든 메시지 수신. 프록시/중계용 |

**유효한 소켓 조합:**
- PUB → SUB, PUB → XSUB
- XPUB → SUB, XPUB → XSUB

### SUB vs XSUB — 핵심 차이

SUB과 XSUB은 모두 `zlink_set_subscription()`으로 구독 정보를
upstream PUB에 전송한다. 공개 API 사용법은 동일하다.
차이는 **로컬 필터 엔진의 on/off**다.

| | SUB (`filter=true`) | XSUB (`filter=false`) |
|---|---|---|
| 구독 있을 때 | 매칭되는 메시지만 수신 | **모든 메시지 수신** (필터 체크 안 함) |
| 구독 없을 때 | **아무것도 수신하지 않음** | **모든 메시지 수신** |
| `""` 빈 구독 | 모든 메시지 수신 (모든 토픽 매칭) | 구독 없이도 이미 전부 수신 |
| 용도 | 일반 구독자 | 프록시/중계 (전체 스트림 통과) |

내부적으로 `xsub_t::xrecv()`의 필터 조건은 `!options.filter || match(msg)`다.
SUB(`filter=true`)은 매 메시지마다 `match()`를 평가하고,
XSUB(`filter=false`)은 `!false = true`이므로 `match()`를 건너뛴다.

> **흔한 혼동:** "SUB에 `""` 빈 구독을 넣으면 XSUB과 같지 않나?"
> → 결과적으로 모든 메시지를 수신하는 것은 같지만,
> SUB은 매 메시지마다 trie match 비용이 발생하고,
> XSUB은 필터 체크 자체를 건너뛴다.
> 또한 SUB은 구독이 **없으면** 아무것도 받지 못하지만,
> XSUB은 구독 없이도 전부 받는다.

**프록시 패턴에서 XSUB/XPUB을 쓰는 이유:**

```
PUB ──── XSUB ═══ XPUB ──── SUB
          │         │
          │  프록시  │
          └─────────┘
```

- XSUB은 구독 상태 없이 PUB의 모든 메시지를 통과시킨다.
- XPUB은 SUB의 구독 이벤트를 `zlink_subscription_event()`로 노출하여
  프록시가 구독 관리 로직(필터링, 로깅, 인가 등)을 삽입할 수 있다.
- 일반 SUB/PUB으로는 이 중계 구조를 만들 수 없다.

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

/* zlink_subscribe() 또는 zlink_subscribe_handler()로 수신 */
```

> 참고: `core/tests/test_pubsub.cpp` — 빈 구독("") → 모든 메시지 수신

### 송수신 요약

| 소켓 | 방향 | 수신 API | 비고 |
|------|------|----------|------|
| PUB | 송신 전용 | N/A | 수신 불가 (`ENOTSUP`) |
| SUB | 수신 전용 | `zlink_subscribe()` / `zlink_subscribe_handler()` | 토픽 + 데이터 분리 반환 |
| XPUB | 양방향 | `zlink_subscription_event()` | 구독 이벤트 수신 |
| XSUB | 수신 전용 | `zlink_subscribe()` / `zlink_subscribe_handler()` | 필터 없이 전체 수신 |

> **참고:** PUB/SUB 계열 4소켓에서 `zlink_send()`/`zlink_recv()`는
> 모두 `ENOTSUP`이다. 발행은 `zlink_publish()`, 수신은
> `zlink_subscribe()` / `zlink_subscribe_handler()`를 사용한다.

PUB/SUB 계열 소켓의 수신은 두 가지 모드를 지원한다:

- **Pull 모드** (기본): `zlink_subscribe()`로 토픽과 데이터를 분리하여 직접 수신
- **Callback 모드**: `zlink_subscribe_handler()`로 콜백을 등록하면 메시지 도착 시 자동 dispatch

callback attach 이후 `zlink_subscribe()`와 data-plane `ZLINK_POLLIN`은
`EBUSY`로 실패한다.

??? example "Full Sample Code -- Recv"

    | Language | Source |
    |----------|--------|
    | C | [pubsub_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pubsub_recv_sample.c) |
    | C++ | [pubsub_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pubsub_recv_sample.cpp) |
    | Java | [PubSubRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PubSubRecvSample.java) |
    | Python | [pubsub_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pubsub_recv.py) |
    | Node | [pubsub_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pubsub_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PubSubRecv/Program.cs) |
    | Rust | [pubsub_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pubsub_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pubsub_recv_sample/main.go) |

??? example "Full Sample Code -- Callback"

    | Language | Source |
    |----------|--------|
    | C | [pubsub_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pubsub_callback_sample.c) |
    | C++ | [pubsub_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pubsub_callback_sample.cpp) |
    | Java | [PubSubCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PubSubCallbackSample.java) |
    | Python | [pubsub_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pubsub_callback.py) |
    | Node | [pubsub_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pubsub_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PubSubCallback/Program.cs) |
    | Rust | [pubsub_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pubsub_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pubsub_callback_sample/main.go) |

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

`zlink_publish()`는 **토픽**과 **멀티파트 메시지**를 별도 파라미터로 받는다.
다른 소켓의 `zlink_send()`와 마찬가지로 기본이 멀티파트이다.

```c
int zlink_publish (void *subject,
                   const char *topic_id,      /* 토픽 문자열 */
                   zlink_msg_t *parts,         /* 데이터 프레임 배열 */
                   size_t part_count,           /* 프레임 수 */
                   zlink_send_flags_t flags);
```

```c
/* 발행: topic = "sensor:cpu", payload = 2개 프레임 */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 4);
memcpy(zlink_msg_data(&parts[0]), "host", 4);
zlink_msg_init_size(&parts[1], 2);
memcpy(zlink_msg_data(&parts[1]), "73", 2);
zlink_publish(pub, "sensor:cpu", parts, 2, 0);

/* SUB 수신 (zlink_subscribe 또는 subscribe_handler 콜백):
   topic     = "sensor:cpu"
   parts[0]  = "host"
   parts[1]  = "73" */
```

토픽은 wire에서 첫 프레임으로 전송되고, `zlink_subscribe()` /
`zlink_subscribe_handler()`가 토픽과 데이터를 분리하여 반환한다.
호출자가 토픽 프레임을 직접 조립할 필요 없다.

> **참고:** `zlink_publish(pub, NULL, parts, ...)`처럼 topic을 NULL로 전달하면
> parts[0]이 토픽 프레임으로 사용되는 레거시 호환 경로가 동작하지만,
> 이 방식은 권장하지 않는다. 항상 `topic_id` 파라미터를 명시적으로 전달한다.

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

/* SUB: zlink_subscribe()로 수신. publish 불가 */
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

| 항목 | SUB | XSUB |
|------|-----|------|
| **토픽 등록** | `zlink_set_subscription()` | `zlink_set_subscription()` (동일) |
| **메시지 수신** | `zlink_subscribe()` — 토픽 필터링 후 수신 | `zlink_subscribe()` — 필터 없이 전체 수신 |
| **로컬 필터** | **켜짐** — 매칭 안 되면 드롭 | **꺼짐** — 모든 메시지 통과 |
| **구독 없는 상태** | 아무것도 수신 안 함 | 모든 메시지 수신 |
| **구현** | `xsub_t` subclass (`filter=true`) | base class (`filter=false`) |

XSUB이 프록시에서 필요한 이유는 구독 상태 없이도 모든 메시지를
통과시키기 때문이다. 토픽 등록은 `zlink_set_subscription()`으로
양쪽 모두 동일하게 upstream에 전송된다.

### PUB vs XPUB — 핵심 차이

| 항목 | PUB | XPUB |
|------|-----|------|
| **메시지 발행** | `zlink_publish()` | `zlink_publish()` (동일) |
| **구독 이벤트** | 노출 안 함 | `zlink_subscription_event()`로 수신 |

XPUB는 어떤 client가 어떤 topic을 구독/해지했는지 알 수 있다.

### PUB/SUB 소켓 공개 API 요약

| 공개 API | PUB | SUB | XPUB | XSUB |
|----------|-----|-----|------|------|
| `zlink_publish()` | 가능 | — | 가능 | — |
| `zlink_subscribe()` | — | 가능 | — | 가능 |
| `zlink_subscribe_handler()` | — | 가능 | — | 가능 |
| `zlink_set_subscription()` | — | 가능 | — | 가능 |
| `zlink_subscription_event()` | — | — | 가능 | — |
| 로컬 필터 | N/A | **켜짐** | N/A | **꺼짐** |

> `zlink_send()` / `zlink_recv()`는 PUB/SUB 계열 4소켓 모두 `ENOTSUP`이다.
> 발행은 `zlink_publish()`, 수신은 `zlink_subscribe()` 전용 API를 사용한다.

> Proxy 패턴에서 XSUB/XPUB을 사용하는 방법은
> [Proxy 가이드](03-6-proxy.ko.md)를 참고.

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

기본적으로 XPUB는 SUB의 구독을 자동 처리한다.
MANUAL 모드에서는 구독 프레임을 수신한 후, 애플리케이션이 직접
`zlink_set_subscription()` / `zlink_unset_subscription()`로 실제 구독을 결정한다.

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
