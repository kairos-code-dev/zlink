# zlink STREAM 소켓 상세 설계 (2026-01-22)

## 0. 범위와 목표
- 목적: Raw TCP/TLS/WebSocket 위에서 4-byte length prefix 기반의
  STREAM 소켓을 제공한다.
- 애플리케이션 메시지 형식: 2-프레임 `[routing_id][payload]`.
- 구현 기반: ASIO(Transport/Proactor 엔진) + Beast(WebSocket).
- 비범위: ZMP 핸드셰이크/메타데이터 교환(사용하지 않음).

---

## 1. 프로토콜 명세 (zlink STREAM Protocol Specification)

### 1.1 용어
- **Payload**: 애플리케이션 데이터 바이트.
- **Routing ID**: 연결을 식별하는 바이너리 ID(서버 내부 생성 또는
  클라이언트 지정).
- **Wire message**: 네트워크로 전송되는 length-prefix 메시지.

### 1.2 와이어 포맷 (4-byte length + payload)
모든 전송 매체(TCP/TLS/WS/WSS)에서 동일한 바이트 스트림 포맷을 사용한다.

```
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-------------------------------+-------------------------------+
|            Length (u32, BE)   |    Payload bytes ...         |
+-------------------------------+-------------------------------+
```

- **Length**: Big Endian uint32, payload 길이.
- **Payload**: `Length` 만큼의 raw bytes.
- **Zero-length**: 연결 이벤트(connect/disconnect) 또는 앱 종료 신호로
  사용 가능.
- **Maximum**: `Length`는 `ZMQ_MAXMSGSIZE`(옵션) 초과 시 오류 처리.

**WebSocket**:
- WS/WSS는 frame 기반이지만, 내부적으로는 바이트 스트림처럼 처리한다.
- 권장: 하나의 WS Binary frame에 `Length+Payload`를 담아 전송.
- 수신 측은 WS 프레임 경계와 무관하게 length prefix를 기준으로 재조립.

### 1.3 메시지 구조 (ZMQ 메시지 레벨)
애플리케이션은 항상 2-프레임 메시지를 사용한다.

```
Frame 0: [routing_id]  (MORE=1)
Frame 1: [payload]     (MORE=0)
```

- `routing_id`는 **4바이트 uint32** (Big Endian, Network Byte Order)
  - 기존 ZMQ의 5바이트(0x00 + uint32) 방식 대신 순수 4바이트 사용
  - 애플리케이션에서 직접 `uint32_t`로 캐스팅 가능
  - 자동 발급: 1부터 시작하는 순차 증가 정수
- `payload`는 길이 제한 내의 바이너리.

### 1.4 연결/해제 이벤트 (명시적 구분)
STREAM 소켓은 연결 이벤트를 **1바이트 이벤트 코드**로 명시적 구분한다.

**이벤트 코드**:
```
0x01 = CONNECT (연결 수립)
0x00 = DISCONNECT (연결 종료)
```

**메시지 형식**:
```
Connect:    [routing_id (4B)][0x01]     - 1바이트 payload
Disconnect: [routing_id (4B)][0x00]     - 1바이트 payload
Data:       [routing_id (4B)][payload]  - N바이트 payload (N≥1)
```

**주의**: 1바이트 payload가 `0x00` 또는 `0x01`이면 이벤트로 해석됨.
실제 데이터로 0x00/0x01 단일 바이트를 전송하려면 패딩 필요 (예: `[0x00, 0x00]`).

1) **연결 수립**
- 새 연결 수립 시, 소켓은 `[routing_id][0x01]` 메시지를 전달한다.
- 모니터 이벤트도 함께 발생: `ZMQ_EVENT_ACCEPTED` (bind), `ZMQ_EVENT_CONNECTED` (connect)

2) **연결 종료 (원격)**
- 원격이 연결을 종료하면 `[routing_id][0x00]` 메시지가 도착한다.
- 모니터 이벤트: `ZMQ_EVENT_DISCONNECTED`

3) **연결 종료 (로컬)**
- 앱이 `[routing_id][0x00]`를 전송하면 해당 연결을 정상 종료한다.
- 종료 후 추가 송신은 `EHOSTUNREACH` 또는 `EPIPE` 처리.

### 1.5 Routing ID 설계 근거
**기존 ZMQ 5바이트 방식의 문제점**:
- 첫 1바이트(0x00)는 자동 생성 ID 마커로만 사용
- 애플리케이션에서 ID 저장/비교 시 매번 첫 바이트 제거 필요
- 메모리 정렬 불편 (5바이트는 어색한 크기)

**zlink 4바이트 방식의 장점**:
- 직접 `uint32_t`로 캐스팅 가능
- 4바이트 정렬로 메모리 효율적
- 최대 4십억 개 동시 연결 지원 (실용적으로 충분)

```c
// 애플리케이션 예시
uint32_t client_id;
zmq_recv(sock, &client_id, 4, 0);
client_id = ntohl(client_id);  // Network → Host byte order

// 응답 시
client_id = htonl(client_id);  // Host → Network byte order
zmq_send(sock, &client_id, 4, ZMQ_SNDMORE);
zmq_send(sock, data, data_len, 0);
```

### 1.5 오류/예외 처리
- `Length`가 비정상(> `ZMQ_MAXMSGSIZE`)이면 해당 연결을 종료한다.
- 길이 프레임/바디가 불완전할 경우, 데이터가 누적될 때까지 대기한다.
- TLS/WS 핸드셰이크 실패 시 소켓은 연결 실패 이벤트를 발생시킨다.

---

## 2. 소켓 API 명세

### 2.1 생성/바인드/연결
```cpp
void *ctx = zmq_ctx_new();

// server (bind)
void *s = zmq_socket(ctx, ZMQ_STREAM);   // 신규 소켓 타입
zmq_bind(s, "tcp://*:9000");
// zmq_bind(s, "tls://*:9443");
// zmq_bind(s, "ws://*:8080/stream");
// zmq_bind(s, "wss://*:8443/stream");

// client (connect)
void *c = zmq_socket(ctx, ZMQ_STREAM);
zmq_connect(c, "tcp://127.0.0.1:9000");
// zmq_connect(c, "tls://127.0.0.1:9443");
// zmq_connect(c, "ws://127.0.0.1:8080/stream");
// zmq_connect(c, "wss://127.0.0.1:8443/stream");
```

- `ZMQ_STREAM`는 새 소켓 타입으로 정의한다 (예: `#define ZMQ_STREAM 11`).
- 내부적으로 `options.raw_socket = true`를 설정하여 ZMP 핸드셰이크를
  비활성화한다.

### 2.2 송신/수신
**전송**
```cpp
// 1) routing_id 전송 (MORE)
zmq_send(s, routing_id, routing_id_len, ZMQ_SNDMORE);

// 2) payload 전송 (0 length는 종료 신호)
zmq_send(s, payload, payload_len, 0);
```

**수신**
```cpp
unsigned char routing_id[256];
int rid_size = zmq_recv(s, routing_id, sizeof(routing_id), 0);
int more = 0;
size_t more_size = sizeof(more);
zmq_getsockopt(s, ZMQ_RCVMORE, &more, &more_size); // must be 1

unsigned char payload[4096];
int payload_size = zmq_recv(s, payload, sizeof(payload), 0);
```

### 2.3 소켓 옵션
STREAM 소켓은 ROUTER와 유사한 라우팅 옵션 및 전송 옵션을 지원한다.

**라우팅/ID**
- `ZMQ_ROUTING_ID`: 로컬 소켓의 라우팅 ID 지정.
- `ZMQ_CONNECT_ROUTING_ID`: connect 시 사용할 라우팅 ID 지정.

**전송/버퍼**
- `ZMQ_LINGER`, `ZMQ_SNDTIMEO`, `ZMQ_RCVTIMEO`
- `ZMQ_SNDHWM`, `ZMQ_RCVHWM`
- `ZMQ_MAXMSGSIZE` (Length 상한)
- `ZMQ_RECONNECT_IVL`, `ZMQ_RECONNECT_IVL_MAX`, `ZMQ_IMMEDIATE`

**TCP**
- `ZMQ_TCP_KEEPALIVE`, `ZMQ_TCP_KEEPALIVE_CNT`,
  `ZMQ_TCP_KEEPALIVE_IDLE`, `ZMQ_TCP_KEEPALIVE_INTVL`
- `ZMQ_TCP_MAXRT`, `ZMQ_TOS`, `ZMQ_BINDTODEVICE`

**TLS**
- `ZMQ_TLS_CERT`, `ZMQ_TLS_KEY`, `ZMQ_TLS_CA`
- `ZMQ_TLS_VERIFY`, `ZMQ_TLS_REQUIRE_CLIENT_CERT`
- `ZMQ_TLS_HOSTNAME`, `ZMQ_TLS_TRUST_SYSTEM`, `ZMQ_TLS_PASSWORD`

**WebSocket**
- 엔드포인트 URL의 path 사용 (예: `ws://host:port/path`)
- 별도 옵션은 없음 (Beast 핸드셰이크 사용)

---

## 3. 클라이언트 구현 가이드 (언어별 예시)

### 3.1 공통 규칙
- 송신: `len(u32, BE) + payload`
- 수신: 4바이트 length 읽고, 그 길이만큼 payload 읽기
- `payload == 0`이면 연결 이벤트 또는 종료 이벤트로 처리

### 3.2 Python (TCP/TLS)
```python
import socket, ssl, struct

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError()
        buf += chunk
    return buf

sock = socket.create_connection(("127.0.0.1", 9000))
# TLS 사용 시:
# sock = ssl.wrap_socket(sock)

def send_msg(payload: bytes):
    sock.sendall(struct.pack(">I", len(payload)) + payload)

def recv_msg() -> bytes:
    n = struct.unpack(">I", recv_exact(sock, 4))[0]
    return recv_exact(sock, n)
```

### 3.3 JavaScript (WebSocket)
```javascript
const ws = new WebSocket("ws://127.0.0.1:8080/stream");
ws.binaryType = "arraybuffer";

function sendMsg(payload) {
  const body = new Uint8Array(payload);
  const len = body.byteLength;
  const buf = new Uint8Array(4 + len);
  buf[0] = (len >>> 24) & 0xff;
  buf[1] = (len >>> 16) & 0xff;
  buf[2] = (len >>> 8) & 0xff;
  buf[3] = len & 0xff;
  buf.set(body, 4);
  ws.send(buf); // Binary frame
}

ws.onmessage = (ev) => {
  const data = new Uint8Array(ev.data);
  const len = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
  const payload = data.slice(4, 4 + len);
};
```

### 3.4 C# (TCP/TLS)
```csharp
using System;
using System.Buffers.Binary;
using System.IO;
using System.Net.Sockets;

static byte[] RecvExact(Stream s, int n) {
    byte[] buf = new byte[n];
    int off = 0;
    while (off < n) {
        int r = s.Read(buf, off, n - off);
        if (r <= 0) throw new IOException("EOF");
        off += r;
    }
    return buf;
}

var client = new TcpClient("127.0.0.1", 9000);
Stream stream = client.GetStream();
// TLS 사용 시: stream = new SslStream(stream, false, ...);

void SendMsg(byte[] payload) {
    Span<byte> header = stackalloc byte[4];
    BinaryPrimitives.WriteUInt32BigEndian(header, (uint)payload.Length);
    stream.Write(header);
    stream.Write(payload, 0, payload.Length);
}

byte[] RecvMsg() {
    var header = RecvExact(stream, 4);
    uint len = BinaryPrimitives.ReadUInt32BigEndian(header);
    return RecvExact(stream, (int)len);
}
```

### 3.5 Java (TCP/TLS)
```java
import java.io.*;
import java.net.Socket;

Socket sock = new Socket("127.0.0.1", 9000);
DataOutputStream out = new DataOutputStream(sock.getOutputStream());
DataInputStream in = new DataInputStream(sock.getInputStream());

void sendMsg(byte[] payload) throws IOException {
    out.writeInt(payload.length); // Big Endian
    out.write(payload);
}

byte[] recvMsg() throws IOException {
    int len = in.readInt();
    byte[] buf = in.readNBytes(len);
    return buf;
}
```

### 3.6 Go (TCP/TLS)
```go
conn, _ := net.Dial("tcp", "127.0.0.1:9000")
// TLS 사용 시: tls.Dial(...)

func sendMsg(payload []byte) error {
    if err := binary.Write(conn, binary.BigEndian, uint32(len(payload))); err != nil {
        return err
    }
    _, err := conn.Write(payload)
    return err
}

func recvMsg() ([]byte, error) {
    var n uint32
    if err := binary.Read(conn, binary.BigEndian, &n); err != nil {
        return nil, err
    }
    buf := make([]byte, n)
    _, err := io.ReadFull(conn, buf)
    return buf, err
}
```

---

## 4. 서버 구현 가이드 (zlink STREAM)

### 4.1 기본 흐름
1) STREAM 소켓 생성 및 bind
2) `[routing_id (4B)][payload]` 수신
3) payload 1바이트 검사: `0x01`=connect, `0x00`=disconnect, 그 외=data
4) 응답 시 동일한 `routing_id`로 2-프레임 송신

### 4.2 C API 예시
```cpp
void *ctx = zmq_ctx_new();
void *s = zmq_socket(ctx, ZMQ_STREAM);
zmq_bind(s, "tcp://*:9000");

while (1) {
    // 1) routing_id 수신 (4바이트)
    uint32_t client_id_be;
    zmq_recv(s, &client_id_be, 4, 0);
    uint32_t client_id = ntohl(client_id_be);

    // 2) payload 수신
    unsigned char payload[4096];
    int payload_len = zmq_recv(s, payload, sizeof(payload), 0);

    // 3) 이벤트 vs 데이터 구분
    if (payload_len == 1) {
        if (payload[0] == 0x01) {
            printf("Client %u connected\n", client_id);
            continue;
        } else if (payload[0] == 0x00) {
            printf("Client %u disconnected\n", client_id);
            continue;
        }
        // 그 외 1바이트는 데이터로 처리
    }

    // 4) Echo 응답
    client_id_be = htonl(client_id);
    zmq_send(s, &client_id_be, 4, ZMQ_SNDMORE);
    zmq_send(s, payload, payload_len, 0);
}
```

### 4.3 모니터 이벤트 활용
connect/disconnect 이벤트를 명확히 구분하려면 모니터를 사용한다.
```cpp
// 예시: ZMQ_EVENT_ACCEPTED, ZMQ_EVENT_CONNECTED, ZMQ_EVENT_DISCONNECTED
zmq_socket_monitor(s, "inproc://stream-mon", ZMQ_EVENT_ALL);
```

---

## 5. 아키텍처 상세 (클래스/시퀀스 다이어그램)

### 5.1 클래스 다이어그램 (ASCII)
```text
socket_base_t
  └─ routing_socket_base_t
       ├─ router_t (existing)
       └─ stream_t (new)

session_base_t
  └─ uses i_engine
       ├─ asio_engine_t (base)
       │    └─ asio_raw_engine_t (new, TCP/TLS)
       └─ asio_ws_engine_t (existing)
            └─ raw mode or asio_ws_raw_engine_t (new)

i_asio_transport
  ├─ tcp_transport (existing)
  ├─ tls_transport (existing)
  └─ ws_transport (Beast, existing)
```

### 5.2 수신 시퀀스 (TCP/TLS/WS 공통)
```text
Remote Client
    |
    |  Length(4) + Payload
    v
Transport (tcp/tls/ws)
    |
    v
asio_raw_engine_t
  - length prefix decode
  - msg_t(payload) 생성
    |
    v
session_base_t
    |
    v
stream_t::xrecv()
  - routing_id 프레임 합성
  - payload 프레임 반환
    |
    v
Application
```

### 5.3 송신 시퀀스
```text
Application
  send [routing_id][payload]
    |
    v
stream_t::xsend()
  - routing_id로 pipe 선택
  - payload 전달
    |
    v
session_base_t
    |
    v
asio_raw_engine_t
  - length prefix encode
  - gather write (header + payload)
    |
    v
Transport (tcp/tls/ws) -> Remote Client
```

### 5.4 연결/해제 이벤트 흐름
```text
listener accept
    |
    v
pipe 생성 + routing_id 할당
    |
    v
stream_t -> [routing_id][empty] deliver
    |
    v
Application (connect 이벤트 처리)

disconnect 발생
    |
    v
pipe 종료
    |
    v
stream_t -> [routing_id][empty] deliver
    |
    v
Application (disconnect 이벤트 처리)
```

### 5.5 구현 매핑 (참고 파일)
- `src/sockets/stream.hpp/.cpp`:
  - `routing_socket_base_t` 패턴 준수
  - `[routing_id][payload]` 프레임 처리
- `src/engine/asio/asio_raw_engine.hpp/.cpp`:
  - ZMP handshake 없음, length prefix decode/encode
- `src/transports/tcp|tls|ws/*`:
  - raw 모드 시 `asio_raw_engine_t` 또는 ws raw 모드 엔진 생성
- `src/sockets/router.hpp/.cpp`:
  - routing_id 생성/관리 패턴 참고
- `src/engine/asio/asio_zmp_engine.hpp`:
  - ASIO 엔진 구조 및 handshake 흐름 참고

---

## 6. 구현 체크리스트

### 6.1 헤더/상수 정의
- [ ] `include/zmq.h`: `#define ZMQ_STREAM 11` 소켓 타입 상수 추가
- [ ] `src/core/options.hpp`: `raw_socket` bool 옵션 추가

### 6.2 소켓 구현
- [ ] `src/sockets/stream.hpp`: stream_t 클래스 헤더
- [ ] `src/sockets/stream.cpp`: stream_t 클래스 구현
  - routing_socket_base_t 상속
  - xsend(): routing_id로 대상 파이프 선택 후 전송
  - xrecv(): [routing_id][payload] 2-프레임 반환
  - identify_peer(): 5바이트 자동 ID 생성

### 6.3 팩토리 수정
- [ ] `src/sockets/socket_base.cpp`: create()에 ZMQ_STREAM 케이스 추가
- [ ] `src/core/session_base.cpp`: create()에 ZMQ_STREAM 케이스 추가

### 6.4 Raw 엔진 구현
- [ ] `src/engine/asio/asio_raw_engine.hpp`: Raw 엔진 헤더
- [ ] `src/engine/asio/asio_raw_engine.cpp`: Raw 엔진 구현
  - asio_zmp_engine.cpp 기반, ZMP 핸드셰이크 로직 제거
  - 4-byte Big Endian length prefix encode/decode
  - ZMQ_MAXMSGSIZE 초과 시 연결 종료

### 6.5 트랜스포트 연동
- [ ] `src/transports/tcp/asio_tcp_connecter.cpp`: raw_socket 시 asio_raw_engine_t 생성
- [ ] `src/transports/tcp/asio_tcp_listener.cpp`: raw_socket 시 asio_raw_engine_t 생성
- [ ] `src/transports/tls/asio_tls_connecter.cpp`: raw_socket 지원
- [ ] `src/transports/tls/asio_tls_listener.cpp`: raw_socket 지원
- [ ] `src/transports/ws/asio_ws_connecter.cpp`: raw_socket 지원
- [ ] `src/transports/ws/asio_ws_listener.cpp`: raw_socket 지원

### 6.6 빌드 시스템
- [ ] `CMakeLists.txt`: 신규 소스 파일 등록
  - `src/sockets/stream.cpp`
  - `src/engine/asio/asio_raw_engine.cpp`

### 6.7 테스트
- [ ] `tests/test_stream_socket.cpp`: STREAM 소켓 테스트
  - Basic Echo (TCP): bind/connect, 2-프레임 송수신
  - Connect/Disconnect Event: 0-length payload 이벤트 확인
  - Multiple Clients: 여러 클라이언트 동시 연결
  - Max Msg Size Breach: 큰 메시지 오류 처리
  - TLS STREAM: tls:// 트랜스포트 테스트
  - WebSocket STREAM: ws:// 트랜스포트 테스트
- [ ] `tests/CMakeLists.txt`: 테스트 파일 등록

---

## 7. 구현 참고 팁

### 7.1 asio_raw_engine_t 구현 전략
기존 `src/engine/asio/asio_zmp_engine.cpp`를 복사하여:
1. ZMP 핸드셰이크 관련 상태머신/코드 제거
2. Length prefix (4-byte BE) 읽기/쓰기 로직만 유지
3. 수신: 4바이트 읽고 → N바이트 페이로드 읽기 → msg_t 생성
4. 송신: msg_t → 4바이트 헤더 + 페이로드 gather write

### 7.2 stream_t 구현 패턴
`src/sockets/router.cpp`의 패턴 참고:
- `_fq` (fair queue)로 인바운드 파이프 관리
- `_outpipes` 맵으로 routing_id → pipe 매핑
- `identify_peer()`로 자동 ID 생성: **4바이트 uint32** (1부터 순차 증가)
- 기존 ZMQ의 5바이트(0x00+uint32) 방식 사용 안 함

### 7.3 이벤트 주입 구현
```cpp
// stream_t::xattach_pipe() - Connect 이벤트 주입
void stream_t::xattach_pipe(pipe_t *pipe_, bool locally_initiated_) {
    // 1) routing_id 할당 (4바이트)
    uint32_t id = _next_routing_id++;
    blob_t routing_id(reinterpret_cast<unsigned char*>(&id), 4);

    // 2) 파이프 등록
    add_out_pipe(routing_id, pipe_);
    _fq.attach(pipe_);

    // 3) Connect 이벤트 주입 [routing_id][0x01]
    msg_t id_msg, event_msg;
    id_msg.init_size(4);
    put_uint32(id_msg.data(), id);
    id_msg.set_flags(msg_t::more);

    event_msg.init_size(1);
    *static_cast<unsigned char*>(event_msg.data()) = 0x01;

    // 내부 이벤트 큐에 추가
    _pending_events.push({std::move(id_msg), std::move(event_msg)});
}

// stream_t::xpipe_terminated() - Disconnect 이벤트 주입
void stream_t::xpipe_terminated(pipe_t *pipe_) {
    blob_t routing_id = get_routing_id(pipe_);

    // Disconnect 이벤트 주입 [routing_id][0x00]
    msg_t id_msg, event_msg;
    id_msg.init_buffer(routing_id.data(), 4);
    id_msg.set_flags(msg_t::more);

    event_msg.init_size(1);
    *static_cast<unsigned char*>(event_msg.data()) = 0x00;

    _pending_events.push({std::move(id_msg), std::move(event_msg)});

    // 파이프 정리
    erase_out_pipe(pipe_);
    _fq.terminated(pipe_);
}
```

---

## 8. Codex 검증 결과 (2026-01-22)

### 8.1 발견된 이슈 (심각도순)

#### [높음] raw_socket 옵션 복구 필요
- `src/core/session_base.cpp:184`에 "raw_socket has been removed" 주석 존재
- 단순 옵션 추가로는 불충분, 옵션 플로우/엔진 선택 로직 전체 검토 필요
- **해결**: `options.raw_socket` 재도입 + `options.cpp` setsockopt 핸들러 추가
  + 모든 connecter/listener에서 raw_socket 분기 처리

#### [높음] Raw 엔진용 인코더/디코더 설계 누락
- `asio_engine_t`는 `_decoder/_encoder` 인터페이스 전제
- 4-byte length-prefix 처리를 위한 별도 클래스 필요
- **해결**:
  - `src/protocol/raw_decoder.hpp/cpp` 추가
  - `src/protocol/raw_encoder.hpp/cpp` 추가
  - 또는 `asio_raw_engine_t` 내부에서 직접 처리 (간단한 프로토콜이므로)

#### [높음] WebSocket Raw 모드 설계 부재
- 현재 `asio_ws_engine_t`가 WS handshake + ZMP 핸드셰이크 모두 처리
- `asio_raw_engine_t`만으로는 WS 동작 불가
- **해결 옵션**:
  1. `asio_ws_engine_t`에 raw 모드 플래그 추가 → ZMP 핸드셰이크 스킵
  2. `asio_ws_raw_engine_t` 신규 생성 → WS 핸드셰이크만 수행

#### [높음] ~~Connect/Disconnect 이벤트 주입 메커니즘 부재~~ ✅ 해결됨
- ~~`disconnect_msg`는 길이>0만 전송~~
- **해결 (반영됨)**:
  - Connect: `[routing_id][0x01]` (1바이트 이벤트 코드)
  - Disconnect: `[routing_id][0x00]` (1바이트 이벤트 코드)
  - `stream_t::xattach_pipe()`와 `xpipe_terminated()`에서 이벤트 큐에 주입
  - 섹션 7.3 참조

### 8.2 중간 우선순위 이슈

#### [중간] 지원 트랜스포트 범위 불명확
- IPC/inproc에 대한 STREAM 지원 여부 미정의
- **해결**:
  - IPC: ZMP 핸드셰이크 없는 raw 모드로 지원 가능
  - inproc: 불필요 (내부 통신용)
  - 문서에 명시: "tcp, tls, ws, wss, ipc 지원. inproc 미지원"

#### [중간] Zero-length payload 제약사항
- 빈 페이로드가 이벤트 용도로 예약됨
- 실제 "빈 데이터" 전송 불가능
- **해결**: 문서에 명시 + 필요시 최소 1바이트 패딩 권장

#### [중간] 테스트 조건부 스킵 누락
- `ZMQ_HAVE_TLS`, `ZMQ_HAVE_WS` 빌드 플래그 기반 테스트 가드 필요
- **해결**: 기존 `is_transport_available()` 패턴 활용

### 8.3 보완된 체크리스트

추가 항목:
- [ ] `src/core/options.hpp/cpp`: `raw_socket` 옵션 재도입
- [ ] `src/protocol/raw_decoder.hpp/cpp`: 4-byte length prefix 디코더
- [ ] `src/protocol/raw_encoder.hpp/cpp`: 4-byte length prefix 인코더
- [ ] `src/transports/ws/asio_ws_engine.hpp/cpp`: raw 모드 플래그 또는 별도 엔진
- [ ] `stream_t::xattach_pipe()`: connect 이벤트 (빈 메시지) 주입
- [ ] `stream_t::xpipe_terminated()`: disconnect 이벤트 주입
- [ ] 문서: IPC 지원, inproc 미지원 명시
- [ ] 문서: zero-length = 이벤트 전용 제약사항 명시
- [ ] 테스트: `ZMQ_HAVE_TLS`, `ZMQ_HAVE_WS` 조건부 스킵

### 8.4 사용자 결정사항 반영 (2026-01-22)

**결정 1: Routing ID 4바이트**
- 기존 ZMQ 5바이트(0x00 + uint32) 방식 폐기
- 순수 4바이트 uint32 사용 (Big Endian)
- 섹션 1.3, 1.5 반영 완료

**결정 2: Connect/Disconnect 명시적 구분**
- Connect: `[routing_id][0x01]`
- Disconnect: `[routing_id][0x00]`
- 섹션 1.4, 4.2, 7.3 반영 완료

**결정 3: 기존 소켓 변경은 별도 작업**
- ROUTER 등 기존 소켓의 이벤트 구분 개선은 STREAM 완료 후 별도 진행

### 8.5 결론

**실행 가능성**: 핵심 설계 결정 완료. 구현 단계 진행 가능.

**남은 구현 이슈**:
1. ✅ ~~connect/disconnect 이벤트 주입 메커니즘~~ (섹션 7.3에 구현 가이드 추가됨)
2. raw_socket 옵션 복구 및 엔진 선택 로직
3. raw 인코더/디코더 또는 엔진 내부 처리 결정
4. WS raw 모드 설계 (기존 엔진 수정 vs 신규 엔진)
