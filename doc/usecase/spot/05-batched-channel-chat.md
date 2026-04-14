[← 이벤트 전파](04-microservice-event.md) | [목록](README.md)

# 배치 기반 채널 메시징 (채팅)

이 문서는 SPOT 기능을 조합해서 메신저와 비슷한 구조를 만드는 방법을
보여 주는 case study다. 여기서 설명하는 구조는 운영 환경의 정답을 고정하려는
문서가 아니다. "SPOT을 이런 식으로 활용할 수 있다"는 출발점에 가깝다.

## 문제

채팅 시스템은 보통 아래 두 가지를 함께 처리해야 한다.

- 누가 어느 채널에 속해 있는지 관리해야 한다.
- 한 사용자가 보낸 메시지를 그 채널의 다른 사용자에게 빠르게 전달해야 한다.

채널 참여자가 많아지면, 응용 코드가 매번 사용자 목록을 돌면서 직접 보내는 방식은
금방 복잡해진다. 같은 채널 메시지를 여러 서버로 나눠 보내야 할 때는 fan-out
경로도 함께 복잡해진다.

이 문서는 이 문제를 다음처럼 단순하게 나눠 본다.

- 채널마다 하나의 `channel spot`을 둔다.
- 온라인 세션마다 하나의 `session spot`을 둔다.
- 세션이 메시지를 보낼 때는 `channel spot`으로 직접 전달한다.
- `channel spot`은 잠깐 메시지를 모았다가 채널 topic으로 발행한다.
- 각 `session spot`은 자신이 참가한 채널 topic을 구독하고 있다가 메시지를 받는다.

## 이 문서에서 보는 모델

### Channel Spot

채널마다 하나의 SPOT facade를 둔다.

- 채널의 입력 창구 역할을 한다.
- 세션이 보낸 routed 메시지를 받는다.
- 채널 안에서 잠깐 메시지를 모아 배치로 만든다.
- 배치를 채널 topic으로 발행한다.

이 문서에서는 `1초 배치`를 예시 정책으로 사용한다. 이 값은 설명을 쉽게 하려고
잡은 값이다. 실제 제품에서는 더 짧게 잡거나, 본문 메시지와 상태성 이벤트를
다르게 다룰 수 있다.

### Session Spot

온라인 세션마다 하나의 SPOT facade를 둔다.

- WebSocket 연결, 앱 연결, 데스크톱 연결 같은 live 세션 단위로 생각한다.
- 자신이 참가한 채널 topic을 구독한다.
- 채널에서 온 배치를 받아 클라이언트에 전달한다.
- 사용자가 메시지를 입력하면 대상 `channel spot`으로 routed 전송한다.

오프라인 사용자는 이 문서의 범위 밖이다. 푸시 알림이나 재접속 복구는 별도
구성 요소로 붙인다고 가정한다.

## 구조 그림

```text
+---------------- Session Server ----------------+
|                                                |
|  session spot:user-A                           |
|    subscriptions: room:123, room:456           |
|    client: WebSocket / App                     |
|                                                |
|  session spot:user-B                           |
|    subscriptions: room:123                     |
|    client: WebSocket / App                     |
|                                                |
+--------------------+---------------------------+
                     |
                     | routed send
                     v
+---------------- Room Owner Server -------------+
|                                                |
|  channel spot:room:123                         |
|    collect for 1 second                        |
|    publish topic: room:123:out                 |
|                                                |
+--------------------+---------------------------+
                     |
                     | topic publish
                     v
+---------------- Session Server ----------------+
|                                                |
|  session spot:user-C                           |
|    subscriptions: room:123                     |
|    client: WebSocket / App                     |
|                                                |
+------------------------------------------------+
```

이 구조의 핵심은 채널 입력과 채널 배포를 한 곳으로 모으는 데 있다.
세션은 채널로 직접 메시지를 넣고, 채널은 topic 발행으로 배포를 맡는다.

## 메시지 흐름

```text
1. session spot:user-A sends routed message to channel spot:room:123
2. channel spot:room:123 receives the message
3. channel spot collects messages for 1 second
4. channel spot publishes one batch to topic room:123:out
5. subscribed session spots receive the batch
6. each session spot forwards messages to its local client
```

이 흐름에서는 응용 코드가 직접 "이 방의 모든 사용자"를 돌 필요가 없다.
세션은 구독만 관리하고, 실제 topic fan-out은 SPOT data plane이 맡는다.

다만 아주 큰 채널에서는 여전히 전달 대상 수가 크다. 이 문서의 목적은 그 비용을
없애는 것이 아니라, 채널 단위 입력과 구독 기반 배포를 단순한 구조로 묶어 보는
데 있다.

## 왜 이 방식이 SPOT와 잘 맞는가

- `channel spot`은 routed 주소를 가진 입력 지점이 되기 쉽다.
- `session spot`은 topic 구독을 가진 수신 지점이 되기 쉽다.
- 같은 facade에서 routed와 topic을 함께 쓸 수 있어서 구조가 단순하다.
- 서버 간 전달은 SPOT Node mesh가 맡고, 응용 코드는 채널과 세션 역할에만
  집중할 수 있다.

SPOT 내부에도 mesh 전송을 위한 짧은 배치가 있다. 여기서 말하는 `1초 배치`는
그 위에 올리는 응용 레벨 정책이다. 즉, 이 문서의 포인트는 SPOT 내부 구현을
대체하는 것이 아니라, SPOT 위에서 채널 정책을 한 번 더 세우는 데 있다.

## 부하를 보는 관점

이 구조에서는 응용 레벨 fan-out이 아래처럼 바뀐다.

| 구간 | 역할 |
|------|------|
| `session -> channel` | 메시지 1건당 routed send 1회 |
| `channel -> mesh` | 배치 주기마다 topic publish 1회 |
| `mesh -> session` | 구독 중인 세션으로 자동 fan-out |

응용 코드 기준으로 보면 "채널 참여자 목록을 순회하며 직접 전송"하는 루프를 없앨 수
있다. 대신 주의 깊게 봐야 할 숫자는 아래 두 가지다.

- `온라인 세션 수`
- `세션당 구독 채널 수`

즉, 이 구조의 병목은 메시지 개수만이 아니라 `구독 수 총량`
(`subscription cardinality`)에도 영향을 받는다.

## 핵심 코드

아래 코드는 개념을 보여 주기 위한 예시다. 실제 구현에서는 에러 처리,
메시지 저장, 배치 버퍼 관리, 세션 생명주기 정리가 더 필요하다.

### 1. channel spot 준비

```c
typedef struct room_batch_t {
    void *spot;
    char topic[64];
    zlink_msg_t *msgs;
    size_t count;
} room_batch_t;

static zlink_routing_id_t make_rid(const char *text)
{
    zlink_routing_id_t rid;
    size_t len = strlen(text);

    memset(&rid, 0, sizeof(rid));
    rid.size = (uint8_t)len;
    memcpy(rid.data, text, len);
    return rid;
}

static void on_room_message(const zlink_routing_id_t *source_node_rid,
                            const zlink_routing_id_t *source_spot_rid,
                            uint64_t request_seq,
                            zlink_msg_t *parts,
                            size_t part_count,
                            void *userdata)
{
    room_batch_t *batch = (room_batch_t *)userdata;

    append_to_batch(batch, parts, part_count);

    if (should_flush_after_1s(batch)) {
        zlink_publish(batch->spot, batch->topic, batch->msgs, batch->count, 0);
        reset_batch(batch);
    }
}

void *room_node = zlink_spot_node_new(ctx);
void *room_spot = zlink_spot_new(room_node);
room_batch_t room_batch = {
  .spot = room_spot,
};

strcpy(room_batch.topic, "room:123:out");

zlink_routing_id_t room_rid = make_rid("room:123");
zlink_set_routing_id(room_spot, room_rid.data, room_rid.size);
zlink_spot_handler(room_spot, on_room_message, &room_batch);
```

이 예시에서는 `room:123`이 routed 입력 주소가 되고, `room:123:out`이 topic
배포 주소가 된다.

### 2. session spot 준비

```c
static void on_session_delivery(const zlink_routing_id_t *source_rid,
                                const char *topic,
                                size_t topic_len,
                                zlink_msg_t *parts,
                                size_t part_count,
                                void *userdata)
{
    session_ctx_t *session = (session_ctx_t *)userdata;
    deliver_batch_to_client(session, topic, parts, part_count);
}

void *session_node = zlink_spot_node_new(ctx);
void *session_spot = zlink_spot_new(session_node);

zlink_set_routing_id(session_spot, "session:user-A:mobile",
                     strlen("session:user-A:mobile"));

zlink_set_subscription(session_spot, "room:123:out");
zlink_set_subscription(session_spot, "room:456:out");
zlink_subscribe_handler(session_spot, on_session_delivery, session_ctx);
```

세션이 채널에 들어오면 해당 topic을 구독하고, 채널을 떠나면 구독을 해제하면
된다.

### 3. 세션이 채널로 메시지 보내기

```c
zlink_msg_t msg;
zlink_routing_id_t room_spot_rid = make_rid("room:123");
zlink_routing_id_t room_node_rid;

zlink_msg_init_size(&msg, len);
memcpy(zlink_msg_data(&msg), text, len);

zlink_discovery_resolve_spot(discovery, &room_spot_rid, &room_node_rid);

zlink_spot_send_spot(session_spot,
                     &room_node_rid,
                     &room_spot_rid,
                     &msg,
                     1,
                     0);
```

세션은 논리 채널 이름인 `room:123`만 알고 있어도 된다. 실제 owner node는
discovery로 찾고, 전송은 기존 routed send 경로를 사용한다.

## 확장 아이디어

이 구조는 아래 항목을 붙이기 쉽다.

- `push notification`: 세션이 없을 때만 별도 worker가 알림을 보낸다.
- `read receipt`: 별도 topic 또는 별도 배치 정책으로 분리한다.
- `typing`: 본문 메시지와 분리해서 더 짧은 주기로 합칠 수 있다.
- `history`: DB에 저장한 sequence를 기준으로 재접속 복구를 붙인다.

즉, `channel spot`은 채널 입력과 배포의 중심이 되고, 나머지 기능은 별도
lane으로 나눠 확장할 수 있다.

## 이 문서에서 일부러 단순화한 점

- `1초 배치`는 예시 정책이다.
- 채널 멤버십 검증은 생략했다.
- 배치 버퍼 메모리 관리와 오류 복구는 생략했다.
- 오프라인 메시지, push, 재접속 catch-up은 생략했다.
- 아주 큰 채널의 추가 분산 전략은 다루지 않았다.

이 case study의 목적은 메신저 전체를 완성하는 것이 아니다. SPOT 하나를
채널 단위 입력 지점으로 쓰고, 다른 SPOT을 온라인 세션 단위 수신 지점으로 써서
메시징 구조를 만들 수 있음을 보여 주는 데 목적이 있다.
