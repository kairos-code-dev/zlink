# 메시지 API 상세

## 1. 개요

zlink 메시지는 `zlink_msg_t` 구조체로 표현되며, 64바이트 고정 크기이다. 소형 메시지는 inline 저장(VSM), 대형 메시지는 별도 할당(LMSG)으로 처리된다.

## 2. 메시지 유형

| 유형 | 조건 | 메모리 | 사용 시점 |
|------|------|--------|-----------|
| VSM (Very Small Message) | ≤33B (64-bit) | msg_t 내부 inline 저장 | 소형 데이터, 가장 빈번 |
| LMSG (Large Message) | >33B | malloc'd 버퍼, 참조 카운팅 | 대형 데이터 |
| CMSG (Constant Message) | 상수 데이터 | 외부 포인터 참조 (복사 없음) | `zlink_send_const()` |
| ZCLMSG (Zero-copy Large) | zero-copy | 외부 버퍼 + 해제 콜백 | `zlink_msg_init_data()` |

> 내부 메모리 레이아웃(VSM/LMSG 구조체 상세)은 [architecture.md](../internals/architecture.md)를 참고.

## 3. 메시지 생명주기

### 3.1 초기화 — zlink_msg_init vs zlink_msg_init_size vs zlink_msg_init_data

#### zlink_msg_init — 빈 메시지

수신용 메시지나 초기화 용도로 사용. 데이터 없이 생성.

```c
zlink_msg_t msg;
zlink_msg_init(&msg);
/* 이후 zlink_msg_recv()로 데이터 수신, 또는 zlink_msg_close()로 해제 */
```

#### zlink_msg_init_size — 크기 지정 (복사 필요)

지정된 크기의 버퍼를 할당한 후, `zlink_msg_data()`로 데이터를 직접 채운다. **데이터를 메시지 내부로 복사**하는 패턴.

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
memcpy(zlink_msg_data(&msg), source_data, 1024);
zlink_msg_send(&msg, socket, 0);
```

**사용 시점:** 자체 버퍼의 데이터를 메시지로 만들 때. 원본 버퍼를 바로 해제해도 안전.

#### zlink_msg_init_data — 외부 버퍼 참조 (zero-copy)

외부 버퍼의 소유권을 메시지에 이전. 복사 없이 전송. 해제 콜백(ffn)으로 버퍼 정리.

```c
void my_free(void *data, void *hint) {
    free(data);
}

void *buf = malloc(4096);
memcpy(buf, source_data, 4096);

zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, 4096, my_free, NULL);
/* buf는 이제 메시지가 소유. 직접 free 하지 않음 */
zlink_msg_send(&msg, socket, 0);
/* 전송 완료 후 my_free(buf, NULL) 자동 호출 */
```

**사용 시점:** 대용량 데이터의 복사를 피하고 싶을 때. 버퍼 해제 시점을 라이브러리에 위임.

> 참고: `core/tests/test_msg_ffn.cpp` — free function 콜백 동작 검증

### 3.2 데이터 접근

```c
void *data = zlink_msg_data(&msg);
size_t size = zlink_msg_size(&msg);
int more = zlink_msg_more(&msg);  /* 다음 프레임 존재 여부 */
```

### 3.3 전송

```c
/* 성공 시 msg 소유권이 라이브러리로 이전 */
int rc = zlink_msg_send(&msg, socket, 0);
if (rc == -1) {
    /* 실패: 호출자가 여전히 소유 */
    zlink_msg_close(&msg);
}
```

### 3.4 수신

```c
zlink_msg_t msg;
zlink_msg_init(&msg);
int rc = zlink_msg_recv(&msg, socket, 0);
if (rc != -1) {
    printf("수신: %.*s\n",
           (int)zlink_msg_size(&msg),
           (char *)zlink_msg_data(&msg));
}
zlink_msg_close(&msg);
```

### 3.5 해제

```c
zlink_msg_close(&msg);
```

## 4. 소유권 규칙

| 상황 | 소유권 | 이후 동작 |
|------|--------|-----------|
| `zlink_msg_send` 성공 | 라이브러리로 이전 | msg는 빈 상태, 접근 불가 |
| `zlink_msg_send` 실패 | 호출자가 여전히 소유 | `zlink_msg_close()` 호출 필요 |
| `zlink_msg_recv` 성공 | 라이브러리가 msg에 데이터 채움 | `zlink_msg_close()` 호출 필요 |
| `zlink_msg_close` | 리소스 해제 | msg 재사용 가능 (재초기화 필요) |

### 소유권 규칙 실전

```c
/* 패턴 1: 전송 성공 → msg 자동 정리 */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "Hello", 5);
int rc = zlink_msg_send(&msg, socket, 0);
if (rc != -1) {
    /* 성공: msg는 이제 빈 상태. close 호출해도 안전하지만 불필요 */
}

/* 패턴 2: 전송 실패 → 수동 정리 필요 */
rc = zlink_msg_send(&msg, socket, ZLINK_DONTWAIT);
if (rc == -1) {
    /* 실패: msg는 여전히 유효. 반드시 close 필요 */
    zlink_msg_close(&msg);
}

/* 패턴 3: send 후 msg 데이터 접근 — 위험! */
zlink_msg_send(&msg, socket, 0);
/* zlink_msg_data(&msg);  ← 미정의 동작! */
```

## 5. Zero-Copy 패턴 상세

### Free Function 콜백 작성법

```c
/* 기본 free 콜백 */
void simple_free(void *data, void *hint) {
    free(data);
}

/* hint를 활용한 콜백 */
void pool_free(void *data, void *hint) {
    struct memory_pool *pool = (struct memory_pool *)hint;
    pool_return(pool, data);
}

/* 알림 콜백 (데이터 자체는 해제하지 않음) */
void notify_free(void *data, void *hint) {
    /* 데이터가 더 이상 사용되지 않음을 알림 */
    memcpy(hint, "freed", 5);
    /* data는 외부에서 관리 */
}
```

> 참고: `core/tests/test_msg_ffn.cpp` — `ffn()` 콜백이 hint에 "freed" 기록

### Free Function 호출 시점

```c
/* 1. 메시지 close 시 호출 */
zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_msg_close(&msg);  /* → my_free(buf, NULL) 호출 */

/* 2. 전송 완료 후 호출 */
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_msg_send(&msg, socket, 0);
/* 전송 완료 시점에 my_free(buf, NULL) 호출 */

/* 3. 복사 후 원본 해제 시 호출 */
zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
zlink_msg_close(&msg);
zlink_msg_close(&copy);  /* 마지막 참조 해제 시 my_free 호출 */
```

> 참고: `core/tests/test_msg_ffn.cpp` — close/send/copy 각 시나리오

### zlink_send_const — 상수 데이터 전송

복사 없이 상수(리터럴, static) 데이터를 전송. free function 불필요.

```c
/* 문자열 리터럴 직접 전송 */
zlink_send_const(socket, "Hello", 5, 0);

/* 멀티파트 */
zlink_send_const(socket, "foo", 3, ZLINK_SNDMORE);
zlink_send_const(socket, "foobar", 6, 0);
```

> 참고: `core/tests/test_pair_inproc.cpp` — `test_zlink_send_const()`

## 6. Multipart 메시지 실전 패턴

멀티파트 메시지는 `ZLINK_SNDMORE` 플래그로 연속 프레임을 전송한다. 수신 측에서는 `zlink_msg_more()`로 다음 프레임 존재 여부를 확인한다.

### 패턴 1: 요청-응답 (DEALER/ROUTER)

```c
/* DEALER → ROUTER: 단일 프레임 전송 */
zlink_send(dealer, "request", 7, 0);

/* ROUTER 수신: [routing_id][request] — 2프레임 멀티파트 */
zlink_msg_t rid, data;
zlink_msg_init(&rid);
zlink_msg_init(&data);
zlink_msg_recv(&rid, router, 0);   /* more=1 */
zlink_msg_recv(&data, router, 0);  /* more=0 */

/* ROUTER 응답: routing_id + 데이터 */
zlink_msg_send(&rid, router, ZLINK_SNDMORE);
zlink_send(router, "reply", 5, 0);

zlink_msg_close(&rid);
zlink_msg_close(&data);
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER 멀티파트

### 패턴 2: 토픽 + 데이터 (PUB/SUB)

```c
/* PUB: [topic][payload] */
zlink_send(pub, "weather", 7, ZLINK_SNDMORE);
zlink_send(pub, "sunny", 5, 0);

/* SUB: 멀티파트 수신 */
char topic[64], payload[256];
zlink_recv(sub, topic, sizeof(topic), 0);
zlink_recv(sub, payload, sizeof(payload), 0);
```

### 패턴 3: 범용 멀티파트 수신 루프

```c
do {
    zlink_msg_t frame;
    zlink_msg_init(&frame);
    zlink_msg_recv(&frame, socket, 0);

    printf("프레임[%zu bytes]: %.*s\n",
           zlink_msg_size(&frame),
           (int)zlink_msg_size(&frame),
           (char *)zlink_msg_data(&frame));

    int more = zlink_msg_more(&frame);
    zlink_msg_close(&frame);

    if (!more) break;
} while (1);
```

## 7. 메시지 복사

### zlink_msg_copy — 참조 카운팅 복사

데이터를 복사하지 않고 참조 카운트를 증가시킨다. 대용량 메시지에서 효율적.

```c
zlink_msg_t original, copy;
zlink_msg_init_size(&original, 1024);
memcpy(zlink_msg_data(&original), data, 1024);

zlink_msg_init(&copy);
zlink_msg_copy(&copy, &original);

/* original과 copy 모두 같은 데이터를 참조 */
/* ZLINK_SHARED 속성이 1로 변경됨 */
int shared = zlink_msg_get(&copy, ZLINK_SHARED);
/* shared == 1 */

zlink_msg_close(&original);
zlink_msg_close(&copy);  /* 마지막 참조 해제 시 실제 메모리 해제 */
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_refcounted()`: copy 후 SHARED 속성 확인

### ZLINK_SHARED 속성

```c
/* 참조 카운팅 메시지 */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
int shared = zlink_msg_get(&msg, ZLINK_SHARED);  /* 0: 단일 소유 */

zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
shared = zlink_msg_get(&copy, ZLINK_SHARED);  /* 1: 공유 */

/* 상수 데이터 메시지 */
zlink_msg_t const_msg;
zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
shared = zlink_msg_get(&const_msg, ZLINK_SHARED);  /* 1: 항상 공유 */
```

> 참고: `core/tests/test_msg_flags.cpp` — `test_shared_const()`: 상수 메시지의 SHARED 속성

## 8. 에러 처리

### 전송 실패

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 100);
memcpy(zlink_msg_data(&msg), data, 100);

int rc = zlink_msg_send(&msg, socket, ZLINK_DONTWAIT);
if (rc == -1) {
    if (errno == EAGAIN) {
        /* HWM 초과: 나중에 재시도 */
    } else if (errno == ENOTSUP) {
        /* 해당 소켓에서 send 불가 (예: SUB 소켓) */
    } else if (errno == ETERM) {
        /* 컨텍스트 종료 */
    }
    /* 실패 시 msg는 여전히 유효 → 반드시 close */
    zlink_msg_close(&msg);
}
```

### 부분 전송 (멀티파트)

멀티파트 메시지의 중간 프레임 전송이 실패하면, 이전에 전송된 프레임은 이미 큐에 들어가 있다. 원자적(atomic) 전송이 아니므로, 수신 측에서 불완전한 멀티파트를 처리할 준비가 필요하다.

```c
/* 프레임 1 전송 성공 */
zlink_send(socket, "header", 6, ZLINK_SNDMORE);

/* 프레임 2 전송 실패 (HWM 등) */
int rc = zlink_send(socket, "body", 4, ZLINK_DONTWAIT);
if (rc == -1) {
    /* header는 이미 큐에 있음 — 수신 측에 불완전한 메시지 전달됨 */
}
```

## 9. zlink_send vs zlink_msg_send

| | `zlink_send` | `zlink_msg_send` |
|---|---|---|
| **입력** | 버퍼 포인터 + 크기 | zlink_msg_t |
| **복사** | 내부에서 msg 생성 + 복사 | zero-copy 가능 |
| **소유권** | 원본 버퍼 유지 | msg 소유권 이전 |
| **사용 시점** | 소형 데이터, 간단한 전송 | 대형 데이터, zero-copy |

```c
/* 간단한 전송 */
zlink_send(socket, "Hello", 5, 0);

/* zero-copy 전송 */
zlink_msg_t msg;
zlink_msg_init_data(&msg, large_buf, large_size, my_free, NULL);
zlink_msg_send(&msg, socket, 0);
```
