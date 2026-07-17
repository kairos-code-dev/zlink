[English](05-tls-security.md) | [한국어](05-tls-security.ko.md)

<!-- zlink-nav:start -->
[← Transport](04-transports.ko.md) | [모니터링 →](06-monitoring.ko.md)
<!-- zlink-nav:end -->

# TLS/SSL 설정 및 보안 가이드

## 1. 개요

zlink는 OpenSSL로 `tls://`와 `wss://` transport를 네이티브 지원한다.
외부 프록시 없이 암호화된 통신을 직접 구성할 수 있다.

SPOT 서비스에서 TLS/WSS 설정은 노드 소유자(node owner)의 책임이다.
`zlink_set_tls_server()` / `zlink_set_tls_client()`는 bind/connect 전에
`MeshNode` 핸들에 적용한다. `Spot` facade 및 하위 pub/sub
핸들에는 TLS 설정을 적용할 수 없으며, 적용을 시도하면 `ZLINK_CONFIG_NOT_SUPPORTED`를 반환한다.

## 2. TLS 서버 설정

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);

/* Set certificate and key (before bind) */
zlink_set_tls_server(socket, "/path/to/server.crt", "/path/to/server.key", 0);

/* TLS bind */
zlink_bind(socket, "tls://*:5555");
```

## 3. TLS 클라이언트 설정

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);

/* Set CA certificate and hostname verification */
zlink_set_tls_client(socket, "/path/to/ca.crt", "server.example.com", 0);

/* TLS connect */
zlink_connect(socket, "tls://server.example.com:5555");
```

## 4. WSS (WebSocket + TLS) 설정

WSS는 ws에 TLS 암호화를 더한 transport다. ws보다 설정이 더 필요하다.

### WSS 서버

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_STREAM);

/* Set TLS certificate/key */
zlink_set_tls_server(socket, "/path/to/cert.pem", "/path/to/key.pem", 0);

/* WSS bind */
zlink_bind(socket, "wss://*:8443");
```

### WSS 클라이언트

`STREAM` 소켓은 bind 전용이라 `STREAM` 기반 WSS 서버에는 **외부** raw
WebSocket/TLS 클라이언트가 접속한다. 일반 zlink ZMP 소켓 타입(PAIR/DEALER 등)은
`zlink_set_tls_client()`로 `wss://` endpoint에 직접 **connect**할 수 있다:

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_set_tls_client(socket, "ca.crt", "server.example.com", 0);
zlink_connect(socket, "wss://server:8443");
```

### ws vs wss 설정 비교

| 설정 | ws | wss |
|------|:--:|:---:|
| 기본 소켓 생성 | O | O |
| `zlink_set_tls_server()` (서버 cert+key) | - | 필수 |
| `zlink_set_tls_client()` (클라이언트 CA+hostname+trust) | - | 사설 CA·hostname override·시스템 CA 비활성화가 필요할 때 (기본은 시스템 CA + endpoint hostname 검증) |

## 5. TLS API 상세

TLS 기본 설정은 두 개의 전용 함수로 하고, 고급 설정은 `ZLINK_OPT_TLS_*` 옵션(`zlink_set_option()`)을 쓴다.

### zlink_set_tls_server()

서버 측 TLS 인증서와 키를 설정한다.

```c
zlink_set_tls_server(socket, cert_path, key_path, require_client_cert);
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `cert_path` | string | 인증서 파일 경로 (PEM 형식) |
| `key_path` | string | 개인키 파일 경로 (PEM 형식) |
| `require_client_cert` | int | 클라이언트 인증서 요구(mTLS) 여부 (0 = 아니오, 1 = 예). raw socket TLS 경로에 적용되며, MeshNode TLS 경로는 이 인자를 사용하지 않는다 |

```c
/* PEM format file paths */
zlink_set_tls_server(socket, "server.crt", "server.key", 0);
```

- 반드시 `zlink_bind()` **이전에** 설정
- PEM 형식만 지원
- 인증서와 키가 일치하지 않으면 핸드셰이크 실패

### zlink_set_tls_client()

클라이언트 측 TLS CA 인증서, 호스트명 검증, 시스템 CA 신뢰를 설정한다.

```c
zlink_set_tls_client(socket, ca_cert_path, hostname, trust_system);
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `ca_cert_path` | string | CA 인증서 경로 (서버 인증서 검증). 시스템 스토어만 쓰려면 `""` 전달 |
| `hostname` | string | 서버 호스트명 (CN/SAN 검증). non-NULL 필수, 빈 문자열이면 connect endpoint host로 검증 |
| `trust_system` | int | 시스템 CA 스토어 신뢰 여부 (0 = 아니오, 1 = 예) |

```c
/* Private CA with hostname verification */
zlink_set_tls_client(socket, "ca.crt", "server.example.com", 0);

/* 시스템 CA만 사용(빈 CA 경로), 호스트명은 여전히 검증 */
zlink_set_tls_client(socket, "", "server.example.com", 1);
```

- raw 소켓에서 `ca_cert_path`와 `hostname`은 non-`NULL` 문자열이어야 한다.
  `NULL`을 넘기면 `ZLINK_CONFIG_INVALID_HANDLE`(`EFAULT`)로 거부된다.
- 시스템 CA 스토어만 쓰려면 `ca_cert_path`에 빈 문자열 `""`을 넘긴다
  (`trust_system=1`). 사설 CA를 추가하려면 경로를 지정한다.
- `hostname`은 CN/SAN 검증에 쓰이며 인증서와 일치해야 한다.
  프로덕션에서는 호스트명 검증을 반드시 권장한다.

> 참고: `core/tests/integration/test_asio_ssl.cpp` — TLS 인증서/사설 CA 설정 테스트

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

## 7. 일반적 TLS 에러 및 문제 해결

### 인증서/키 불일치

```
증상: bind 또는 핸드셰이크 실패
원인: 서버 인증서와 개인키가 일치하지 않음
해결: 인증서-키 쌍을 검증
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
해결: zlink_set_tls_client()에 ca_cert_path를 설정하거나 trust_system 파라미터 확인
```

### 호스트명 불일치

```
증상: 핸드셰이크 실패
원인: zlink_set_tls_client()의 hostname 파라미터가 인증서 CN/SAN과 불일치
해결: 인증서에 올바른 CN/SAN을 포함하거나 hostname 파라미터를 수정
```

### 인증서 만료

```
증상: 핸드셰이크 실패
원인: 서버 또는 CA 인증서 유효기간 만료
해결: 인증서 갱신
```

```bash
# Check certificate validity period
openssl x509 -noout -dates -in server.crt
```

### 모니터링으로 TLS 에러 감지

```c
void on_tls_error(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("Handshake failed: event=0x%llx value=%llu\n",
           (unsigned long long)ev->event,
           (unsigned long long)ev->value);
}

zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL |
              ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL |
              ZLINK_EVENT_HANDSHAKE_FAILED_AUTH,
};
void *mon = zlink_socket_monitor_open(socket, &opts);
zlink_socket_monitor_handler(mon, on_tls_error, NULL);
```

## 8. 운영 환경 체크리스트

### 인증서 관리

- [ ] TLS 1.2 이상 사용 (zlink는 TLS 1.2 server/client context를 생성)
- [ ] 프로덕션에서 공인 CA 인증서 사용
- [ ] 인증서 만료 전 자동 갱신 프로세스 구축
- [ ] 개인키 파일 권한 제한 (`chmod 600`)
- [ ] 인증서 체인 완전성 확인

### 클라이언트 설정

- [ ] `zlink_set_tls_client()`의 `hostname` 파라미터 설정 (호스트명 검증 활성화)
- [ ] `zlink_set_tls_client()`의 `ca_cert_path` 명시적 설정 또는 시스템 CA 확인
- [ ] 사설 CA 사용 시 `zlink_set_tls_client()`의 `trust_system=0`

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

    /* TLS Server */
    void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_set_tls_server(server, "server.crt", "server.key", 0);
    zlink_bind(server, "tls://*:5555");

    /* TLS Client */
    void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_set_tls_client(client, "ca.crt", "localhost", 0);
    zlink_connect(client, "tls://127.0.0.1:5555");

    /* 암호화된 통신 — server는 zlink_recv()로 수신 (PAIR은 recv handler 미지원) */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 12);
    memcpy(zlink_msg_data(&part), "Secure Hello", 12);
    zlink_send(client, &part, 1, 0);

    /* server측 zlink_recv()가 parts[0] = "Secure Hello"를 받는다 */

    zlink_close(client);
    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
```

### WSS STREAM 서버

```c
void *ctx = zlink_ctx_new();

/* WSS Server (STREAM) */
void *server = zlink_socket(ctx, ZLINK_SOCKET_STREAM);
zlink_set_tls_server(server, "server.crt", "server.key", 0);
int linger = 0;
zlink_set_option(server, ZLINK_OPT_LINGER, &linger, sizeof(linger));
zlink_bind(server, "wss://*:8443");

/* External raw WSS client connects to this endpoint.
 * STREAM server receives [routing_id][0x01] and then data frames.
 */

zlink_close(server);
zlink_ctx_term(ctx);
```

---
<!-- zlink-nav:bottom:start -->
[← Transport](04-transports.ko.md) | [모니터링 →](06-monitoring.ko.md)
<!-- zlink-nav:bottom:end -->
