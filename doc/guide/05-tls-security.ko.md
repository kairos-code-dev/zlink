# TLS/SSL 설정 및 보안 가이드

## 1. 개요

zlink는 OpenSSL을 통해 `tls://`와 `wss://` transport를 네이티브 지원한다. 외부 프록시 없이 암호화된 통신을 직접 구성할 수 있다.

## 2. TLS 서버 설정

```c
void *socket = zlink_socket(ctx, ZLINK_ROUTER);

/* 인증서 및 키 설정 (bind 전) */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/server.crt", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/server.key", 0);

/* TLS 바인드 */
zlink_bind(socket, "tls://*:5555");
```

## 3. TLS 클라이언트 설정

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);

/* CA 인증서 설정 */
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.crt", 0);

/* (선택) 호스트명 검증 */
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "server.example.com", 0);

/* TLS 연결 */
zlink_connect(socket, "tls://server.example.com:5555");
```

## 4. WSS (WebSocket + TLS) 설정

WSS는 ws에 TLS 암호화를 추가한 transport이다. ws 대비 추가 설정이 필요하다.

### WSS 서버

```c
void *socket = zlink_socket(ctx, ZLINK_STREAM);

/* TLS 인증서/키 설정 */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/cert.pem", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/key.pem", 0);

/* WSS 바인드 */
zlink_bind(socket, "wss://*:8443");
```

### WSS 클라이언트

```c
void *socket = zlink_socket(ctx, ZLINK_STREAM);

/* 시스템 CA 사용 비활성화 (사설 인증서 사용 시) */
int trust_system = 0;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system));

/* CA 인증서 설정 */
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.pem", 0);

/* 호스트명 검증 */
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "localhost", 9);

/* WSS 연결 */
zlink_connect(socket, "wss://server:8443");
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_wss_basic()`

### ws vs wss 설정 비교

| 설정 | ws | wss |
|------|:--:|:---:|
| 기본 소켓 생성 | O | O |
| `ZLINK_TLS_CERT` / `ZLINK_TLS_KEY` (서버) | - | 필수 |
| `ZLINK_TLS_CA` (클라이언트) | - | 권장 |
| `ZLINK_TLS_HOSTNAME` (클라이언트) | - | 권장 |
| `ZLINK_TLS_TRUST_SYSTEM` (클라이언트) | - | 선택 |

## 5. TLS 소켓 옵션 상세

| 옵션 | 타입 | 방향 | 기본값 | 설명 |
|------|------|------|--------|------|
| `ZLINK_TLS_CERT` | string | 서버 | — | 인증서 파일 경로 (PEM 형식) |
| `ZLINK_TLS_KEY` | string | 서버 | — | 개인키 파일 경로 (PEM 형식) |
| `ZLINK_TLS_CA` | string | 클라이언트 | — | CA 인증서 경로 (서버 인증서 검증) |
| `ZLINK_TLS_HOSTNAME` | string | 클라이언트 | — | 서버 호스트명 (CN/SAN 검증) |
| `ZLINK_TLS_TRUST_SYSTEM` | int | 클라이언트 | 1 | 시스템 CA 스토어 신뢰 여부 |

### ZLINK_TLS_CERT / ZLINK_TLS_KEY

서버가 클라이언트에게 자신을 인증하기 위한 인증서와 개인키.

```c
/* PEM 형식 파일 경로 */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "server.crt", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "server.key", 0);
```

- 반드시 `zlink_bind()` **이전에** 설정
- PEM 형식만 지원
- 인증서와 키가 일치하지 않으면 핸드셰이크 실패

### ZLINK_TLS_CA

클라이언트가 서버 인증서를 검증하기 위한 CA 인증서.

```c
zlink_setsockopt(socket, ZLINK_TLS_CA, "ca.crt", 0);
```

- CA 미설정 시 시스템 CA 스토어 사용 (`ZLINK_TLS_TRUST_SYSTEM=1`)
- 사설 CA 사용 시 반드시 설정

### ZLINK_TLS_HOSTNAME

클라이언트가 서버 인증서의 CN(Common Name) 또는 SAN(Subject Alternative Name)을 검증.

```c
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "server.example.com", 0);
```

- 미설정 시 호스트명 검증 생략 (보안 경고)
- 프로덕션에서는 반드시 설정 권장
- 인증서의 CN 또는 SAN과 일치해야 함

### ZLINK_TLS_TRUST_SYSTEM

시스템 CA 스토어(OS에 설치된 루트 인증서)를 신뢰할지 여부.

```c
/* 시스템 CA 비활성화 (사설 인증서만 사용) */
int trust = 0;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust, sizeof(trust));

/* 시스템 CA 활성화 (기본값) */
int trust = 1;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust, sizeof(trust));
```

- 기본값: 1 (시스템 CA 신뢰)
- 사설 CA만 사용하는 환경에서는 0으로 설정하고 `ZLINK_TLS_CA` 명시
- 공인 인증서 사용 시 기본값 유지

> 참고: `core/tests/test_stream_socket.cpp` — `trust_system = 0` 설정 후 사설 CA 사용

## 6. 테스트용 인증서 생성

### CA 키 및 인증서

```bash
openssl req -x509 -newkey rsa:2048 -keyout ca.key -out ca.crt \
  -days 365 -nodes -subj "/CN=Test CA"
```

### 서버 키 및 CSR

```bash
openssl req -newkey rsa:2048 -keyout server.key -out server.csr \
  -nodes -subj "/CN=localhost"
```

### CA로 서버 인증서 서명

```bash
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365
```

### SAN (Subject Alternative Name) 포함

호스트명 검증을 위해 SAN을 포함하는 인증서 생성:

```bash
openssl req -newkey rsa:2048 -keyout server.key -out server.csr \
  -nodes -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365 \
  -copy_extensions copy
```

## 7. 일반적 TLS 에러 및 트러블슈팅

### 인증서/키 불일치

```
증상: bind 또는 핸드셰이크 실패
원인: 서버 인증서와 개인키가 일치하지 않음
해결: 인증서-키 쌍 확인
```

```bash
# 인증서와 키의 modulus 비교
openssl x509 -noout -modulus -in server.crt | openssl md5
openssl rsa -noout -modulus -in server.key | openssl md5
# 두 값이 같아야 함
```

### CA 인증서 미설정

```
증상: 클라이언트 연결 실패, 핸드셰이크 타임아웃
원인: 클라이언트가 서버 인증서를 검증할 CA가 없음
해결: ZLINK_TLS_CA 설정 또는 ZLINK_TLS_TRUST_SYSTEM 확인
```

### 호스트명 불일치

```
증상: 핸드셰이크 실패
원인: ZLINK_TLS_HOSTNAME과 인증서 CN/SAN 불일치
해결: 인증서에 올바른 CN/SAN 포함, 또는 HOSTNAME 설정 수정
```

### 인증서 만료

```
증상: 핸드셰이크 실패
원인: 서버 또는 CA 인증서 유효기간 만료
해결: 인증서 갱신
```

```bash
# 인증서 유효기간 확인
openssl x509 -noout -dates -in server.crt
```

### 모니터링으로 TLS 에러 감지

```c
void *mon = zlink_socket_monitor_open(socket,
    ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL |
    ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL |
    ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);

zlink_monitor_event_t ev;
if (zlink_monitor_recv(mon, &ev, ZLINK_DONTWAIT) == 0) {
    printf("핸드셰이크 실패: event=0x%llx value=%llu\n",
           (unsigned long long)ev.event,
           (unsigned long long)ev.value);
}
```

## 8. 운영 환경 체크리스트

### 인증서 관리

- [ ] TLS 1.2 이상 사용 (OpenSSL 기본 설정)
- [ ] 프로덕션에서 공인 CA 인증서 사용
- [ ] 인증서 만료 전 자동 갱신 프로세스 구축
- [ ] 개인키 파일 권한 제한 (`chmod 600`)
- [ ] 인증서 체인 완전성 확인

### 클라이언트 설정

- [ ] `ZLINK_TLS_HOSTNAME` 설정 (호스트명 검증 활성화)
- [ ] `ZLINK_TLS_CA` 명시적 설정 또는 시스템 CA 확인
- [ ] 사설 CA 사용 시 `ZLINK_TLS_TRUST_SYSTEM=0`

### 모니터링

- [ ] `HANDSHAKE_FAILED_*` 이벤트 모니터링
- [ ] 인증서 만료 알림 설정
- [ ] TLS 연결 실패 로깅

## 9. 완전한 예제

### TLS 서버-클라이언트

```c
#include <zlink.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    void *ctx = zlink_ctx_new();

    /* TLS 서버 */
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_setsockopt(server, ZLINK_TLS_CERT, "server.crt", 0);
    zlink_setsockopt(server, ZLINK_TLS_KEY, "server.key", 0);
    zlink_bind(server, "tls://*:5555");

    /* TLS 클라이언트 */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_setsockopt(client, ZLINK_TLS_CA, "ca.crt", 0);
    zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);
    zlink_connect(client, "tls://127.0.0.1:5555");

    /* 암호화된 통신 */
    zlink_send(client, "Secure Hello", 12, 0);

    char buf[256];
    int size = zlink_recv(server, buf, sizeof(buf), 0);
    buf[size] = '\0';
    printf("수신: %s\n", buf);

    zlink_close(client);
    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
```

### WSS STREAM 서버

```c
void *ctx = zlink_ctx_new();

/* WSS 서버 (STREAM) */
void *server = zlink_socket(ctx, ZLINK_STREAM);
zlink_setsockopt(server, ZLINK_TLS_CERT, "server.crt", 0);
zlink_setsockopt(server, ZLINK_TLS_KEY, "server.key", 0);
int linger = 0;
zlink_setsockopt(server, ZLINK_LINGER, &linger, sizeof(linger));
zlink_bind(server, "wss://*:8443");

/* WSS 클라이언트 (STREAM) */
void *client = zlink_socket(ctx, ZLINK_STREAM);
int trust = 0;
zlink_setsockopt(client, ZLINK_TLS_TRUST_SYSTEM, &trust, sizeof(trust));
zlink_setsockopt(client, ZLINK_TLS_CA, "ca.crt", 0);
zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);
zlink_setsockopt(client, ZLINK_LINGER, &linger, sizeof(linger));

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);
zlink_connect(client, endpoint);

/* 연결 이벤트 수신 후 암호화된 데이터 교환 */
unsigned char server_id[4], client_id[4], code;
zlink_recv(server, server_id, 4, 0);
zlink_recv(server, &code, 1, 0);  /* 0x01 = 연결 */

zlink_recv(client, client_id, 4, 0);
zlink_recv(client, &code, 1, 0);  /* 0x01 = 연결 */

/* 데이터 전송 */
zlink_send(client, client_id, 4, ZLINK_SNDMORE);
zlink_send(client, "secure data", 11, 0);

zlink_close(client);
zlink_close(server);
zlink_ctx_term(ctx);
```
