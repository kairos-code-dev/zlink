[English](./stream-socket.md) | [한국어](./stream-socket.ko.md)

# STREAM 소켓 WS/WSS 최적화

## 1. 개요

STREAM 소켓은 ZMP(zlink Message Protocol) 핸드셰이크 없이 연결하는 외부 클라이언트
(웹 브라우저, 게임 클라이언트 등)와 RAW 통신을 지원한다. tcp, tls, ws, wss transport를
지원하며, 특히 WS/WSS 경로의 성능 최적화에 집중한다.

## 2. 아키텍처

### 2.1 컴포넌트 구성

| 컴포넌트 | 파일 | 역할 |
|----------|------|------|
| stream_t | src/runtime/sockets/stream/stream.cpp | STREAM 소켓 로직 |
| raw_encoder_t | src/runtime/protocol/raw_encoder.cpp | Length-Prefix 인코딩 |
| raw_decoder_t | src/runtime/protocol/raw_decoder.cpp | Length-Prefix 디코딩 |
| asio_raw_engine_t | src/runtime/engine/asio/asio_raw_engine.cpp | RAW I/O 엔진 |
| ws_transport_t | src/runtime/transports/ws/ | WebSocket 전송 |
| wss_transport_t | src/runtime/transports/ws/ | WebSocket + TLS |

### 2.2 데이터 흐름

```mermaid
sequenceDiagram
    participant App as Application
    participant SS as Stream Socket
    participant Eng as Engine
    participant Tr as Transport

    App->>SS: zlink_send(rid + data)
    SS->>Eng: pipe_t::write()
    Eng->>Tr: raw_encode (4B len + payload)
    Tr->>Tr: ws::write
```

## 3. WS/WSS 성능 특성

### 3.1 Read Path
- 데이터는 Beast `flat_buffer` 에서 출력 `msg_t` 로 직접 이동한다
  (중간 staging buffer 없이 단일 copy).

### 3.2 Write Path
- `msg_t` payload 를 Beast write 버퍼로 직접 전달한다 (중간 copy 없음).

### 3.3 Beast Write Buffer
- 64KB write 버퍼. 여러 소형 메시지가 하나의 Beast write 로 묶이도록
  선택한 크기다.

### 3.4 프레임 분할
- `auto_fragment(false)` — 논리 메시지 하나가 하나의 WebSocket 프레임에
  대응한다.

## 4. 측정 처리량

표준 벤치마크 머신의 단일 socket 대표 처리량:

| Transport | Throughput |
|-----------|------------|
| TCP       | 1493 MB/s  |
| WS        |  696 MB/s  |
| WSS 1KB   |  382 MB/s  |

WS 프레이밍을 택해서 얻는 이득은 대용량 메시지에서 가장 크다. 64KB 이상
payload 에서는 WS 가 TCP 라인 레이트에 근접하고, WSS 비용은 TLS 암호화
오버헤드가 좌우한다.

## 5. 설계 트레이드오프

- Speculative write 미지원 (WebSocket 프레임 기반)
- Gather write는 WS/WSS에서 지원 (Beast가 내부에서 버퍼링)
- TLS/WSS는 암호화 오버헤드 존재

## 6. Packet Handler 수신 모드

STREAM 소켓에는 서로 배타적인 수신 모드가 셋 있다. 소켓 하나당 하나만
활성화할 수 있으며, 같은 소켓에 두 번째 활성화를 시도하면 `EBUSY`로
실패한다.

| 모드 | 활성화 방식 | 전달 형태 |
|------|-------------|-----------|
| Raw recv | 기본 | `zlink_recv()`가 read 단위로 raw bytes 반환 |
| Raw callback | `zlink_recv_handler()` | `zlink_socket_msg_handler_fn` 이 raw bytes 를 받는다 |
| Packet callback | `zlink_stream_packet_handler()` | `zlink_stream_packet_handler_fn` 이 header / body 로 이미 분리된 `zlink_msg_t` 를 받는다 |

Packet handler 모드는 raw STREAM 바이트 파이프 위에 `header + body` 프레이밍을
올리는 애플리케이션 프로토콜을 위한 것이다 — 예를 들어 주문 처리 게이트웨이가
작은 제어 헤더 뒤에 큰 payload를 싣는 경우다. 호출자마다 똑같은
length-prefix(길이 접두사) 디코더와 버퍼링 상태 머신을 거듭 구현하는
대신, STREAM 이 내부에서 frame 을 파싱하고 이미 할당된
`zlink_msg_t` 를 콜백에 넘긴다.

### 6.1 Wire framing

각 논리 packet 은 wire 에 다음 형식으로 실린다:

```
+------------------+--------------------+----------------+-------------------+
| u16 header_size  | u32 body_size      | header bytes   | body bytes        |
| (big-endian)     | (big-endian)       | (header_size)  | (body_size)       |
+------------------+--------------------+----------------+-------------------+
```

- `header_size` 는 2-byte big-endian unsigned integer.
- `body_size` 는 4-byte big-endian unsigned integer.
- 두 size 는 모두 `0` 일 수 있다. `header_size=0 && body_size=0` 인 패킷도
  콜백을 그대로 유발하며, header 와 body 가 비어 있어도 non-`NULL`
  인 `zlink_msg_t` 두 개로 전달된다.
- 최대 크기는 내부 한계로 제한된다. 한계를 넘는 size 광고는 malformed
  framing 으로 취급된다 (6.4 참고).

### 6.2 Per-connection 누적기

들어오는 바이트는 `source_rid` (원격쪽 STREAM routing identity) 를 키로 삼은
per-connection decoder 를 거친다.

```
  wire bytes (arbitrary fragmentation)
         |
         v
  +-------------------------+
  | stream decoder (per rid)|
  |   state: PARSE_HEADER_SIZE
  |          PARSE_BODY_SIZE
  |          ALLOC_MSGS
  |          READ_HEADER
  |          READ_BODY
  |          DELIVER
  +-------------------------+
         |
         v
  callback(stream, source_rid, header_msg, body_msg, userdata)
```

먼저 length field 가 파싱된다. `header_size` 와 `body_size` 가 모두 확정되면
구현이 header / body 용 `zlink_msg_t` 를 미리 allocation 하고, 이후
socket read 는 바이트를 그 message 들의 backing buffer 에 곧장 흘려넣는다.
Delivery 시점에는 두 번째 copy 가 없다 — 콜백이 실행될 때 payload는
이미 넘겨받을 message 안에 들어 있다.

### 6.3 Callback 규약

Signature:

```
zlink_stream_packet_handler_fn(stream,
                               source_rid,    // borrowed view
                               header_msg,    // ownership 이전
                               body_msg,      // ownership 이전
                               userdata)
```

- `source_rid` 는 콜백 실행 동안만 유효한 빌린 참조(borrowed view)다. 콜백
  뒤에도 보존하려면 복사해야 한다.
- `header_msg` 와 `body_msg` 는 wire size 가 `0` 이어도 항상
  non-`NULL` 로 전달된다. 두 메시지의 ownership 이 콜백으로 넘어가며,
  콜백이 `zlink_msg_close()` 로 닫을 책임을 진다.
- 같은 `source_rid` 에서 오는 패킷들은 직렬화된다. 같은 피어의 뒤
  패킷이 앞 패킷을 앞지를 수 없다. 서로 다른 `source_rid` 의 패킷
  은 서로 다른 worker 스레드에서 병렬로 디스패치될 수 있다.
- 콜백 안에서의 self-close 는 raw `zlink_recv_handler` 케이스와 같은
  규칙을 따른다. 콜백 안에서 수신 모드를 바꾸거나 소켓을 닫으려
  하면 `EBUSY` 로 실패한다.

### 6.4 Malformed framing

다음 상황은 malformed 로 보고 해당 연결을 닫는다.

- 선언된 `header_size` 또는 `body_size` 가 내부 한계를 넘는 경우.
- Length field 는 도착했지만 전체 패킷이 도착하기 전에 피어가 close /
  reset 되는 경우 — 즉 mid-length 또는 mid-payload close.

이 경우 STREAM monitor 에 해당 `source_rid` 의 disconnect 이벤트로
노출된다. 불완전한 패킷은 절대 콜백으로 전달되지 않으며,
연결과 함께 decoder state 도 폐기된다.

### 6.5 왜 STREAM 안에서 decode 하는가

애플리케이션마다 따로 하는 대신 STREAM 안에서 decode 하도록 둔 이유는 두 가지다.

- **복사 한 번 감소.** 애플리케이션이 "조립된" contiguous buffer 를 한 번
  만졌다가 다시 쪼갤 필요가 없다. header / body message 가 곧 소켓 read 의
  목적 버퍼다.
- **순서 보장.** Per-`source_rid` 직렬화를 decoder 쪽에서 강제하므로,
  호출자가 raw byte delivery 위에 별도 reorder 로직을 올릴 필요가 없다.

## 7. 현재 STREAM 런타임 기본값

STREAM 은 transport 전반에 공통된 기본 성능 프로파일을 쓴다.
STREAM 외 공통 소켓 기본값은
[socket-option-defaults.ko.md](./socket-option-defaults.ko.md)를 참고한다.

### 7.1 내부 상수 고정 항목

아래 값들은 내부 상수로 고정되며 STREAM env 로 제어하지 않는다:
- handler allocator: 활성
- read drain: 활성
- speculative write: STREAM/TCP 경로에서 상시 on 고정
- RX slab buffering: 활성
- speculative write byte budget: `2097152`
- read drain max loops: `64`
- read drain max bytes: `1048576`

### 7.2 소켓/리스너 기본값

- backlog: `65536`
- `sndhwm` / `rcvhwm`: routed 역할 auto HWM floor에서 시작
- `sndbuf` / `rcvbuf`: auto HWM transport budget 계산값 사용
- auto HWM 비활성 + `sndbuf` / `rcvbuf` 미설정 시 호환 기본값 `262144`
- accept 동시성(STREAM 전용): 기본 `4`, 최대 `128`
- 세션 스케줄러(STREAM): 기본 `rr`

### 7.3 현재 유지되는 STREAM 런타임 환경변수

- `ZLINK_ASIO_STREAM_ACCEPT_CONCURRENCY`: 기본 `4`, 최대 `128`로 제한
- `ZLINK_ASIO_STREAM_SESSION_SCHED` (`rr|minload`): 기본 `rr`
- `ZLINK_ASIO_STREAM_ENABLE_NON_TCP_SPEC_READ`: 기본 비활성
- `ZLINK_ASIO_STREAM_DISABLE_GATHER`: 기본 비활성이라 STREAM gather-write 는 유지됨
- `ZLINK_ASIO_STREAM_GATHER_THRESHOLD`: 기본 `1024`
- `ZLINK_ASIO_STREAM_TINY_GATHER_THRESHOLD`: 기본 `0`
- `ZLINK_ASIO_STREAM_INITIAL_TARGET_CAP`: 기본 `4096`
- `ZLINK_ASIO_STREAM_BATCH_SIZE`: 기본 `4096`
- `ZLINK_ASIO_STREAM_BATCH_HEADROOM`: 기본 `64`

## 8. Peer rid disconnect

STREAM의 public routing id는 서버가 연결별로 부여한 4바이트 connection id다.
`zlink_disconnect_rid()`는 이 id를 `uint32_t`로 해석해 STREAM 라우팅 맵에서
pipe를 찾고 종료 요청을 넣는다. 4바이트가 아닌 rid는 잘못된 인자로 실패한다.

## 9. Session Actor relay (ActorGateway attach)

STREAM socket은 client session 메시지를 SpotNode Actor로 relay할 수 있다. 각 client
연결의 `source_rid`가 STREAM session이 되고, `zlink_stream_bind_actor()`로 Actor 하나
이상에 bind될 수 있다. bind를 실행하려면 그 전에 STREAM handle이 자신의 session을
소유하는 SpotNode를 알고 있어야 한다. 이것이 ActorGateway attach다.

```c
zlink_config_result_t zlink_stream_attach_actor_gateway(void *stream,
                                                        void *node);
```

STREAM handle이 session owner SpotNode를 얻는 경로는 두 가지다.

- **명시적 attach.** `zlink_stream_attach_actor_gateway(stream, node)`는 stream을
  routed-capable `node`가 소유하도록 기록한다. raw STREAM socket과 connector 기반
  stream은 library가 handle에서 SpotNode로 이어지는 구조적 연결이 없으므로 이 경로가 필수다.
  attach는 one-way이고 sticky하다. 다른 node로 다시 붙이려 하면 거부하고(`EBUSY` /
  `ZLINK_CONFIG_INVALID_STATE`), 같은 stream/node 쌍이면 멱등으로 받아들이며, stream close
  또는 node destroy 때만 해제된다. routed가 아닌 node는 `ENOTSUP` /
  `ZLINK_CONFIG_NOT_SUPPORTED`로 거부된다.
- **구조적 추론.** STREAM socket 자체가 SpotNode 소유 socket(node 내부 socket)이면
  owner를 socket registry에서 복원하므로 명시적 attach가 필요 없다.

STREAM socket은 relay 상태를 직접 보관하지 않는다. owner 매핑, session-to-Actor binding,
relay 경로는 모두 SpotNode Actor runtime에 있다. 배선과 local/remote relay 경로, cleanup
규칙은 [spot-internals.ko.md](./spot-internals.ko.md) 12절("STREAM session과 Actor
binding")에 정리되어 있다. STREAM 계층에서 중요한 것은 `source_rid`별 byte pipe가 relay가
타는 transport라는 점, 그리고 session disconnect가 bound Actor의 joined Spot은 바꾸지 않고 그
session의 binding만 제거한다는 점뿐이다.
