# Transport 가이드

## 1. Transport 종류

| Transport | URI 형식 | 예시 | 암호화 | 핸드셰이크 |
|-----------|----------|------|:------:|:----------:|
| tcp | `tcp://host:port` | `tcp://127.0.0.1:5555` | - | - |
| ipc | `ipc://path` | `ipc:///tmp/test.ipc` | - | - |
| inproc | `inproc://name` | `inproc://workers` | - | - |
| ws | `ws://host:port` | `ws://127.0.0.1:8080` | - | O |
| wss | `wss://host:port` | `wss://server:8443` | O | O |
| tls | `tls://host:port` | `tls://server:5555` | O | O |

## 2. TCP

표준 TCP/IP 네트워크 통신.

### 기본 사용법

```c
/* 서버: 특정 인터페이스 */
zlink_bind(socket, "tcp://192.168.1.10:5555");

/* 서버: 모든 인터페이스 */
zlink_bind(socket, "tcp://*:5555");

/* 클라이언트: IP 주소 */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* 클라이언트: DNS 이름 */
zlink_connect(socket, "tcp://server.example.com:5555");
```

### 와일드카드 포트 (자동 할당)

OS가 사용 가능한 포트를 자동 할당한다. 테스트나 동적 포트 환경에서 유용하다.

```c
/* 포트 0 또는 * 사용 */
zlink_bind(socket, "tcp://127.0.0.1:*");

/* 할당된 엔드포인트 조회 */
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
/* endpoint = "tcp://127.0.0.1:53821" (예시) */

/* 조회된 엔드포인트로 연결 */
zlink_connect(other_socket, endpoint);
```

> 참고: `core/tests/test_pair_tcp.cpp` — `bind_loopback_ipv4()` 와일드카드 바인드 패턴

### DNS 이름 사용

connect 시 호스트명을 사용하면 내부적으로 DNS 리졸빙이 수행된다.

```c
/* DNS 이름으로 연결 */
zlink_connect(socket, "tcp://localhost:5555");
```

> 주의: DNS 리졸빙은 블로킹으로 수행된다. 프로덕션에서는 IP 주소 사용을 권장한다.
> 참고: `core/tests/test_pair_tcp.cpp` — `test_pair_tcp_connect_by_name()`

### 에러 처리

```c
/* bind 실패: 포트 이미 사용 중 */
int rc = zlink_bind(socket, "tcp://*:5555");
if (rc == -1) {
    if (errno == EADDRINUSE)
        printf("포트 5555 이미 사용 중\n");
}

/* connect 실패: 잘못된 주소 */
rc = zlink_connect(socket, "tcp://invalid:99999");
if (rc == -1) {
    printf("연결 실패: %s\n", zlink_strerror(errno));
}
```

### 특성

- **TCP_NODELAY** 활성화 (Nagle 알고리즘 비활성화)
- **Speculative write** — 동기 쓰기 먼저 시도 후 실패 시 비동기 전환
- **Gather write** — 헤더와 바디를 한번에 전송 (시스템콜 감소)

> Speculative write 등 내부 최적화 상세는 [architecture.md](../internals/architecture.md)를 참고.

## 3. IPC

Unix 도메인 소켓 기반 로컬 프로세스 간 통신.

### 기본 사용법

```c
/* 서버 */
zlink_bind(socket, "ipc:///tmp/myapp.ipc");

/* 클라이언트 */
zlink_connect(socket, "ipc:///tmp/myapp.ipc");
```

### 와일드카드 바인드

```c
/* IPC 와일드카드 — 임시 경로 자동 할당 */
zlink_bind(socket, "ipc://*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `zlink_bind(router, "ipc://*")`

### 에러 처리

```c
/* 경로가 너무 긴 경우 */
int rc = zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
if (rc == -1 && errno == ENAMETOOLONG) {
    printf("IPC 경로가 시스템 제한(108자)을 초과\n");
}
```

> 참고: `core/tests/test_pair_ipc.cpp` — `test_endpoint_too_long()`

### 특성

- **Linux/macOS에서만 지원** (Windows 미지원)
- TCP 대비 낮은 오버헤드 (네트워크 스택 우회)
- 파일 경로 기반 주소 (경로 최대 108자)

## 4. inproc

프로세스 내(in-process) 통신. 가장 빠른 transport.

### 기본 사용법

```c
/* bind가 먼저 호출되어야 함 */
zlink_bind(socket_a, "inproc://workers");
zlink_connect(socket_b, "inproc://workers");
```

### 에러 처리

```c
/* bind 없이 connect 시도 */
int rc = zlink_connect(socket, "inproc://nonexistent");
if (rc == -1) {
    printf("bind가 아직 없음\n");
}
```

### 특성

- **동일 context 내에서만** 사용 가능
- **bind가 connect보다 먼저** 호출되어야 함
- Lock-free pipe 직접 연결 (네트워크 없음)
- 가장 낮은 지연시간, 가장 높은 처리량

> 참고: `core/tests/test_pair_inproc.cpp` — bind → connect → bounce 패턴

## 5. WebSocket (ws)

웹 브라우저 및 외부 클라이언트 연동.

### 기본 사용법

```c
/* 서버 */
zlink_bind(socket, "ws://*:8080");

/* 클라이언트 */
zlink_connect(socket, "ws://server:8080");

/* 와일드카드 포트 */
zlink_bind(socket, "ws://127.0.0.1:*");
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_ws_basic()`

### 특성

- RFC 6455 준수
- Beast 라이브러리 기반
- 바이너리 프레임 모드 (Opcode=0x02)
- 64KB write buffer
- **STREAM 소켓에서만 사용 가능**

## 6. WebSocket + TLS (wss)

암호화된 WebSocket 통신.

### 기본 사용법

```c
/* 서버 */
zlink_setsockopt(socket, ZLINK_TLS_CERT, cert_path, 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, key_path, 0);
zlink_bind(socket, "wss://*:8443");

/* 클라이언트 */
int trust_system = 0;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system));
zlink_setsockopt(socket, ZLINK_TLS_CA, ca_path, 0);
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "localhost", 9);
zlink_connect(socket, "wss://server:8443");
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_wss_basic()`

### ws 대비 추가 설정

| 설정 | ws | wss |
|------|:--:|:---:|
| `ZLINK_TLS_CERT` (서버) | - | 필수 |
| `ZLINK_TLS_KEY` (서버) | - | 필수 |
| `ZLINK_TLS_CA` (클라이언트) | - | 권장 |
| `ZLINK_TLS_HOSTNAME` (클라이언트) | - | 권장 |
| `ZLINK_TLS_TRUST_SYSTEM` (클라이언트) | - | 선택 |

## 7. TLS

네이티브 TLS 암호화 통신.

### 기본 사용법

```c
/* 서버 */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/cert.pem", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/key.pem", 0);
zlink_bind(socket, "tls://*:5555");

/* 클라이언트 */
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.pem", 0);
zlink_connect(socket, "tls://server:5555");
```

상세 TLS 설정은 [TLS 보안 가이드](05-tls-security.md)를 참고.

## 8. Transport 제약사항

| 제약 | 설명 |
|------|------|
| ws/wss → STREAM만 | ws, wss transport는 STREAM 소켓만 지원. tls는 모든 소켓 타입 사용 가능 |
| inproc bind 우선 | inproc는 bind가 connect보다 먼저 호출 필요 |
| ipc 플랫폼 | ipc는 Unix/Linux/macOS만 지원 (Windows 미지원) |
| 동일 context | inproc는 동일 context 내에서만 사용 |
| IPC 경로 길이 | Unix 도메인 소켓 경로 최대 108자 |

## 9. Transport 선택 의사결정 플로우

```
통신 상대가 외부 클라이언트인가?
├── Yes → 암호화 필요?
│         ├── Yes → WebSocket? → wss://
│         │         └── No → tls://
│         └── No → WebSocket? → ws://
│                   └── No → tcp:// (STREAM)
└── No → 같은 프로세스?
         ├── Yes → inproc://
         └── No → 같은 머신?
                  ├── Yes → Unix? → ipc://
                  │         └── Windows → tcp://
                  └── No → 암호화 필요?
                           ├── Yes → tls://
                           └── No → tcp://
```

| 사용 사례 | 추천 Transport | 비고 |
|-----------|---------------|------|
| 스레드 간 통신 | inproc | 최고 성능 |
| 로컬 프로세스 간 (Unix) | ipc | TCP 대비 낮은 오버헤드 |
| 로컬 프로세스 간 (Windows) | tcp | IPC 미지원 |
| 서버 간 통신 | tcp | 표준 네트워크 통신 |
| 암호화 통신 | tls | 네이티브 TLS |
| 웹 클라이언트 | ws 또는 wss | WebSocket |
| 최고 성능 순서 | inproc > ipc > tcp > ws | 오버헤드 증가 순 |

## 10. bind vs connect

### 기본 원칙

- **bind**: 안정적인 주소를 제공하는 쪽 (서버, 잘 알려진 주소)
- **connect**: 상대방 주소를 알고 연결하는 쪽 (클라이언트)

### 다중 bind/connect

하나의 소켓에 여러 엔드포인트를 bind하거나 connect할 수 있다.

```c
/* 다중 bind — 여러 인터페이스에서 수신 */
zlink_bind(router, "tcp://192.168.1.10:5555");
zlink_bind(router, "tcp://10.0.0.1:5555");
zlink_bind(router, "ipc:///tmp/router.ipc");

/* 다중 connect — 여러 서버에 연결 */
zlink_connect(dealer, "tcp://server1:5555");
zlink_connect(dealer, "tcp://server2:5555");
```

### ZLINK_LAST_ENDPOINT

와일드카드 바인드 후 실제 할당된 엔드포인트를 조회한다.

```c
zlink_bind(socket, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
printf("바인드된 엔드포인트: %s\n", endpoint);
```

성능 비교는 [성능 가이드](10-performance.md)를 참고.
