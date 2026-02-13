# ZLINK STREAM 소켓 상세 스펙

- 작성일: 2026-02-13
- 기준 소스: `core/src/sockets/stream.cpp`, `core/include/zlink.h`
- 목적: STREAM 소켓을 처음부터 구현할 때 필요한 모든 사항을 기술

---

## 1. 개요

### 1.1 정의

STREAM 소켓은 **외부 클라이언트**(웹 브라우저, 게임 클라이언트, 네이티브 TCP 클라이언트)와
**RAW 통신**하기 위한 소켓 타입이다. ZMP(zlink messaging protocol) 핸드셰이크 없이
즉시 데이터를 교환한다.

### 1.2 핵심 특성

| 항목 | 값 |
|------|-----|
| 소켓 타입 ID | `ZLINK_STREAM` = `11` |
| 프로토콜 | RAW (4-byte length-prefix) |
| ZMP 핸드셰이크 | 없음 |
| 라우팅 ID 크기 | 고정 4바이트 (`uint32`) |
| 지원 트랜스포트 | `tcp`, `tls`, `ws`, `wss` |
| 유효 조합 | STREAM ↔ STREAM, STREAM ↔ 외부 클라이언트 |

### 1.3 설계 원칙

- 외부 클라이언트가 zlink 프로토콜을 몰라도 통신 가능
- 각 피어를 4-byte 정수로 식별하여 멀티 클라이언트 관리
- 연결/해제 이벤트를 데이터 스트림에 인라인으로 전달
- 모든 메시지는 반드시 2-프레임 멀티파트 구조

### 1.4 통신 구조

```
┌──────────────┐     RAW (4B Length-Prefix)     ┌──────────┐
│ 외부 Client   │◄────────────────────────────►│  STREAM  │
└──────────────┘                                └──────────┘

┌──────────────┐     RAW (4B Length-Prefix)     ┌──────────┐
│   STREAM     │◄────────────────────────────►│  STREAM  │
│  (client)    │                                │ (server) │
└──────────────┘                                └──────────┘
```

### 1.5 비호환 대상

STREAM 소켓은 zlink 내부 소켓(PAIR, PUB, SUB, DEALER, ROUTER 등)과 연결할 수 없다.
프로토콜이 다르기 때문이다.

---

## 2. 와이어 프로토콜

### 2.1 프레임 형식

네트워크 상에서 교환되는 각 메시지는 다음과 같은 length-prefix 프레임 형식을 따른다.

```
┌─────────────────────┬──────────────────────┐
│  길이 (4-byte BE)   │  페이로드 (가변)      │
├─────────────────────┼──────────────────────┤
│  uint32 big-endian  │  size 바이트          │
└─────────────────────┴──────────────────────┘
```

- **길이 필드**: 4바이트 big-endian unsigned integer
- **페이로드**: 길이 필드에 명시된 바이트 수만큼의 데이터
- **바이트 순서**: 항상 네트워크 바이트 오더 (big-endian)

### 2.2 인코딩 (송신 경로)

구현 파일: `core/src/protocol/raw_encoder.cpp`

```
1. header_ready(): msg_t.size()를 4-byte BE로 변환 → _tmp_buf에 저장
2. body_ready(): msg_t.data()를 그대로 전송
3. 1→2 반복
```

### 2.3 디코딩 (수신 경로)

구현 파일: `core/src/protocol/raw_decoder.cpp`

```
1. header_ready(): 4-byte BE 읽기 → msg_size 산출
2. size_ready(): msg_size vs MAXMSGSIZE 검증
   - 초과 시 errno=EMSGSIZE, return -1 → 연결 종료
3. body_ready(): msg_size 바이트 읽기 → msg_t에 저장
4. 1로 복귀 (다음 메시지)
```

### 2.4 바이트 오더 헬퍼

구현 파일: `core/src/protocol/wire.hpp`

```cpp
inline void put_uint32(unsigned char *buffer_, uint32_t value_);
inline uint32_t get_uint32(const unsigned char *buffer_);
```

- `put_uint32`: 호스트 → 빅엔디안 변환 후 4바이트 기록
- `get_uint32`: 빅엔디안 4바이트 → 호스트 순서 변환

### 2.5 최대 메시지 크기

- `MAXMSGSIZE`가 설정된 경우 디코더에서 검증
- 유효 최대값 = `min(MAXMSGSIZE, 0xFFFFFFFF)`
- 초과 시 연결 즉시 종료 (disconnect 이벤트 발생)

---

## 3. 애플리케이션 메시지 형식

### 3.1 2-프레임 멀티파트 구조

STREAM 소켓의 모든 메시지는 반드시 **2-프레임 멀티파트**로 구성된다.

```
┌─────────────────────────────┐
│ Frame 0: routing_id (4B)    │  ← ZLINK_SNDMORE / ZLINK_RCVMORE 설정됨
├─────────────────────────────┤
│ Frame 1: payload (가변)     │  ← MORE 플래그 없음
└─────────────────────────────┘
```

| 프레임 | 크기 | 내용 | MORE 플래그 |
|--------|------|------|-------------|
| Frame 0 | 정확히 4바이트 | 피어의 `routing_id` (uint32, BE) | 있음 |
| Frame 1 | 가변 | 애플리케이션 데이터 또는 이벤트 코드 | 없음 |

### 3.2 특수 이벤트

| 페이로드 (Frame 1) | 의미 |
|---------------------|------|
| 1바이트 `0x01` | **연결 이벤트**: 새 피어가 연결됨 |
| 1바이트 `0x00` | **해제 이벤트**: 피어가 연결 해제됨 |
| N바이트 데이터 | 일반 데이터 메시지 |

### 3.3 수신 코드 패턴

```c
unsigned char routing_id[4];
unsigned char buf[4096];

// Frame 0: routing_id 수신
int rc = zlink_recv(stream, routing_id, 4, 0);
assert(rc == 4);

// MORE 플래그 확인
int more = 0;
size_t more_size = sizeof(more);
zlink_getsockopt(stream, ZLINK_RCVMORE, &more, &more_size);
assert(more == 1);

// Frame 1: 페이로드 수신
int size = zlink_recv(stream, buf, sizeof(buf), 0);

if (size == 1 && buf[0] == 0x01) {
    // 새 클라이언트 연결
} else if (size == 1 && buf[0] == 0x00) {
    // 클라이언트 연결 해제
} else {
    // 일반 데이터 처리
}
```

### 3.4 송신 코드 패턴

```c
// Frame 0: routing_id 전송 (SNDMORE 필수)
zlink_send(stream, routing_id, 4, ZLINK_SNDMORE);

// Frame 1: 페이로드 전송
zlink_send(stream, "response", 8, 0);
```

---

## 4. 라우팅 ID 관리

### 4.1 라우팅 ID 정의

- 크기: 고정 **4바이트** (`uint32`)
- ROUTER 소켓의 가변 크기 routing_id와 다름
- 각 연결된 피어마다 고유한 라우팅 ID 부여

### 4.2 자동 할당

구현: `stream_t::identify_peer()`

```
1. _next_integral_routing_id 값 사용 (초기값: 1)
2. 4-byte BE로 변환하여 routing_id 생성
3. _next_integral_routing_id++ (0에 도달하면 1로 리셋)
```

### 4.3 수동 할당 (ZLINK_CONNECT_ROUTING_ID)

클라이언트 측에서 `zlink_connect()` 전에 라우팅 ID를 지정할 수 있다.

```c
uint32_t my_id = htonl(42);
zlink_setsockopt(client, ZLINK_CONNECT_ROUTING_ID, &my_id, 4);
zlink_connect(client, endpoint);
```

제약사항:

- `optval`이 non-NULL이고 크기가 **정확히 4바이트**여야 한다
- 그 외 크기는 `EINVAL` 반환
- `locally_initiated_`(클라이언트 측)에서만 적용
- 이미 사용 중인 routing_id와 중복되면 안 됨

### 4.4 내부 자료구조

```cpp
uint32_t _next_integral_routing_id = 1;         // 자동 할당 카운터
std::map<uint32_t, pipe_t*> _out_by_id;          // routing_id → pipe 매핑
```

### 4.5 라우팅 ID 저장

피어가 연결되면 두 형태로 저장:

```cpp
pipe_->set_router_socket_routing_id(routing_id);              // blob 형태 (4B)
pipe_->set_server_socket_routing_id(get_uint32(routing_id));  // uint32 형태
_out_by_id[pipe_->get_server_socket_routing_id()] = pipe_;    // 룩업 맵 등록
```

---

## 5. 연결 생명주기

### 5.1 소켓 생성 및 초기화

```c
void *ctx = zlink_ctx_new();
void *stream = zlink_socket(ctx, ZLINK_STREAM);
```

생성자에서 설정되는 기본값:

| 항목 | 기본값 | 설명 |
|------|--------|------|
| `options.type` | `ZLINK_STREAM` (11) | 소켓 타입 |
| `options.backlog` | `65536` | listen 백로그 |
| `options.in_batch_size` | `65536` (최소) | 수신 배치 크기 |
| `options.out_batch_size` | `65536` (최소) | 송신 배치 크기 |
| `_next_integral_routing_id` | `1` | 라우팅 ID 초기값 |

### 5.2 서버 바인드

```c
zlink_bind(stream, "tcp://*:8080");

// 동적 포트 할당
zlink_bind(stream, "tcp://127.0.0.1:*");
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(stream, ZLINK_LAST_ENDPOINT, endpoint, &len);
```

### 5.3 클라이언트 연결

```c
zlink_connect(client, "tcp://127.0.0.1:8080");
```

### 5.4 파이프 연결 (xattach_pipe)

새 피어가 연결되면 내부적으로 호출:

```
1. identify_peer(pipe, locally_initiated)  → routing_id 할당
2. _fq.attach(pipe)                        → fair queue에 등록
3. queue_event(routing_id, 0x01)           → 연결 이벤트 큐잉
```

### 5.5 파이프 종료 (xpipe_terminated)

피어가 연결 해제되면 내부적으로 호출:

```
1. _out_by_id에서 해당 pipe 제거
2. _fq에서 해당 pipe 제거
3. _current_out이 해당 pipe이면 NULL로 초기화
4. queue_event(routing_id, 0x00)  → 해제 이벤트 큐잉
```

### 5.6 연결/해제 이벤트 흐름

```
클라이언트 connect
    │
    ├→ 서버: [routing_id][0x01]  (연결 이벤트 수신)
    └→ 클라이언트: [routing_id][0x01]  (연결 이벤트 수신)

    ... 데이터 교환 ...

클라이언트 close / 네트워크 단절
    │
    └→ 서버: [routing_id][0x00]  (해제 이벤트 수신)
```

### 5.7 능동적 연결 해제

애플리케이션에서 특정 피어 연결을 강제 종료할 수 있다:

```c
// 0x00 페이로드 전송 → 해당 피어 파이프 terminate
zlink_send(stream, routing_id, 4, ZLINK_SNDMORE);
unsigned char disconnect = 0x00;
zlink_send(stream, &disconnect, 1, 0);
```

내부 동작: `xsend()`에서 1바이트 `0x00` 감지 시 `_current_out->terminate(false)` 호출.

---

## 6. 송수신 상태 머신

### 6.1 송신 상태 머신 (xsend)

구현: `stream_t::xsend()`

```
상태 변수:
  _more_out: bool     (멀티파트 진행 중 여부)
  _current_out: pipe* (현재 대상 파이프)

┌──────────────────────────────────────────────────────────┐
│                    _more_out == false                     │
│                    _current_out == NULL                   │
│                                                          │
│  xsend(msg with MORE flag)                               │
│    ├─ msg.size() != 4 → errno=EINVAL, return -1         │
│    ├─ routing_id = get_uint32(msg.data())                │
│    ├─ _out_by_id.find(routing_id)                        │
│    │   ├─ 못 찾음 → errno=EHOSTUNREACH, return -1       │
│    │   └─ 찾음 → _current_out = pipe                    │
│    │       └─ check_write() 실패 → errno=EAGAIN, -1     │
│    ├─ _more_out = true                                   │
│    └─ return 0 (ID 프레임 소비)                          │
└──────────────────────────────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────┐
│                    _more_out == true                      │
│                                                          │
│  xsend(msg without MORE flag)                            │
│    ├─ _more_out = false                                  │
│    ├─ disconnect 검사 (1B, 0x00)                         │
│    │   └─ 맞으면 pipe->terminate(), return 0             │
│    ├─ _current_out->write(msg)                           │
│    ├─ _current_out->flush()                              │
│    ├─ _current_out = NULL                                │
│    └─ return 0                                           │
└──────────────────────────────────────────────────────────┘
```

### 6.2 수신 상태 머신 (xrecv)

구현: `stream_t::xrecv()`

```
상태 변수:
  _prefetched: bool           (프리페치된 메시지 존재 여부)
  _routing_id_sent: bool      (routing_id 프레임 이미 전달 여부)
  _prefetched_routing_id_value: uint32
  _prefetched_msg: msg_t

┌──────────────────────────────────────────────────────────┐
│ Case 1: _prefetched && !_routing_id_sent                 │
│   → 4-byte routing_id 메시지 생성                        │
│   → MORE 플래그 설정                                     │
│   → _routing_id_sent = true                              │
│   → return 0                                             │
├──────────────────────────────────────────────────────────┤
│ Case 2: _prefetched && _routing_id_sent                  │
│   → _prefetched_msg을 출력으로 이동                      │
│   → _prefetched = false                                  │
│   → return 0                                             │
├──────────────────────────────────────────────────────────┤
│ Case 3: !_prefetched && 이벤트 큐에 항목 있음            │
│   → prefetch_event()                                     │
│   → xrecv() 재귀 호출 (Case 1으로)                      │
├──────────────────────────────────────────────────────────┤
│ Case 4: !_prefetched && 이벤트 큐 비어있음               │
│   → _fq.recvpipe() 호출                                 │
│   → pipe에서 routing_id 추출                             │
│   → 메시지를 _prefetched_msg에 캐시                     │
│   → 4-byte routing_id 메시지 생성 + MORE 설정            │
│   → _prefetched = true, _routing_id_sent = true          │
│   → return 0                                             │
└──────────────────────────────────────────────────────────┘
```

### 6.3 이벤트 프리페치 (prefetch_event)

```
1. _pending_events 큐에서 front 꺼냄
2. routing_id 추출 (4B → uint32)
3. 1-byte 메시지 생성 (이벤트 코드: 0x01 또는 0x00)
4. _prefetched = true, _routing_id_sent = false
```

---

## 7. Fair Queue (수신 분배)

### 7.1 목적

여러 피어가 동시에 데이터를 보낼 때, 특정 피어에 의한 서비스 거부를 방지하기 위해
**라운드 로빈** 방식으로 수신을 분배한다.

### 7.2 자료구조

구현 파일: `core/src/sockets/fq.hpp`, `core/src/sockets/fq.cpp`

```cpp
class fq_t {
    typedef array_t<pipe_t, 1> pipes_t;
    pipes_t _pipes;              // 전체 등록된 파이프
    pipes_t::size_type _active;  // 읽기 가능한 파이프 수
    pipes_t::size_type _current; // 현재 라운드 로빈 위치
    bool _more;                  // 멀티파트 연속 플래그
    pipe_t *_last_in;            // 마지막으로 읽은 파이프
};
```

### 7.3 주요 메서드

| 메서드 | 동작 |
|--------|------|
| `attach(pipe)` | 파이프 등록, 활성화 대기 |
| `activated(pipe)` | 파이프를 활성 목록에 추가 (읽기 가능) |
| `pipe_terminated(pipe)` | 파이프 제거, 활성 목록 조정 |
| `recv(msg)` | 라운드 로빈으로 다음 활성 파이프에서 메시지 수신 |
| `recvpipe(msg, &pipe)` | 수신 + 소스 파이프 반환 |
| `has_in()` | 읽을 수 있는 데이터 존재 여부 |

### 7.4 라운드 로빈 알고리즘

```
1. _current 위치의 파이프에서 msg 읽기 시도
2. 성공 시 → 멀티파트가 아니면 _current 다음으로 이동
3. 실패 시 → 해당 파이프를 비활성으로 이동, _active--, 다음 시도
4. 모든 활성 파이프 순회 후 데이터 없으면 EAGAIN
```

멀티파트 메시지 처리:

- 멀티파트 진행 중(`_more == true`)이면 같은 파이프에서 계속 읽음
- 멀티파트 완료 후에야 다음 파이프로 이동

---

## 8. 소켓 옵션

### 8.1 STREAM 전용 옵션

| 옵션 | 상수 | 타입 | 기본값 | 설명 |
|------|------|------|--------|------|
| `ZLINK_CONNECT_ROUTING_ID` | 61 | `void*` (4B) | -- | connect 시 사용할 routing_id 지정 |

### 8.2 공통 소켓 옵션 (STREAM 적용)

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_ROUTING_ID` | `void*` | -- | 현재 routing_id 조회 (읽기 전용) |
| `ZLINK_MAXMSGSIZE` | `int64_t` | `-1` (무제한) | 최대 메시지 크기, 초과 시 연결 종료 |
| `ZLINK_SNDHWM` | `int` | `300000` | 송신 High Water Mark |
| `ZLINK_RCVHWM` | `int` | `300000` | 수신 High Water Mark |
| `ZLINK_LINGER` | `int` | `-1` | close 시 대기 시간 (ms), 0=즉시 |
| `ZLINK_SNDBUF` | `int` | OS 기본 | OS 송신 버퍼 크기 |
| `ZLINK_RCVBUF` | `int` | OS 기본 | OS 수신 버퍼 크기 |
| `ZLINK_BACKLOG` | `int` | `65536` | listen 백로그 크기 |
| `ZLINK_RCVMORE` | `int` | -- | 멀티파트 연속 여부 조회 (읽기 전용) |
| `ZLINK_RCVTIMEO` | `int` | `-1` | 수신 타임아웃 (ms), -1=블로킹 |
| `ZLINK_SNDTIMEO` | `int` | `-1` | 송신 타임아웃 (ms), -1=블로킹 |
| `ZLINK_LAST_ENDPOINT` | `char*` | -- | 마지막 bind된 엔드포인트 (읽기 전용) |
| `ZLINK_TYPE` | `int` | -- | 소켓 타입 조회 (읽기 전용) |
| `ZLINK_EVENTS` | `int` | -- | 현재 이벤트 플래그 (읽기 전용) |
| `ZLINK_IMMEDIATE` | `int` | `0` | connect 즉시 완료 대기 |
| `ZLINK_CONNECT_TIMEOUT` | `int` | `0` | connect 타임아웃 (ms) |

### 8.3 TLS/WSS 옵션

| 옵션 | 타입 | 기본값 | 용도 | 적용 측 |
|------|------|--------|------|---------|
| `ZLINK_TLS_CERT` | `char*` | -- | 서버 인증서 파일 경로 | 서버 |
| `ZLINK_TLS_KEY` | `char*` | -- | 서버 개인키 파일 경로 | 서버 |
| `ZLINK_TLS_CA` | `char*` | -- | CA 인증서 파일 경로 | 클라이언트 |
| `ZLINK_TLS_HOSTNAME` | `char*` | -- | 호스트명 검증 대상 | 클라이언트 |
| `ZLINK_TLS_TRUST_SYSTEM` | `int` | `1` | 시스템 CA 저장소 신뢰 여부 | 클라이언트 |

### 8.4 TCP Keepalive 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_TCP_KEEPALIVE` | `int` | `-1` | TCP keepalive 활성화 |
| `ZLINK_TCP_KEEPALIVE_CNT` | `int` | `-1` | keepalive 프로브 횟수 |
| `ZLINK_TCP_KEEPALIVE_IDLE` | `int` | `-1` | 유휴 후 프로브 시작까지 시간 (초) |
| `ZLINK_TCP_KEEPALIVE_INTVL` | `int` | `-1` | 프로브 간격 (초) |

### 8.5 송수신 플래그

| 플래그 | 값 | 설명 |
|--------|-----|------|
| `ZLINK_DONTWAIT` | `1` | 논블로킹 송수신 |
| `ZLINK_SNDMORE` | `2` | 멀티파트 메시지 계속 (routing_id 프레임에 사용) |

---

## 9. 트랜스포트 지원

### 9.1 지원 트랜스포트 목록

| 트랜스포트 | URI 스킴 | 암호화 | WebSocket | STREAM 지원 |
|-----------|----------|--------|-----------|-------------|
| TCP | `tcp://` | 없음 | 없음 | O |
| TLS | `tls://` | TLS | 없음 | O |
| WebSocket | `ws://` | 없음 | 있음 | O |
| WebSocket+TLS | `wss://` | TLS | 있음 | O |
| IPC | `ipc://` | -- | -- | **X** |
| inproc | `inproc://` | -- | -- | **X** |

### 9.2 TCP 사용

```c
// 서버
zlink_bind(stream, "tcp://*:8080");
zlink_bind(stream, "tcp://127.0.0.1:*");  // 동적 포트

// 클라이언트
zlink_connect(client, "tcp://127.0.0.1:8080");
```

### 9.3 TLS 사용

```c
// 서버
zlink_setsockopt(server, ZLINK_TLS_CERT, cert_path, strlen(cert_path));
zlink_setsockopt(server, ZLINK_TLS_KEY, key_path, strlen(key_path));
zlink_bind(server, "tls://*:8443");

// 클라이언트
int trust_system = 0;
zlink_setsockopt(client, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(int));
zlink_setsockopt(client, ZLINK_TLS_CA, ca_path, strlen(ca_path));
zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "example.com", 11);
zlink_connect(client, "tls://example.com:8443");
```

### 9.4 WebSocket 사용

```c
// 서버
zlink_bind(stream, "ws://127.0.0.1:*");

// 클라이언트
zlink_connect(client, "ws://127.0.0.1:9090");
```

WebSocket 핸드셰이크는 자동 처리된다. 핸드셰이크 완료 후 연결 이벤트(0x01)가 발생한다.

### 9.5 WebSocket+TLS (WSS) 사용

```c
// 서버
zlink_setsockopt(server, ZLINK_TLS_CERT, cert_path, strlen(cert_path));
zlink_setsockopt(server, ZLINK_TLS_KEY, key_path, strlen(key_path));
zlink_bind(server, "wss://127.0.0.1:*");

// 클라이언트
int trust_system = 0;
zlink_setsockopt(client, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(int));
zlink_setsockopt(client, ZLINK_TLS_CA, ca_path, strlen(ca_path));
zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);
zlink_connect(client, "wss://localhost:8443");
```

### 9.6 트랜스포트 제약

- `ws://`, `wss://`는 **STREAM 소켓 전용**이다. 다른 소켓 타입에서는 사용 불가.
- `ipc://`, `inproc://`는 STREAM 소켓에서 사용할 수 없다.
- `tls://`는 모든 소켓 타입에서 사용 가능하다.

---

## 10. 에러 처리

### 10.1 송신 에러

| errno | 조건 | 발생 위치 |
|-------|------|-----------|
| `EINVAL` | routing_id 프레임 크기가 4가 아님 | `xsend()` |
| `EAGAIN` | 대상 파이프 쓰기 불가 (버퍼 풀) | `xsend()` |
| `EHOSTUNREACH` | routing_id에 해당하는 파이프 없음 | `xsend()` |

### 10.2 수신 에러

| errno | 조건 | 발생 위치 |
|-------|------|-----------|
| `EAGAIN` | fair queue에 읽을 데이터 없음 | `xrecv()` via `_fq.recvpipe()` |

### 10.3 디코더 에러

| errno | 조건 | 결과 |
|-------|------|------|
| `EMSGSIZE` | 메시지 크기 > MAXMSGSIZE | 연결 종료, disconnect 이벤트 |
| `ENOMEM` | 메모리 할당 실패 | 연결 종료 |

### 10.4 소켓 옵션 에러

| errno | 조건 |
|-------|------|
| `EINVAL` | `ZLINK_CONNECT_ROUTING_ID` 크기가 4가 아님 |

### 10.5 에러 처리 전략

- 내부 불변 조건 위반 시 `errno_assert()` 매크로로 프로세스 중단
- `zlink_assert()` 매크로로 논리적 불변 조건 검증
- 복구 가능한 에러는 `errno` 설정 후 `-1` 반환

---

## 11. 스레딩 모델

### 11.1 소켓 스레드 안전성

- STREAM 소켓은 **스레드 세이프하지 않다**
- 모든 소켓 조작은 **단일 스레드에서 직렬화**해야 한다
- 한 소켓을 여러 스레드에서 동시 접근하면 정의되지 않은 동작 발생

### 11.2 I/O 스레드

- 소켓 내부 I/O는 컨텍스트의 I/O 스레드가 처리
- 사용자 스레드와 I/O 스레드 간 통신은 인터록 커맨드 큐 사용
- 커맨드: bind, connect, term, stop 등

### 11.3 멀티파트 상태 무결성

- `_more_out`, `_routing_id_sent`, `_prefetched` 등 상태 변수는 volatile이 아님
- 호출자가 직렬 접근을 보장해야 함
- 멀티파트 송수신 중 다른 스레드에서 접근하면 상태 불일치 발생

---

## 12. 재연결 및 페일오버

### 12.1 재연결 동작

- 연결 해제 시 자동 재연결 (트랜스포트별 구현)
- `ZLINK_RECONNECT_IVL`: 초기 재연결 간격 (기본 100ms)
- `ZLINK_RECONNECT_IVL_MAX`: 지수 백오프 상한

### 12.2 재연결 시 라우팅 ID

- 재연결 시 **새로운 routing_id가 할당**된다 (기존 ID 재사용 안 함)
- `ZLINK_CONNECT_ROUTING_ID`를 설정하면 재연결 시에도 동일 ID 사용 가능
- 서버 측에서는 기존 연결의 disconnect 이벤트(0x00) 후 새 connect 이벤트(0x01)가 발생

### 12.3 연결 풀링

- 연결 풀링이나 멀티플렉싱은 없음
- 파이프 1개 = 연결 1개
- 연결 해제된 파이프는 재사용되지 않음

---

## 13. 클래스 계층 구조

### 13.1 상속 관계

```
socket_base_t                    (기본 소켓)
  └─ routing_socket_base_t       (라우팅 기능 공통)
       ├─ router_t               (ROUTER 소켓)
       └─ stream_t               (STREAM 소켓)
```

### 13.2 stream_t 주요 멤버

구현 파일: `core/src/sockets/stream.hpp`

```cpp
class stream_t : public routing_socket_base_t {
    // 이벤트 구조체
    struct stream_event_t {
        blob_t routing_id;
        unsigned char code;      // 0x01=connect, 0x00=disconnect
    };

    // 수신 상태
    fq_t _fq;                               // fair queue
    bool _prefetched;                        // 프리페치 메시지 존재
    bool _routing_id_sent;                   // routing_id 프레임 전달 여부
    uint32_t _prefetched_routing_id_value;   // 캐시된 routing_id
    msg_t _prefetched_msg;                   // 캐시된 페이로드

    // 송신 상태
    pipe_t *_current_out;                    // 현재 송신 대상 파이프
    bool _more_out;                          // 멀티파트 진행 중

    // 라우팅 관리
    uint32_t _next_integral_routing_id;      // 자동 할당 카운터
    std::map<uint32_t, pipe_t*> _out_by_id;  // routing_id → pipe

    // 이벤트 큐
    std::deque<stream_event_t> _pending_events;
};
```

### 13.3 관련 컴포넌트

| 컴포넌트 | 파일 | 역할 |
|----------|------|------|
| `stream_t` | `src/sockets/stream.cpp` | STREAM 소켓 핵심 로직 |
| `raw_encoder_t` | `src/protocol/raw_encoder.cpp` | Length-Prefix 인코딩 |
| `raw_decoder_t` | `src/protocol/raw_decoder.cpp` | Length-Prefix 디코딩 |
| `asio_raw_engine_t` | `src/engine/asio/asio_raw_engine.cpp` | RAW I/O 엔진 |
| `ws_transport_t` | `src/transports/ws/` | WebSocket 트랜스포트 |
| `wss_transport_t` | `src/transports/ws/` | WebSocket + TLS |
| `fq_t` | `src/sockets/fq.cpp` | Fair Queue (수신 분배) |
| `wire.hpp` | `src/protocol/wire.hpp` | 바이트 오더 변환 헬퍼 |

---

## 14. WS/WSS 성능 최적화

### 14.1 읽기 경로 복사 제거

- 변경 전: Beast flat_buffer → 임시 버퍼 → msg_t (2회 복사)
- 변경 후: Beast flat_buffer에서 msg_t로 직접 이동 (1회 복사 제거)

### 14.2 쓰기 경로 복사 제거

- 변경 전: msg_t → 중간 버퍼 → Beast write (2회 복사)
- 변경 후: msg_t 데이터를 Beast write 버퍼에 직접 전달

### 14.3 Beast 쓰기 버퍼 확대

- 기본 4KB → **64KB**로 확대
- 소규모 메시지 다수를 배치 전송 가능

### 14.4 프레임 프래그먼테이션 비활성화

- `auto_fragment(false)` 설정
- 메시지당 단일 WebSocket 프레임 보장

### 14.5 벤치마크 결과

| 항목 | 최적화 전 | 최적화 후 | 개선율 |
|------|-----------|-----------|--------|
| WS 1KB | 315 MB/s | 473 MB/s | +50% |
| WSS 1KB | 279 MB/s | 382 MB/s | +37% |
| WS 64KB | -- | -- | +97% |
| WSS 64KB | -- | -- | +54% |
| WS 262KB | -- | -- | +139% |
| WSS 262KB | -- | -- | +62% |

Beast 독립 실행 대비:

| 트랜스포트 | Beast | zlink | 비율 |
|-----------|-------|-------|------|
| tcp | 1416 MB/s | 1493 MB/s | 105% |
| ws | 540 MB/s | 696 MB/s | 129% |

---

## 15. C API 레퍼런스

### 15.1 소켓 생성/해제

```c
void *stream = zlink_socket(ctx, ZLINK_STREAM);
int rc = zlink_close(stream);
```

### 15.2 바인드/연결

```c
int rc = zlink_bind(stream, "tcp://*:8080");
int rc = zlink_connect(stream, "tcp://127.0.0.1:8080");
int rc = zlink_disconnect(stream, "tcp://127.0.0.1:8080");
```

### 15.3 송수신

```c
// 송신 (2-프레임 필수)
int rc = zlink_send(stream, routing_id, 4, ZLINK_SNDMORE);
int rc = zlink_send(stream, data, size, 0);

// 수신 (2-프레임)
int rc = zlink_recv(stream, routing_id, 4, 0);    // Frame 0
int rc = zlink_recv(stream, buffer, buf_size, 0);  // Frame 1
```

### 15.4 옵션 설정/조회

```c
// 설정
int rc = zlink_setsockopt(stream, ZLINK_LINGER, &linger, sizeof(linger));

// 조회
int more;
size_t more_size = sizeof(more);
int rc = zlink_getsockopt(stream, ZLINK_RCVMORE, &more, &more_size);
```

### 15.5 폴링

```c
zlink_pollitem_t items[] = {
    { stream, 0, ZLINK_POLLIN, 0 }
};
int rc = zlink_poll(items, 1, timeout_ms);

if (items[0].revents & ZLINK_POLLIN) {
    // 데이터 수신 가능
}
```

---

## 16. 구현 체크리스트

STREAM 소켓을 처음부터 구현할 때 아래 항목을 모두 충족해야 한다.

### 16.1 핵심 기능

- [ ] 소켓 타입 등록 (`ZLINK_STREAM = 11`)
- [ ] `routing_socket_base_t` 상속
- [ ] 4-byte 고정 크기 routing_id 할당 로직
- [ ] routing_id 자동 증가 카운터 (1부터, 0 스킵)
- [ ] `_out_by_id` 맵을 통한 routing_id → pipe 매핑
- [ ] Fair Queue (`fq_t`) 기반 수신 분배

### 16.2 송수신

- [ ] 2-프레임 멀티파트 송신 상태 머신 (`_more_out`, `_current_out`)
- [ ] 2-프레임 멀티파트 수신 상태 머신 (`_prefetched`, `_routing_id_sent`)
- [ ] 프리페치 메시지 캐싱 (`_prefetched_msg`)
- [ ] MORE 플래그 설정/해제
- [ ] 논블로킹 모드 지원 (`ZLINK_DONTWAIT`)

### 16.3 이벤트 시스템

- [ ] 연결 이벤트 (0x01) 생성 및 큐잉
- [ ] 해제 이벤트 (0x00) 생성 및 큐잉
- [ ] 이벤트 큐 (`_pending_events`) 관리
- [ ] 이벤트 프리페치 로직 (`prefetch_event()`)
- [ ] 능동적 disconnect (0x00 송신 시 pipe terminate)

### 16.4 와이어 프로토콜

- [ ] 4-byte big-endian length-prefix 인코더
- [ ] 4-byte big-endian length-prefix 디코더
- [ ] MAXMSGSIZE 검증 (디코더)
- [ ] 바이트 오더 변환 유틸리티

### 16.5 소켓 옵션

- [ ] `ZLINK_CONNECT_ROUTING_ID` 설정 (4바이트 제한)
- [ ] `ZLINK_MAXMSGSIZE` 적용
- [ ] `ZLINK_LINGER` 적용
- [ ] `ZLINK_SNDHWM` / `ZLINK_RCVHWM` 적용
- [ ] `ZLINK_RCVMORE` 조회
- [ ] `ZLINK_LAST_ENDPOINT` 조회
- [ ] 기본 backlog 65536 설정
- [ ] 기본 batch size 65536 설정

### 16.6 트랜스포트

- [ ] TCP (`tcp://`) 지원
- [ ] TLS (`tls://`) 지원
- [ ] WebSocket (`ws://`) 지원
- [ ] WebSocket+TLS (`wss://`) 지원
- [ ] TLS 옵션 (CERT, KEY, CA, HOSTNAME, TRUST_SYSTEM) 처리

### 16.7 에러 처리

- [ ] EINVAL: routing_id 크기 불일치
- [ ] EAGAIN: 파이프 쓰기 불가
- [ ] EHOSTUNREACH: routing_id 미발견
- [ ] EMSGSIZE: 최대 크기 초과
- [ ] ENOMEM: 메모리 부족

### 16.8 정리 (cleanup)

- [ ] 파이프 종료 시 `_out_by_id`에서 제거
- [ ] 파이프 종료 시 fair queue에서 제거
- [ ] 파이프 종료 시 `_current_out` NULL 처리
- [ ] 소멸자에서 `_prefetched_msg.close()` 호출

---

## 17. 테스트 시나리오

### 17.1 기본 TCP 통신 (`test_stream_tcp_basic`)

```
1. 서버 bind, 클라이언트 connect
2. 양측 connect 이벤트(0x01) 수신 확인
3. 클라이언트 → 서버 메시지 전송/수신 확인
4. 서버 → 클라이언트 응답 전송/수신 확인
5. 클라이언트 close → 서버에서 disconnect 이벤트(0x00) 수신 확인
```

### 17.2 MAXMSGSIZE 적용 (`test_stream_maxmsgsize`)

```
1. 서버에 MAXMSGSIZE=4 설정
2. 양측 connect 이벤트 수신
3. 클라이언트에서 8바이트 메시지 전송
4. 서버에서 disconnect 이벤트(0x00) 수신 확인 (연결 종료)
```

### 17.3 WebSocket 통신 (`test_stream_ws_basic`)

```
1. 서버 ws:// bind, 클라이언트 ws:// connect
2. 양측 connect 이벤트 수신 (WebSocket 핸드셰이크 자동)
3. 메시지 교환 검증
```

### 17.4 WSS 통신 (`test_stream_wss_basic`)

```
1. 서버 TLS 인증서/키 설정
2. 클라이언트 CA/hostname 설정, trust_system=0
3. wss:// bind/connect
4. 양측 connect 이벤트 수신
5. 암호화된 채널로 메시지 교환 검증
```

### 17.5 라우팅 ID 크기 확인 (`test_stream_routing_id_size`)

```
1. STREAM 소켓 간 연결
2. 수신된 routing_id 크기가 정확히 4바이트인지 확인
3. RCVMORE 플래그 설정 확인
```

### 17.6 CONNECT_ROUTING_ID 확인 (`test_connect_rid_string_alias`)

```
1. 문자열 routing_id 설정 시 EINVAL 반환 확인
2. 4바이트 바이너리 routing_id 설정 시 성공 확인
3. 설정된 고정 ID가 자동 할당 대신 사용되는지 확인
```

---

## 18. 데이터 흐름 다이어그램

### 18.1 전체 데이터 경로

```
Application                 Stream Socket                Engine               Transport
    │                           │                          │                      │
    │  zlink_send(rid+data)     │                          │                      │
    │──────────────────────────►│                          │                      │
    │                           │  xsend(rid) → find pipe  │                      │
    │                           │  xsend(data) → write     │                      │
    │                           │─────────────────────────►│                      │
    │                           │                          │  raw_encode          │
    │                           │                          │  [4B len][payload]   │
    │                           │                          │─────────────────────►│
    │                           │                          │                      │ TCP/WS
    │                           │                          │                      │ write
    │                           │                          │                      │
    │                           │                          │  raw_decode          │
    │                           │                          │◄─────────────────────│
    │                           │  _fq.recvpipe()          │                      │
    │                           │◄─────────────────────────│                      │
    │  zlink_recv(rid)          │                          │                      │
    │◄──────────────────────────│  xrecv() → routing_id    │                      │
    │  zlink_recv(data)         │                          │                      │
    │◄──────────────────────────│  xrecv() → payload       │                      │
```

### 18.2 이벤트 흐름

```
Transport (peer connects)
    │
    ▼
xattach_pipe()
    ├─ identify_peer()  → routing_id 할당
    ├─ _fq.attach()     → fair queue 등록
    └─ queue_event(rid, 0x01)
         │
         ▼
    _pending_events 큐
         │
    xrecv() 호출 시
         │
         ▼
    prefetch_event()
         │
         ▼
    [routing_id][0x01] 멀티파트 메시지로 전달
```

---

## 19. 참고 파일 경로

### 19.1 핵심 구현

| 파일 | 설명 |
|------|------|
| `core/src/sockets/stream.hpp` | STREAM 소켓 클래스 정의 |
| `core/src/sockets/stream.cpp` | STREAM 소켓 구현 (~306행) |
| `core/src/sockets/socket_base.hpp` | 기본 소켓 클래스 |
| `core/src/sockets/fq.hpp` / `fq.cpp` | Fair Queue |
| `core/src/protocol/raw_encoder.cpp` | Length-Prefix 인코더 |
| `core/src/protocol/raw_decoder.cpp` | Length-Prefix 디코더 |
| `core/src/protocol/wire.hpp` | 바이트 오더 헬퍼 |

### 19.2 공개 API

| 파일 | 설명 |
|------|------|
| `core/include/zlink.h` | C 공개 헤더 (소켓 타입, 옵션, API 함수) |

### 19.3 테스트

| 파일 | 설명 |
|------|------|
| `core/tests/test_stream_socket.cpp` | TCP/WS/WSS 기본 테스트 |
| `core/tests/routing-id/test_stream_routing_id_size.cpp` | routing_id 크기 테스트 |
| `core/tests/routing-id/test_connect_rid_string_alias.cpp` | CONNECT_ROUTING_ID 테스트 |

### 19.4 시나리오 벤치마크

| 파일 | 설명 |
|------|------|
| `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp` | 성능 시나리오 |
| `core/tests/scenario/stream/zlink/run_stream_scenarios.sh` | 시나리오 러너 |
| `bindings/python/benchwithzlink/pattern_stream.py` | Python 벤치마크 |

### 19.5 문서

| 파일 | 설명 |
|------|------|
| `doc/guide/03-5-stream.md` | 사용자 가이드 |
| `doc/internals/stream-socket.md` | WS/WSS 최적화 상세 |
| `doc/plan/stream-cs-fastpath-cppserver-based.ko.md` | CS fastpath 설계 |
