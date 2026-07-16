[English](stream.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md) · [STREAM session service](../service/stream-session.ko.md) · [errno map](../errno-map.ko.md)

# 소켓 — STREAM

이 문서는 ZLink Core 10.0.0의 범용 raw STREAM 공개 계약을 정의한다. 대상 독자는 TCP/WS 연결의 byte 또는
고정 framing packet을 routing ID로 송수신하는 C API와 bindings 개발자다. 이 문서는 “STREAM의 수신 모드,
메시지 소유권과 session routing ID 의미는 무엇인가?”에 답한다.

## 1. 범위

STREAM은 accept한 client 연결마다 routing ID를 부여하는 bind 전용 raw socket이다. `zlink_connect()`는
지원하지 않는다. application은 routing ID로 client를 선택해 전송하고 수신 record에서 source routing ID를
확인한다.

STREAM은 MeshNode, Spot, ActorRef와 Actor mailbox를 알지 않는다. session–Actor binding과 Actor 이동
barrier는 별도 [STREAM session service](../service/stream-session.ko.md)가 소유한다. raw STREAM에는 Actor
binding 또는 part 단위 relay API가 없다.

## 2. 생성, bind와 option

```c
void *zlink_socket(void *ctx, zlink_socket_type_t type);
zlink_bind_result_t zlink_bind(void *socket, const char *endpoint);
zlink_close_result_t zlink_close(void *socket);

typedef enum zlink_stream_option_t {
  ZLINK_STREAM_OPT_NOTIFY = 0x3501
} zlink_stream_option_t;

zlink_config_result_t zlink_set_stream_option(
  void *stream,
  zlink_stream_option_t option,
  const void *value,
  size_t value_size);
zlink_config_result_t zlink_get_stream_option(
  void *stream,
  zlink_stream_option_t option,
  void *value,
  size_t *value_size_inout);
```

`zlink_socket(ctx, ZLINK_SOCKET_STREAM)`으로 만든다. `ZLINK_STREAM_OPT_NOTIFY`는 `int` 0 또는 1이며 bind
전에 설정한다. 값 1은 client 연결과 해제를 길이 0인 data record로 수신하게 한다. source routing ID는
연결된 client를 식별한다.

공통 `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `RCVTIMEO`, `LINGER`, TLS와 buffer option은
`zlink_set_option()`과 `zlink_get_option()`을 사용한다. STREAM은 context auto-HWM의 `stream` policy class를
사용한다. 정책을 끄면 일반 HWM 기본값 1000을 사용한다.

## 3. 수신 모드

한 STREAM handle은 다음 세 모드 가운데 하나만 사용한다.

1. raw receive: `zlink_recv()`가 complete transport record를 반환한다.
2. raw callback: `zlink_recv_handler()`가 record를 callback에 전달한다.
3. packet callback: `zlink_stream_packet_handler()`가 고정 framing packet을 조립해 전달한다.

첫 receive 또는 handler 등록이 mode를 고정한다. 다른 receive mode를 사용하거나 두 번째 handler를
등록하면 `ZLINK_RECV_BUSY` 또는 `ZLINK_HANDLER_BUSY`, `errno == EBUSY`다. data-plane `POLLIN`은 raw receive
mode로 간주한다. send-ready handler와 `POLLOUT`은 receive mode와 독립적이다.

## 4. Routed send와 raw receive

```c
zlink_submit_result_t zlink_send_rid(
  void *stream,
  const zlink_routing_id_t *target_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_recv_result_t zlink_recv(
  void *stream,
  zlink_routing_id_t *source_rid_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);
```

`zlink_send_rid()`는 target client에 complete multipart 하나를 제출한다. 성공하면 모든 input part의
소유권이 Core로 이동하고 실패하면 caller에게 남는다. 연결이 없으면 `ZLINK_SUBMIT_NOT_CONNECTED`, HWM
또는 `DONTWAIT` backpressure는 `ZLINK_SUBMIT_BACKPRESSURED`다.

`zlink_recv()`는 source routing ID와 complete multipart를 반환한다. 성공하면 Core가 할당한 part 배열과
각 `zlink_msg_t`의 소유권이 caller에게 이동한다. caller는 `zlink_multipart_close()`로 모두 해제한다.
`DONTWAIT`에 data가 없거나 receive timeout이 끝나면 `ZLINK_RECV_NO_DATA`다.

## 5. Raw callback

```c
zlink_handler_result_t zlink_recv_handler(
  void *stream,
  zlink_socket_msg_handler_fn handler,
  void *userdata);
```

raw callback은 STREAM에서만 지원한다. handler가 받은 source routing ID는 callback 동안만 유효한 borrowed
view다. message part 소유권은 callback 계약에 따라 handler로 이동하며 handler는 각 part를 정확히 한 번
소비하거나 닫는다. callback 안에서 같은 handler를 교체하거나 socket을 close하면 `EDEADLK`다.

## 6. Packet callback과 framing

```c
typedef void (*zlink_stream_packet_handler_fn)(
  void *stream,
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *header,
  zlink_msg_t *body,
  void *userdata);

zlink_handler_result_t zlink_stream_packet_handler(
  void *stream,
  zlink_stream_packet_handler_fn handler,
  void *userdata);
```

packet mode는 각 client byte stream에서 다음 frame을 순서대로 조립한다.

```text
+----------------+----------------+----------------+---------------+
| header_size:u16| body_size:u32  | header bytes   | body bytes    |
+----------------+----------------+----------------+---------------+
| big endian     | big endian     | exact length   | exact length  |
+----------------+----------------+----------------+---------------+
```

두 payload 크기는 0일 수 있다. callback은 이 경우에도 `NULL` 대신 길이 0인 유효한 message를 받는다.
source routing ID는 callback 동안만 유효한 borrowed view이고 header와 body 소유권은 callback으로 이동한다.

선언 길이가 구현 제한을 넘거나 연결이 incomplete packet 상태로 종료되면 partial packet을 전달하지 않는다.
해당 client 연결을 종료하고 socket monitor에 protocol failure를 기록한다.

## 7. Send readiness와 thread safety

```c
zlink_handler_result_t zlink_send_ready_handler(
  void *stream,
  zlink_send_ready_handler_fn handler,
  void *userdata);
```

send-ready는 이전 submit이 backpressure였을 때 다시 시도할 가치가 있음을 알린다. 다음 submit 성공을
보장하지 않는다. handler는 교체할 수 있지만 `NULL`로 해제하지 않는다. 같은 handler 안의 재등록은
`ZLINK_HANDLER_DEADLOCK`, `errno == EDEADLK`다.

socket configuration과 close는 application이 직렬화한다. 서로 다른 client routing ID에 대한 submit은
thread-safe다. 같은 multipart object를 동시에 사용하면 안 된다. 결과 enum과 errno의 정확한 대응은
[errno map](../errno-map.ko.md)을 따른다.
