[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# 모니터링 API 사용법

## 1. 개요

zlink 모니터링 API는 소켓의 연결/해제/핸드셰이크 등 이벤트를 실시간으로 관찰할 수 있다. 콜백 기반으로 동작하며, 이벤트 발생 시 등록된 핸들러 함수가 자동으로 호출된다.

패밀리별 control contract와 회귀 테스트 기준은
[socket-family-monitor-contract-spec.ko.md](../plan/direct-callback-recv/socket-family-monitor-contract-spec.ko.md)에
별도로 정리한다. 이 가이드는 실제 사용 시 어떤 event를 gate로 써야 하는지에
집중한다.

성능 테스트 정책 문서는 이 가이드의 ready gate 규칙을 그대로 참조한다.
특히 perf/bench의 start gate는 **monitor delivery-ready event만** 사용해야 하며,
`sleep`, 고정 지연, monitor snapshot polling으로 ready를 판정하면 안 된다.

## 2. 모니터 활성화

### 2.1 콜백 기반 (권장)

```c
/* 이벤트 핸들러 정의 */
void on_monitor_event(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("이벤트: 0x%llx\n", (unsigned long long)ev->event);
    printf("로컬: %s\n", ev->local_addr);
    printf("원격: %s\n", ev->remote_addr);

    if (ev->routing_id.size > 0) {
        printf("routing_id: ");
        for (uint8_t i = 0; i < ev->routing_id.size; ++i)
            printf("%02x", ev->routing_id.data[i]);
        printf("\n");
    }
}

void *server = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(server, "tcp://*:5555");

/* 모니터 생성 (옵션 기반) */
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon = zlink_socket_monitor_open(server, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
```

이벤트 발생 시 `on_monitor_event` 콜백이 자동으로 호출된다.

### 이벤트 구조체

```c
typedef struct {
    uint64_t event;               /* 이벤트 타입 */
    uint64_t value;               /* 보조 값 (fd, errno, reason 등) */
    zlink_routing_id_t routing_id; /* 상대방 routing_id */
    char local_addr[256];         /* 로컬 주소 */
    char remote_addr[256];        /* 원격 주소 */
} zlink_monitor_event_t;
```

## 4. Socket Monitor 이벤트

`zlink_socket_monitor_open()`으로 관찰하는 이벤트다.
raw 소켓의 transport/session 상태를 알려준다.

### 이벤트 전체 표

| 상수 | 값 | 설명 | `value` | `routing_id` | 발생 측 | 이후 가능한 동작 |
|---|---|---|---|---|---|---|
| `CONNECTION_READY_CHANGED` | `0x1000` | 핸드셰이크 완료, 메시징 가능 | `current_ready_count` | ROUTER/STREAM: peer id | 양쪽 | **send/recv 시작** |
| `CONNECTED` | `0x0001` | TCP 연결 성립 (핸드셰이크 전) | fd | — | 클라이언트 | `CONNECTION_READY_CHANGED` 대기 |
| `ACCEPTED` | `0x0020` | 수신 연결 accept (핸드셰이크 전) | fd | — | 서버 | `CONNECTION_READY_CHANGED` 대기 |
| `DISCONNECTED` | `0x0200` | 세션 종료 | reason 코드 | 가능 | 양쪽 | 재연결 로직 실행 |
| `LISTENING` | `0x0008` | bind 성공, 수신 대기 중 | fd | — | 서버 | — |
| `CLOSED` | `0x0080` | 의도적 close 완료 | — | — | 양쪽 | — |
| `CONNECT_DELAYED` | `0x0002` | 비동기 연결 재시도 예약 | errno | — | 클라이언트 | 자동 재시도 |
| `CONNECT_RETRIED` | `0x0004` | 비동기 재연결 진행 중 | — | — | 클라이언트 | 자동 재시도 |
| `BIND_FAILED` | `0x0010` | bind 실패 | errno | — | 서버 | 주소/권한 확인 |
| `ACCEPT_FAILED` | `0x0040` | accept 실패 | errno | — | 서버 | fd 한도 확인 |
| `CLOSE_FAILED` | `0x0100` | close 실패 | errno | — | 양쪽 | — |
| `HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | 핸드셰이크 실패 (일반) | errno | — | 양쪽 | 네트워크 확인 |
| `HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | 프로토콜 오류로 실패 | 에러 코드 | — | 양쪽 | 버전/설정 확인 |
| `HANDSHAKE_FAILED_AUTH` | `0x4000` | 인증 실패 | — | — | 양쪽 | TLS/인증 설정 확인 |
| `SUB_DELIVERY_READY_CHANGED` | `0x8000` | SUB subscription 전파 완료 | `1`=ready, `0`=lost | — | SUB 측 | **`zlink_subscribe()` 수신 시작** |
| `PUB_DELIVERY_READY_CHANGED` | `0x10000` | PUB subscriber 준비 완료 | `1`=ready, `0`=lost | — | PUB 측 | **`zlink_publish()` delivery 시작** |
| `MONITOR_STOPPED` | `0x0400` | 모니터 종료 | — | — | 양쪽 | `zlink_monitor_close()` |

### 연결 흐름

```
클라이언트: CONNECT_DELAYED(선택) → CONNECTED → CONNECTION_READY_CHANGED → send/recv 시작
서버:       LISTENING → ACCEPTED → CONNECTION_READY_CHANGED → send/recv 시작
종료:       CONNECTION_READY_CHANGED → DISCONNECTED → CONNECT_DELAYED → 재연결...
```

### CONNECTION_READY_CHANGED 상세

핸드셰이크 완료 후 발생한다. 이 이벤트를 받으면 즉시 메시지를 보내고 받을 수 있다.
`value` 필드에는 `current_ready_count` -- 현재 ready 피어의 절대 수가 포함된다.

- ROUTER/STREAM에서는 `ev->routing_id`에 peer identity가 포함된다.
- PAIR/DEALER에서는 `routing_id`가 비어 있다.

### DISCONNECTED reason 코드

| 코드 | 이름 | 의미 |
|------|------|------|
| 0 | `UNKNOWN` | 원인 불명 |
| 1 | `LOCAL` | 로컬에서 의도적 종료 |
| 2 | `REMOTE` | 원격 피어 정상 종료 |
| 3 | `HANDSHAKE_FAILED` | 핸드셰이크 실패 |
| 4 | `TRANSPORT_ERROR` | 전송계층 오류 |
| 5 | `CTX_TERM` | 컨텍스트 종료 |

### 프로토콜 에러 코드

`HANDSHAKE_FAILED_PROTOCOL` 발생 시 `value` 필드에 다음 코드 중 하나가 포함된다:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 형식의 ZMP HELLO 커맨드. |

## 5. 이벤트 흐름 다이어그램

### 연결 성공

```
클라이언트 측:
  CONNECT_DELAYED (선택) → CONNECTED → CONNECTION_READY_CHANGED

서버 측:
  ACCEPTED → CONNECTION_READY_CHANGED
```

### 핸드셰이크 실패

```
클라이언트 측:
  CONNECTED → HANDSHAKE_FAILED_* → DISCONNECTED

서버 측:
  ACCEPTED → HANDSHAKE_FAILED_* → DISCONNECTED
```

### 정상 해제

```
CONNECTION_READY_CHANGED → DISCONNECTED (reason=LOCAL or REMOTE)
```

### 재연결

```
CONNECTED → CONNECTION_READY_CHANGED → DISCONNECTED →
CONNECT_DELAYED → CONNECT_RETRIED → CONNECTED → CONNECTION_READY_CHANGED
```

## 6. DISCONNECTED reason 코드

`DISCONNECTED` 이벤트의 `value` 필드에 해제 사유가 포함된다.

| 코드 | 이름 | 의미 | 대응 방법 |
|------|------|------|-----------|
| 0 | UNKNOWN | 원인 불명 | 로그 기록 후 관찰 |
| 1 | LOCAL | 로컬에서 의도적 종료 | 정상 동작, 처리 불필요 |
| 2 | REMOTE | 원격 피어 정상 종료 | 재연결 로직 실행 |
| 3 | HANDSHAKE_FAILED | 핸드셰이크 실패 | TLS/프로토콜 설정 확인 |
| 4 | TRANSPORT_ERROR | 전송계층 오류 | 네트워크 상태 확인 |
| 5 | CTX_TERM | 컨텍스트 종료 | 종료 처리 |

### reason 코드 처리 예제

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_DISCONNECTED) {
        switch (ev->value) {
            case 0: printf("원인 불명 해제\n"); break;
            case 1: printf("로컬 종료\n"); break;
            case 2:
                printf("원격 피어 종료 — 재연결 시도\n");
                /* 재연결 로직 */
                break;
            case 3:
                printf("핸드셰이크 실패 — TLS 설정 확인\n");
                break;
            case 4:
                printf("전송 오류 — 네트워크 확인\n");
                break;
            case 5:
                printf("컨텍스트 종료\n");
                break;
        }
    }
}
```

## 7. 이벤트 필터링 및 구독 프리셋

### 특정 이벤트만 구독

```c
/* 연결/해제 이벤트만 */
zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED,
};
void *mon = zlink_socket_monitor_open(server, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
```

### 권장 구독 프리셋

| 프리셋 | 이벤트 마스크 | 용도 |
|--------|-------------|------|
| 기본 | `CONNECTION_READY_CHANGED \| DISCONNECTED` | 연결 상태 추적 |
| 디버깅 | 기본 + `CONNECTED \| ACCEPTED \| CONNECT_DELAYED \| CONNECT_RETRIED` | 연결 과정 상세 |
| 보안 | 기본 + `HANDSHAKE_FAILED_*` | 인증 실패 감지 |
| 전체 | `ZLINK_EVENT_ALL` | 모든 이벤트 |

### 프리셋 구현 예제

```c
/* 기본 프리셋 */
#define MONITOR_PRESET_BASIC \
    (ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED)

/* 디버깅 프리셋 */
#define MONITOR_PRESET_DEBUG \
    (MONITOR_PRESET_BASIC | ZLINK_EVENT_CONNECTED | \
     ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_CONNECT_DELAYED | \
     ZLINK_EVENT_CONNECT_RETRIED)

/* 보안 프리셋 */
#define MONITOR_PRESET_SECURITY \
    (MONITOR_PRESET_BASIC | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL | \
     ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL | \
     ZLINK_EVENT_HANDSHAKE_FAILED_AUTH)

zlink_socket_monitor_open_options_t sec_opts = { .events = MONITOR_PRESET_SECURITY };
void *mon = zlink_socket_monitor_open(server, &sec_opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
```

## 8. Socket Monitor Snapshot

monitor handle에서 현재 aggregate 상태를 바로 조회할 수 있다.

```c
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *monitor = zlink_socket_monitor_open(socket, &opts);
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(monitor, &snapshot);
printf("ready peers: %u, sndq=%llu, rcvq=%llu\n",
       snapshot.ready_count,
       (unsigned long long) snapshot.snd_pending_msgs,
       (unsigned long long) snapshot.rcv_pending_msgs);
```

event 콜백 안에서 snapshot을 조합해 쓸 수도 있다.

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY_CHANGED) {
        zlink_monitor_snapshot_t snapshot;
        zlink_monitor_snapshot(g_monitor, &snapshot);
        printf("현재 ready peers: %u\n", snapshot.ready_count);
    }
}
```

## 8.1 서비스 모니터

서비스 모니터는 Gateway, SPOT, Discovery 같은 서비스 핸들의
상태 변화를 관찰한다. socket monitor와는 별도 API다.

- **이벤트 타입**: `zlink_service_event_t` (socket monitor의 `zlink_monitor_event_t`와 다름)
- **콜백 타입**: `zlink_service_monitor_handler_fn`
- **열기**: `zlink_service_monitor_open(target, &options)`
- **닫기**: `zlink_monitor_close(&mon)` (socket monitor와 동일)

### 서비스 모니터 열기

```c
/* Gateway 서비스 모니터 */
zlink_service_monitor_open_options_t opts = {
    .events = ZLINK_SERVICE_MONITOR_EVENT_ALL
};
void *mon = zlink_service_monitor_open(gateway, &opts);
```

대상 handle(discovery, gateway, spot, spot_node)을 넘기면 된다.
handle 종류는 런타임에 자동 판별된다.

### 콜백 모드

```c
void on_gateway_event(const zlink_service_event_t *ev, void *userdata)
{
    if (ev->event_type & ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED) {
        printf("send ready: %u\n", ev->value);
    }
    if (ev->event_type & ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP) {
        printf("route up, ready routes: %u\n", ev->value);
    }
}

zlink_service_monitor_handler(mon, on_gateway_event, NULL);
```

### Recv 모드

```c
zlink_service_event_t ev;
int rc = zlink_service_monitor_recv(mon, &ev);
if (rc == 0) {
    printf("event: 0x%x, value: %u\n", ev.event_type, ev.value);
}
```

### 서비스 이벤트 전체 표

`zlink_service_monitor_open()`으로 관찰하는 이벤트다.
서비스별로 발생하는 이벤트가 다르다.

#### Gateway 이벤트

| 상수 | 설명 | `value` | 이후 가능한 동작 |
|---|---|---|---|
| `GATEWAY_MONITOR_EVENT_READY_CHANGED` | 서비스 준비 상태 변화 | `current_ready_count` | — |
| `GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED` | send 가능 상태 변화 | `current_ready_count` | **value>0이면 `zlink_gateway_send()` 시작** |
| `GATEWAY_MONITOR_EVENT_ROUTE_UP` | peer route 활성화 | 현재 ready route 수 | — |
| `GATEWAY_MONITOR_EVENT_ROUTE_DOWN` | peer route 비활성화 | 현재 ready route 수 | — |

#### SPOT 이벤트

| 상수 | 설명 | `value` | 이후 가능한 동작 |
|---|---|---|---|
| `SUB_DELIVERY_READY_CHANGED` | sub delivery 준비 상태 변화 | — | **수신 시작 가능** |
| `PUB_FIRST_DELIVERY_READY_CHANGED` | 최소 1개 subscriber 준비 | — | **`zlink_publish()` delivery 시작** |
| `SPOT_SUB_FILTER_APPLIED` | 구독 필터가 peer에 전파됨 | — | — |
| `SPOT_SUB_SUBSCRIPTION_READY` | 구독 수신 준비 완료 | — | — |
| `SPOT_PUB_DELIVERY_READY_CHANGED` | subject별 remote delivery-ready 변화 | — | — |

#### Discovery 이벤트

| 상수 | 설명 | `value` | 이후 가능한 동작 |
|---|---|---|---|
| `DISCOVERY_SERVICE_UP` | 검색된 서비스 활성화 | — | — |
| `DISCOVERY_SERVICE_DOWN` | 검색된 서비스 비활성화 | — | — |
| `DISCOVERY_PROVIDERS_CHANGED` | provider 집합 변경 | — | — |

#### 공통 이벤트 (모든 서비스)

| 상수 | 설명 |
|---|---|
| `MONITOR_EVENT_ERROR` | 에러 발생 |
| `MONITOR_EVENT_CLOSED` | 모니터 닫힘 |

상세 이벤트 목록은 [events.ko.md](../api/events.ko.md)를 참고한다.

## 9. 다중 소켓 모니터링

여러 소켓의 이벤트를 각각의 콜백 핸들러로 처리.

```c
void on_event_a(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("소켓 A 이벤트: 0x%llx\n", (unsigned long long)ev->event);
}

void on_event_b(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("소켓 B 이벤트: 0x%llx\n", (unsigned long long)ev->event);
}

zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon_a = zlink_socket_monitor_open(sock_a, &opts);
zlink_socket_monitor_handler(mon_a, on_event_a, NULL);
void *mon_b = zlink_socket_monitor_open(sock_b, &opts);
zlink_socket_monitor_handler(mon_b, on_event_b, NULL);

/* ... 애플리케이션 로직 ... */

/* 정리 */
zlink_monitor_close(&mon_a);
zlink_monitor_close(&mon_b);
```

## 10. 주의사항

### 모니터 스레드 안전성

`zlink_socket_monitor_open()`과 monitor handle close는 raw/service handle의
저빈도 control-path 계약에 속한다. 즉 애플리케이션 스레드에서 호출할 수 있고,
같은 handle과 섞여도 correctness가 유지된다. 다만 monitor callback은 I/O 경로
에서 실행되므로 callback 내부의 느린 작업은 사용자 큐로 넘기는 편이 좋다.

```c
/* 애플리케이션 스레드에서 monitor open */
void *socket = zlink_socket(ctx, ZLINK_ROUTER);
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon = zlink_socket_monitor_open(socket, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);

/* 이후 다른 작업 스레드에서 snapshot 조회 가능 */
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(mon, &snapshot);
```

### 동시 모니터 제한

동일 소켓에 동시에 여러 모니터를 설정할 수 없다.

### 콜백 처리 속도

콜백 핸들러에서 블로킹 작업을 수행하면 I/O 진행에 영향을 줄 수 있다.
느린 처리가 필요하면 콜백 안에서 사용자 queue로 넘기고 별도 thread에서 처리한다.

### 모니터 종료 절차

```c
/* 모니터 핸들 닫기 */
zlink_monitor_close(&mon);
```

## 11. 메시징 시작 전 준비 확인

소켓 또는 서비스가 실제로 메시지를 보내고 받을 수 있는 시점을 알아야 할 때,
어떤 이벤트를 기다리면 되는지 정리한다.

### 11.0 perf/bench start gate 규칙

perf/bench 구현은 아래 규칙을 따른다.

- start gate는 이 절에 명시된 monitor event만 사용한다.
- `sleep`/고정 지연으로 ready를 추정하지 않는다.
- `zlink_monitor_snapshot()`으로 ready를 polling하지 않는다.
- `CONNECTED`, `ACCEPTED`, `LISTENING`은 progress/debug 이벤트일 뿐,
  perf 시작 gate로 쓰지 않는다.
- delivery-ready event를 받기 전에는 측정 구간에 진입하지 않는다.

### 11.1 Raw 소켓 — PAIR, DEALER, ROUTER

`CONNECTION_READY_CHANGED` 이벤트를 받으면 즉시 send/recv가 가능하다.
PAIR/DEALER/ROUTER 계열의 perf start gate는 이 이벤트 하나만 사용한다.

```c
/* DEALER/ROUTER 예시 */
zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY_CHANGED
};
void *mon = zlink_socket_monitor_open(router, &opts);

/* monitor callback에서 ready 확인 */
void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
    if (ev->event & ZLINK_EVENT_CONNECTION_READY_CHANGED) {
        /* ROUTER: ev->routing_id에 peer identity가 들어있다 */
        /* 바로 routed send 가능 */
    }
}
zlink_socket_monitor_handler(mon, on_ready, NULL);
```

| 패밀리 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| PAIR | 양쪽 `CONNECTION_READY_CHANGED` | 양방향 send/recv |
| DEALER | `CONNECTION_READY_CHANGED` | send/recv |
| ROUTER | `CONNECTION_READY_CHANGED` | `ev->routing_id`로 routed send/recv |

### 11.2 Raw 소켓 — STREAM

STREAM은 다른 raw 소켓과 다르다. server는 클라이언트가 먼저 데이터를 보내야
routing_id를 확보할 수 있다. 순서:

1. 클라이언트가 raw TCP로 첫 payload를 보낸다
2. 서버에서 첫 inbound 메시지를 recv하여 routing_id를 확보한다
3. `CONNECTION_READY_CHANGED` 이벤트를 확인한다
4. 확보한 routing_id로 reply를 보낸다

```c
/* STREAM server: 먼저 recv로 routing_id 확보 → CONNECTION_READY_CHANGED 확인 → reply */
zlink_routing_id_t rid;
zlink_msg_t payload;
zlink_msg_init(&payload);
recv_stream_routing_id_and_payload(server, &rid, &payload);

/* 이 시점에서 CONNECTION_READY_CHANGED가 이미 발생했는지 monitor로 확인 */
/* rid로 reply 가능 */
zlink_stream_send_msg(server, &rid, &payload, 0);
```

| 패밀리 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| STREAM | 첫 inbound payload recv + `CONNECTION_READY_CHANGED` | `routing_id`로 reply send |

perf start gate에서는 STREAM도 snapshot이 아니라 위 두 조건으로만 ready를
판정한다.

### 11.3 Raw 소켓 — PUB/SUB

raw PUB/SUB 소켓은 socket monitor에서 delivery-ready 이벤트를 제공한다.
PUB/SUB perf start gate는 `CONNECTION_READY_CHANGED`가 아니라 아래 delivery-ready
이벤트를 사용한다. PUB과 SUB 각각에 별도 모니터를 열어서 양쪽 모두 ready를
확인한 뒤 메시징한다.

- `SUB_DELIVERY_READY_CHANGED(value=1)` — subscription이 전파되어 수신 가능
- `PUB_DELIVERY_READY_CHANGED(value=1)` — subscriber가 준비되어 publish delivery 가능

```c
zlink_set_subscription(sub, "topic");

/* SUB 모니터: subscription 전파 완료 대기 */
zlink_socket_monitor_open_options_t sub_opts = {
    .events = ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED
};
void *sub_mon = zlink_socket_monitor_open(sub, &sub_opts);

/* PUB 모니터: subscriber 준비 완료 대기 */
zlink_socket_monitor_open_options_t pub_opts = {
    .events = ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED
};
void *pub_mon = zlink_socket_monitor_open(pub, &pub_opts);

/* 양쪽 delivery-ready 확인 후 메시징 */
/* ... SUB_DELIVERY_READY_CHANGED(value=1) + PUB_DELIVERY_READY_CHANGED(value=1) 수신 ... */
zlink_publish(pub, NULL, &part, 1, 0);  /* raw PUB: topic_id는 NULL */
zlink_subscribe(sub, &parts, &count, 0, topic_buf, &topic_len);

zlink_monitor_close(&pub_mon);
zlink_monitor_close(&sub_mon);
```

| 패밀리 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| PUB | `PUB_DELIVERY_READY_CHANGED(value=1)` | `zlink_publish()` delivery |
| SUB | `SUB_DELIVERY_READY_CHANGED(value=1)` | `zlink_subscribe()` 수신 |

### 11.4 서비스 — Gateway

`GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED(value>0)` 이벤트를 받으면 바로 send가 가능하다.
Gateway perf start gate는 이 이벤트 하나만 사용한다.

```c
/* client gateway에 service monitor 열기 */
zlink_service_monitor_open_options_t opts = {
    .events = ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED
              | ZLINK_GATEWAY_MONITOR_EVENT_ERROR
};
void *mon = zlink_service_monitor_open(client, &opts);

/* callback으로 ready 확인 */
void on_gw(const zlink_service_event_t *ev, void *userdata) {
    if (ev->event_type == ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED && ev->value > 0) {
        /* route 준비 완료 — 바로 zlink_gateway_send() 가능 */
    }
}
zlink_service_monitor_handler(mon, on_gw, NULL);
```

### 11.5 서비스 — SPOT

SPOT은 sub과 pub에 각각 별도 service monitor를 열어서 다른 이벤트를 구독한다.

```c
/* sub 모니터: SUB_DELIVERY_READY_CHANGED 구독 */
zlink_service_monitor_open_options_t sub_opts = {
    .events = ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
              | ZLINK_MONITOR_EVENT_ERROR
};
void *sub_mon = zlink_service_monitor_open(sub_node, &sub_opts);

/* pub 모니터: PUB_FIRST_DELIVERY_READY_CHANGED 구독 */
zlink_service_monitor_open_options_t pub_opts = {
    .events = ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
              | ZLINK_MONITOR_EVENT_ERROR
};
void *pub_mon = zlink_service_monitor_open(pub_node, &pub_opts);

/* 양쪽 모두 ready 확인 후 메시징 시작 */
/* sub ready → zlink_subscribe()로 수신 가능 */
/* pub ready → zlink_publish()로 delivery 가능 */
```

| 서비스 | 기다릴 이벤트 | 이후 가능한 동작 |
|---|---|---|
| Gateway | `GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED(value>0)` | `zlink_gateway_send()` |
| SPOT sub | `SUB_DELIVERY_READY_CHANGED` | `zlink_subscribe()` 수신 시작 |
| SPOT pub | `PUB_FIRST_DELIVERY_READY_CHANGED` | `zlink_publish()` delivery 시작 |

perf policy에서 service start gate는 위 이벤트만 사용한다. snapshot/status 조회는
운영 관찰용으로는 가능하지만, perf 시작 판정에는 쓰지 않는다.

### 11.3 Snapshot

`zlink_monitor_snapshot()`과 `zlink_*_status_snapshot()`은
현재 상태를 조회하는 용도다. 운영 대시보드, health check, 디버깅에 활용한다.

```c
/* 현재 gateway 상태 확인 */
zlink_gateway_status_t status;
zlink_gateway_status_snapshot(gateway, &status);
printf("state=%d, ready_providers=%u\n", status.state, status.ready_provider_count);
```

---
[← TLS 보안](05-tls-security.ko.md) | [서비스 개요 →](07-0-services.ko.md)
