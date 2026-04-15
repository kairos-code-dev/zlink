[English](09-message-api.md) | [한국어](09-message-api.ko.md)

# Message API 상세

> **Normative status: Illustrative — Needs refresh.**
> 이 가이드는 설명 목적의 문서이며, API 명칭/시그니처의 정확한 기준은
> `core/include/zlink.h`와 `bindings/README.md`다.

### 용어

| 용어 | 설명 |
|------|------|
| VSM (Very Small Message) | 33바이트 이하 메시지. `zlink_msg_t` 내부에 인라인 저장된다 |
| LMSG (Large Message) | 33바이트 초과 메시지. heap 할당 버퍼를 reference counting으로 관리한다 |
| CMSG (Constant Message) | 외부 상수 데이터를 복사 없이 참조하는 메시지 (`ffn=NULL`) |
| ZCLMSG (Zero-copy Large) | 외부 버퍼를 참조하며 해제 콜백(`ffn`)으로 수명을 관리하는 메시지 |
| multipart | 여러 프레임(part)을 하나의 논리적 메시지로 묶어 원자적으로 전송하는 방식 |
| zero-copy | 데이터를 복사하지 않고 포인터 참조만 전달하여 전송하는 기법 |
| reference count (refcount) | 같은 데이터 버퍼를 공유하는 메시지 핸들의 수. 0이 되면 버퍼가 해제된다 |
| ownership | 메시지 데이터의 소유권. send 성공 시 library로 이전되고, 실패 시 caller가 유지한다 |
| routing_id | Router 소켓이 피어를 식별하는 데 사용하는 고유 바이트 열 (최대 255바이트) |
| control part(제어 파트) | request-reply, SPOT routed 같은 상위 프로토콜이 payload 앞에 붙이는 내부 part |
| HWM (High Water Mark) | 소켓의 송신/수신 큐 최대 용량. 초과 시 backpressure가 발생한다 |

## 1. 개요

zlink message는 `zlink_msg_t` struct로 표현되며, 64 byte 고정 크기이다.
작은 data는 struct 내부에 inline 저장(VSM)하고,
큰 data는 heap buffer를 reference counting으로 관리(LMSG)한다.

## 2. Message Type

| Type | 조건 | Memory | 사용 시점 |
|------|------|--------|-----------|
| VSM (Very Small Message) | ≤33B (64-bit) | msg_t 내부 inline 저장 | 소형 data, 가장 빈번 |
| LMSG (Large Message) | >33B | malloc'd buffer, reference counted | 대형 data |
| CMSG (Constant Message) | constant data | 외부 pointer 참조 (copy 없음) | `zlink_msg_init_data(..., NULL, NULL)` |
| ZCLMSG (Zero-copy Large) | zero-copy | 외부 buffer + free callback | `zlink_msg_init_data()` |

> 내부 memory layout(VSM/LMSG struct 상세)은 [architecture.md](../internals/architecture.ko.md)를 참고.

## 3. 동작 원리

### 3.1 Memory Model

`zlink_msg_t`는 항상 64 byte 고정 struct이다. data 크기에 따라
내부 저장 전략이 자동으로 결정된다:

```text
+------------------------------------------------------+
|               zlink_msg_t  (64 bytes)                |
+------------------------------------------------------+
|                                                      |
|  VSM  (<=33B):                                       |
|    [ type | size | data ......................... ]  |
|                          ^ stored inline             |
|                                                      |
|  LMSG (>33B):                                        |
|    [ type | content_ptr ]                            |
|                   |                                  |
|                   v                                  |
|             +------------------+                     |
|             | heap buffer      |                     |
|             | + refcount       |                     |
|             +------------------+                     |
|                                                      |
|  CMSG:                                               |
|    [ type | data_ptr ]                               |
|                   |                                  |
|                   v                                  |
|             +------------------+                     |
|             | external const   |                     |
|             +------------------+                     |
|                                                      |
|  ZCLMSG:                                             |
|    [ type | data_ptr | ffn_ptr | hint | ... ]        |
|                |           |                         |
|                v           v                         |
|          +----------+  ffn(data, hint)               |
|          | user buf  |  called on release            |
|          +----------+                                |
+------------------------------------------------------+
```

핵심: `zlink_msg_t` struct 자체는 stack/배열에 놓이고, 큰 data만 heap을
사용한다. 이 구조 덕분에 message array를 stack에 선언하고 바로 send할 수
있다.

### 3.2 Function Overview

| Function | 동작 | Ownership 변화 |
|----------|------|----------------|
| `zlink_msg_init` | 빈 message 생성 | caller가 소유 |
| `zlink_msg_init_size` | size만큼 buffer 할당 | caller 소유, `memcpy`로 채움 |
| `zlink_msg_init_data` | 외부 buffer 연결 (zero-copy) | ownership이 message로 이전 |
| `zlink_msg_close` | message 해제 (refcount=0이면 free) | 소유 포기 |
| `zlink_msg_move` | src -> dest 이동, src는 빈 상태 | dest로 이전 |
| `zlink_msg_copy` | src -> dest 복사, LMSG는 refcount 증가 | dest도 공동 소유 |
| `zlink_msg_data` | data buffer pointer 반환 | 변화 없음 (읽기 전용) |
| `zlink_msg_size` | data size(byte) 반환 | 변화 없음 |
| `zlink_msg_refcnt` | storage reference count 반환 | 변화 없음 |
| `zlink_msg_gets` | message metadata property를 string으로 반환 | 변화 없음 |

### 3.3 Move vs Copy

두 함수 모두 message 내용을 다른 `zlink_msg_t`로 옮기지만 동작이 다르다:

```c
/* Handler callback receives all frames as parts array */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame[%zu bytes]: %.*s\n",
               zlink_msg_size(&parts[i]),
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}
```

- **move**: ownership 이전. src를 더 이상 사용할 수 없다. refcount 변화 없음.
- **copy**: ownership 공유. VSM이면 byte copy, LMSG이면 refcount를 증가시켜 같은 buffer를 가리킨다. 양쪽 모두 `zlink_msg_close()` 필요.

### 3.4 Close와 Reference Counting

`zlink_msg_close()`는 message type에 따라 다르게 동작한다:

| Type | `zlink_msg_close()` 동작 |
|------|--------------------------|
| VSM | struct를 빈 상태로 reset (heap 해제 없음) |
| LMSG | refcount 감소. 0이 되면 heap buffer `free()` |
| CMSG | struct를 빈 상태로 reset (외부 buffer는 건드리지 않음) |
| ZCLMSG | refcount 감소. 0이 되면 `ffn(data, hint)` callback 호출 |

```c
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
```

## 4. Message Lifecycle

### 4.1 Init — zlink_msg_init vs zlink_msg_init_size vs zlink_msg_init_data

#### zlink_msg_init — Empty Message

Recv용 message나 `zlink_msg_copy()` target으로 사용. Data 없이 생성.

```c
zlink_msg_t msg;
zlink_msg_init(&msg);
/* Used for initialization or as a target for zlink_msg_copy(). Free with zlink_msg_close() */
```

#### zlink_msg_init_size — Size 지정 (copy 필요)

지정된 size의 buffer를 할당한 후, `zlink_msg_data()`로 data를 직접 채운다.
≤33B이면 VSM(inline), >33B이면 LMSG(heap)로 자동 결정된다.

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
memcpy(zlink_msg_data(&msg), source_data, 1024);
zlink_send(socket, &msg, 1, 0);
```

**사용 시점:** 자체 buffer의 data를 message로 만들 때. 원본 buffer를 바로 해제해도 안전.

#### zlink_msg_init_data — 외부 Buffer 참조 (zero-copy)

외부 buffer의 ownership을 message에 이전. Copy 없이 전송.
Free callback(ffn)으로 buffer 정리.

```c
void my_free(void *data, void *hint) {
    free(data);
}

void *buf = malloc(4096);
memcpy(buf, source_data, 4096);

zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, 4096, my_free, NULL);
/* buf is now owned by the message. Do not free it directly */
zlink_send(socket, &msg, 1, 0);
/* my_free(buf, NULL) is called automatically after sending completes */
```

**`ffn=NULL`인 경우 (CMSG):** buffer를 해제하지 않고 borrowed reference로
유지한다. String literal이나 static data를 copy 없이 전송할 때 사용.
이 경우 `zlink_msg_refcnt()`는 항상 1을 반환한다.

```c
int zlink_send(void *s_, zlink_msg_t *parts_, size_t part_count_, zlink_send_flags_t flags_);
```

**사용 시점:** 대용량 data의 copy를 피하고 싶을 때. Buffer 해제 시점을 library에 위임.

> 참고: `core/tests/test_msg_ffn.cpp` — free function callback 동작 검증

### 4.2 Data Access

```c
void *data = zlink_msg_data(&msg);
size_t size = zlink_msg_size(&msg);
```

> **참고:** header 에는 `zlink_msg_more()` 나 `ZLINK_MORE` 가 없다.
> 애플리케이션 코드는 per-message `more` flag 대신 multipart parts-array
> API 를 사용한다.

### 4.3 Move와 Copy

#### zlink_msg_move — Ownership 이전 (zero-copy)

Message 내용을 dest로 이동시키고 src는 빈 상태가 된다.
LMSG의 경우 refcount를 증가시키지 않고 pointer만 이전한다.

```c
/* Single frame */
zlink_msg_t msg;
zlink_msg_init_data(&msg, (void *)"Hello", 5, NULL, NULL);
zlink_send(socket, &msg, 1, 0);

/* Multipart — parts array */
zlink_msg_t parts[2];
zlink_msg_init_data(&parts[0], (void *)"foo", 3, NULL, NULL);
zlink_msg_init_data(&parts[1], (void *)"foobar", 6, NULL, NULL);
zlink_send(socket, parts, 2, 0);
```

**사용 시점:** message를 다른 변수로 넘길 때. `zlink_msg_copy()`와 달리
refcount 증가가 없어 단순하다.

#### zlink_msg_copy — Reference Counted Copy

VSM은 byte 단위 value copy, LMSG/ZCLMSG는 같은 buffer를 공유하며
refcount를 증가시킨다. 양쪽 모두 `zlink_msg_close()` 필요.

```c
zlink_msg_t original, copy;
zlink_msg_init_size(&original, 1024);
memcpy(zlink_msg_data(&original), data, 1024);

zlink_msg_init(&copy);
zlink_msg_copy(&copy, &original);

/* Both original and copy reference the same data */
/* storage refcount is now 2 */
int refcnt = zlink_msg_refcnt(&copy);
/* refcnt == 2 */

zlink_msg_close(&original);
zlink_msg_close(&copy);  /* Actual memory freed when last reference is released */
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_refcounted()`

### 4.4 Metadata Property — zlink_msg_gets

Message에 부착된 metadata property를 string으로 반환한다.
해당 property가 없으면 `NULL`을 반환한다.

```c
zlink_send_rid(router, &target_rid, parts, part_count, 0);
```

### 4.5 Send

```c
/* Multipart send: pass an array of msg parts */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);

zlink_submit_result_t rc = zlink_send(socket, parts, 2, 0);
if (rc != ZLINK_SUBMIT_OK) {
    /* 실패: caller 가 parts 소유권을 계속 보유 */
    for (size_t i = 0; i < 2; i++)
        zlink_msg_close(&parts[i]);
}
```


### 4.6 Recv

Message는 socket에 등록한 핸들러 콜백으로 수신된다.
Callback이 `zlink_msg_t` part를 직접 제공한다:

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 100);
memcpy(zlink_msg_data(&part), data, 100);

zlink_submit_result_t rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
if (rc != ZLINK_SUBMIT_OK) {
    if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
        /* HWM 초과: 나중에 retry */
    } else if (rc == ZLINK_SUBMIT_NOT_SUPPORTED) {
        /* 해당 socket 에서 send 불가 (예: SUB socket) */
    } else if (rc == ZLINK_SUBMIT_TERMINATED) {
        /* Context terminated */
    }
    /* 실패 시 part 는 여전히 유효 → 반드시 close */
    zlink_msg_close(&part);
}
```

### 4.7 Close

```c
zlink_msg_close(&msg);
```

## 5. Ownership 규칙

| 상황 | Ownership | 이후 동작 |
|------|-----------|-----------|
| `zlink_send` 성공 | library로 이전 | msg part는 빈 상태, access 불가 |
| `zlink_send` 실패 | caller가 여전히 소유 | 각 part에 `zlink_msg_close()` 호출 필요 |
| 핸들러 콜백이 메시지 전달 | library가 메시지 part 제공 | 각 part에 `zlink_msg_close()` 호출 필요 |
| `zlink_msg_close` | resource 해제 | msg 재사용 가능 (재init 필요) |

### Ownership 규칙 실전

```c
/* Pattern 1: Send 성공 → msg parts 자동 정리 */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_submit_result_t rc = zlink_send(socket, &part, 1, 0);
if (rc == ZLINK_SUBMIT_OK) {
    /* 성공: part 는 이제 비어있음. close 는 안전하지만 불필요 */
}

/* Pattern 2: Send 실패 → 수동 정리 필요 */
zlink_msg_t part2;
zlink_msg_init_size(&part2, 5);
memcpy(zlink_msg_data(&part2), "Hello", 5);
rc = zlink_send(socket, &part2, 1, ZLINK_DONTWAIT);
if (rc != ZLINK_SUBMIT_OK) {
    /* 실패: part2 여전히 유효. 반드시 close */
    zlink_msg_close(&part2);
}

/* Pattern 3: Accessing msg data after send — dangerous! */
zlink_send(socket, &part, 1, 0);
/* zlink_msg_data(&part);  ← undefined behavior! */
```

## 6. Zero-Copy Pattern 상세

### Free Function Callback 작성법

```c
/* Basic free callback */
void simple_free(void *data, void *hint) {
    free(data);
}

/* Callback using hint */
void pool_free(void *data, void *hint) {
    struct memory_pool *pool = (struct memory_pool *)hint;
    pool_return(pool, data);
}

/* Notification callback (does not free the data itself) */
void notify_free(void *data, void *hint) {
    /* Notify that the data is no longer in use */
    memcpy(hint, "freed", 5);
    /* data is managed externally */
}
```

> 참고: `core/tests/test_msg_ffn.cpp` — `ffn()` callback이 hint에 "freed" 기록

### Free Function 호출 시점

```c
/* 1. Called on message close */
zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_msg_close(&msg);  /* → my_free(buf, NULL) called */

/* 2. Called after sending completes */
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_send(socket, &msg, 1, 0);
/* my_free(buf, NULL) called when sending completes */

/* 3. Called when original is freed after copy */
zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
zlink_msg_close(&msg);
zlink_msg_close(&copy);  /* my_free called when last reference is released */
```

> 참고: `core/tests/test_msg_ffn.cpp` — close/send/copy 각 시나리오

### Constant Data 전송 (CMSG)

`zlink_msg_init_data()`를 `ffn=NULL`로 호출하면 constant(literal, static) data를
copy 없이 전송할 수 있다.

```c
/* Single-part send */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_send(socket, &part, 1, 0);

/* Zero-copy send */
zlink_msg_t zcmsg;
zlink_msg_init_data(&zcmsg, large_buf, large_size, my_free, NULL);
zlink_send(socket, &zcmsg, 1, 0);

/* Multipart send */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(socket, parts, 2, 0);
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_const()`

## 7. Multipart Message 실전 Pattern

Multipart message는 `zlink_send()` 한 번의 호출로 parts array를 전송한다.

### Pattern 1: Request-Reply (DEALER/ROUTER)

```c
/* DEALER → ROUTER: send single frame */
zlink_msg_t req;
zlink_msg_init_size(&req, 7);
memcpy(zlink_msg_data(&req), "request", 7);
zlink_send(dealer, &req, 1, 0);

/* ROUTER handler callback receives: source_rid + parts */
void on_request(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* parts[0] = "request", source_rid = DEALER's routing_id */

    /* ROUTER reply: directed send via zlink_send_rid */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER multipart

### Pattern 2: Topic + Data (PUB/SUB)

```c
/* PUB: [topic][payload] as parts array */
zlink_msg_t pub_parts[2];
zlink_msg_init_size(&pub_parts[0], 7);
memcpy(zlink_msg_data(&pub_parts[0]), "weather", 7);
zlink_msg_init_size(&pub_parts[1], 5);
memcpy(zlink_msg_data(&pub_parts[1]), "sunny", 5);
zlink_send(pub, pub_parts, 2, 0);

/* SUB handler callback receives topic and payload separately */
void on_spot(const zlink_routing_id_t *source_rid,
             const char *topic, size_t topic_len,
             zlink_msg_t *parts, size_t part_count,
             void *userdata)
{
    /* topic = "weather", parts[0] = "sunny" */
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

### Pattern 3: Handler Callback에서 Multipart 처리

```c
/* Reference-counted message */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
int refcnt = zlink_msg_refcnt(&msg);  /* 1: single owner */

zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
refcnt = zlink_msg_refcnt(&copy);  /* 2: shared by msg and copy */

/* Constant data message */
zlink_msg_t const_msg;
zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
refcnt = zlink_msg_refcnt(&const_msg);  /* 1: not internally refcounted */
```

## 8. Storage Refcount — zlink_msg_refcnt

`zlink_msg_refcnt()`는 message storage의 reference count를 반환한다.
Refcounted storage가 아니면 1을 반환한다.

내부 reference count는 atomic 연산으로 관리된다. `zlink_msg_copy()`로
만든 별도 handle을 서로 다른 스레드에서 copy/close하는 것은 안전하다.
`zlink_msg_refcnt()` 반환값은 시점 스냅샷이므로 진단/assertion 용도로 쓴다.
단일 `zlink_msg_t` 인스턴스를 여러 스레드에서 동시에 접근하면 안 된다.

| 상황 | `refcnt` 반환값 |
|------|-----------------|
| `init_size` 직후 (단독 소유) | 1 |
| `copy` 후 (refcount > 1) | 2 이상 |
| `init_data(..., ffn, ...)` (ZCLMSG) | 단독 1, copy 후 2 이상 |
| `init_data(..., NULL, NULL)` (CMSG) | 항상 1 |

```c
/* Reference counted message */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
int refcnt = zlink_msg_refcnt(&msg);  /* 1: single owner */

zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
refcnt = zlink_msg_refcnt(&copy);  /* 2: msg와 copy가 공유 */

/* Constant data message (CMSG) */
zlink_msg_t const_msg;
zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
refcnt = zlink_msg_refcnt(&const_msg);  /* 1: internal refcount 대상 아님 */
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_const()`: constant message의 shared property

## 9. Error 처리

### Send 실패

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 100);
memcpy(zlink_msg_data(&part), data, 100);

zlink_submit_result_t rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
if (rc != ZLINK_SUBMIT_OK) {
    if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
        /* HWM 초과: 나중에 retry */
    } else if (rc == ZLINK_SUBMIT_NOT_SUPPORTED) {
        /* 해당 socket 에서 send 불가 (예: SUB socket) */
    } else if (rc == ZLINK_SUBMIT_TERMINATED) {
        /* Context terminated */
    }
    /* 실패 시 part 는 여전히 유효 → 반드시 close */
    zlink_msg_close(&part);
}
```

## 10. zlink_send (Multipart Msg 기반)

`zlink_send()`는 `zlink_msg_t` parts array와 part count를 받는다:

```c
zlink_msg_close(&msg);
```

```c
/* Single-part send */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_send(socket, &part, 1, 0);

/* Zero-copy send */
zlink_msg_t zcmsg;
zlink_msg_init_data(&zcmsg, large_buf, large_size, my_free, NULL);
zlink_send(socket, &zcmsg, 1, 0);

/* Multipart send */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(socket, parts, 2, 0);
```

ROUTER directed send에는 `zlink_send_rid()`를 사용한다:

```c
zlink_send_rid(router, &target_rid, parts, part_count, 0);
```


## 11. request-reply 와 metadata

`zlink_msg_t` 는 payload part 컨테이너다. 메시지 자체에 request-reply
문맥이나 per-message metadata 필드는 담지 않는다.

request-reply 흐름은 전용 API 로 연다.

- `DEALER` / `ROUTER`: [03-3-dealer.ko.md](03-3-dealer.ko.md),
  [03-4-router.ko.md](03-4-router.ko.md)
- SPOT routed request-reply: [07-3-spot.ko.md](07-3-spot.ko.md)
- 와이어(wire, 프로토콜 전송 레벨) envelope 구조: [../internals/protocol-zmp.ko.md](../internals/protocol-zmp.ko.md)

즉 message API 관점에서 기억할 점은 단순하다.

- payload 는 `zlink_msg_t` 로 만든다.
- request-reply 문맥은 message 내부 필드가 아니라 와이어 제어 파트(control part)에 있다.
- message metadata 직렬화 경로는 공개 계약이 아니다.

---
[← Routing ID](08-routing-id.ko.md) | [Performance →](10-performance.ko.md)
