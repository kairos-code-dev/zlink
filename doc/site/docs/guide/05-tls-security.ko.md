
# TLS/SSL 설정 및 보안 가이드

## 1. 개요

zlink는 OpenSSL을 통해 `tls://`와 `wss://` transport를 네이티브 지원한다.
외부 프록시 없이 암호화된 통신을 직접 구성할 수 있다.

SPOT 서비스에서 TLS/WSS 설정은 node owner 책임이다.
`zlink_set_tls_server()` / `zlink_set_tls_client()`는 bind/connect 전에
`SpotNode` handle에 적용해야 한다. unified `spot`과 SPOT child pub/sub
handle은 TLS 설정 surface가 아니며 `ENOTSUP`로 실패한다.

## 2. TLS 서버 설정

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_ROUTER);

    /* 인증서 및 키 설정 (bind 전) */
    zlink_set_tls_server(socket, "/path/to/server.crt", "/path/to/server.key", 0);

    /* TLS 바인드 */
    zlink_bind(socket, "tls://*:5555");
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t socket(ctx);
    socket.set_tls_server("/path/to/server.crt", "/path/to/server.key", 0);
    socket.bind("tls://*:5555");
    ```

=== "Java"

    ```java
    RouterSocket socket = new RouterSocket(ctx);
    socket.setTlsServer("/path/to/server.crt", "/path/to/server.key", 0);
    socket.bind("tls://*:5555");
    ```

=== "Python"

    ```python
    socket = zlink.RouterSocket(ctx)
    socket.set_tls_server("/path/to/server.crt", "/path/to/server.key", 0)
    socket.bind("tls://*:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.RouterSocket(ctx);
    socket.setTlsServer("/path/to/server.crt", "/path/to/server.key", 0);
    socket.bind("tls://*:5555");
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new RouterSocket(ctx);
    socket.SetTlsServer("/path/to/server.crt", "/path/to/server.key", 0);
    socket.Bind("tls://*:5555");
    ```

=== "Rust"

    ```rust
    let socket = ctx.router_socket()?;
    socket.set_tls_server("/path/to/server.crt", "/path/to/server.key", false)?;
    socket.bind("tls://*:5555")?;
    ```

=== "Go"

    ```go
    socket, err := ctx.RouterSocket()
    if err != nil { log.Fatal(err) }
    socket.SetTLSServer("/path/to/server.crt", "/path/to/server.key", false)
    socket.Bind("tls://*:5555")
    ```

## 3. TLS 클라이언트 설정

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_DEALER);

    /* CA 인증서 및 호스트명 검증 설정 */
    zlink_set_tls_client(socket, "/path/to/ca.crt", "server.example.com", 0);

    /* TLS 연결 */
    zlink_connect(socket, "tls://server.example.com:5555");
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t socket(ctx);
    socket.set_tls_client("/path/to/ca.crt", "server.example.com", 0);
    socket.connect("tls://server.example.com:5555");
    ```

=== "Java"

    ```java
    DealerSocket socket = new DealerSocket(ctx);
    socket.setTlsClient("/path/to/ca.crt", "server.example.com", 0);
    socket.connect("tls://server.example.com:5555");
    ```

=== "Python"

    ```python
    socket = zlink.DealerSocket(ctx)
    socket.set_tls_client("/path/to/ca.crt", "server.example.com", 0)
    socket.connect("tls://server.example.com:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.DealerSocket(ctx);
    socket.setTlsClient("/path/to/ca.crt", "server.example.com", 0);
    socket.connect("tls://server.example.com:5555");
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new DealerSocket(ctx);
    socket.SetTlsClient("/path/to/ca.crt", "server.example.com", 0);
    socket.Connect("tls://server.example.com:5555");
    ```

=== "Rust"

    ```rust
    let socket = ctx.dealer_socket()?;
    socket.set_tls_client("/path/to/ca.crt", "server.example.com", false)?;
    socket.connect("tls://server.example.com:5555")?;
    ```

=== "Go"

    ```go
    socket, err := ctx.DealerSocket()
    if err != nil { log.Fatal(err) }
    socket.SetTLSClient("/path/to/ca.crt", "server.example.com", false)
    socket.Connect("tls://server.example.com:5555")
    ```

## 4. WSS (WebSocket + TLS) 설정

WSS는 ws에 TLS 암호화를 추가한 transport이다. ws 대비 추가 설정이 필요하다.

### WSS 서버

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_STREAM);

    /* TLS 인증서/키 설정 */
    zlink_set_tls_server(socket, "/path/to/cert.pem", "/path/to/key.pem", 0);

    /* WSS 바인드 */
    zlink_bind(socket, "wss://*:8443");
    ```

=== "C++"

    ```cpp
    zlink::stream_socket_t socket(ctx);
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("wss://*:8443");
    ```

=== "Java"

    ```java
    StreamSocket socket = new StreamSocket(ctx);
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("wss://*:8443");
    ```

=== "Python"

    ```python
    socket = zlink.StreamSocket(ctx)
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0)
    socket.bind("wss://*:8443")
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.StreamSocket(ctx);
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("wss://*:8443");
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new StreamSocket(ctx);
    socket.SetTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.Bind("wss://*:8443");
    ```

=== "Rust"

    ```rust
    let socket = ctx.stream_socket()?;
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", false)?;
    socket.bind("wss://*:8443")?;
    ```

=== "Go"

    ```go
    socket, err := ctx.StreamSocket()
    if err != nil { log.Fatal(err) }
    socket.SetTLSServer("/path/to/cert.pem", "/path/to/key.pem", false)
    socket.Bind("wss://*:8443")
    ```

### WSS 클라이언트 (외부 Raw 클라이언트)

`ZLINK_STREAM`은 서버 전용이므로, WSS 클라이언트는 외부 WebSocket/TLS 클라이언트 스택을 사용해야 한다.

개념 예시:

```text
대상: wss://server:8443
- CA 신뢰: /path/to/ca.pem
- 호스트명 검증: localhost
```

### ws vs wss 설정 비교

| 설정 | ws | wss |
|------|:--:|:---:|
| 기본 소켓 생성 | O | O |
| `zlink_set_tls_server()` (서버 cert+key) | - | 필수 |
| `zlink_set_tls_client()` (클라이언트 CA+hostname+trust) | - | 권장 (외부 raw client) |

## 5. TLS API 상세

TLS는 개별 소켓 옵션 대신 두 개의 전용 함수로 설정한다.

### zlink_set_tls_server()

서버 측 TLS 인증서와 키를 설정한다.

=== "C"

    ```c
    zlink_set_tls_server(socket, cert_path, key_path, require_client_cert);
    ```

=== "C++"

    ```cpp
    socket.set_tls_server(cert_path, key_path, require_client_cert);
    ```

=== "Java"

    ```java
    socket.setTlsServer(certPath, keyPath, requireClientCert);
    ```

=== "Python"

    ```python
    socket.set_tls_server(cert_path, key_path, require_client_cert)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer(certPath, keyPath, requireClientCert);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer(certPath, keyPath, requireClientCert);
    ```

=== "Rust"

    ```rust
    socket.set_tls_server(cert_path, key_path, require_client_cert)?;
    ```

=== "Go"

    ```go
    socket.SetTLSServer(certPath, keyPath, requireClientCert)
    ```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `cert_path` | string | 인증서 파일 경로 (PEM 형식) |
| `key_path` | string | 개인키 파일 경로 (PEM 형식) |
| `require_client_cert` | int | 클라이언트 인증서 요구 여부 (0 = 아니오, 1 = 예) |

=== "C"

    ```c
    /* PEM 형식 파일 경로 */
    zlink_set_tls_server(socket, "server.crt", "server.key", 0);
    ```

=== "C++"

    ```cpp
    socket.set_tls_server("server.crt", "server.key", 0);
    ```

=== "Java"

    ```java
    socket.setTlsServer("server.crt", "server.key", 0);
    ```

=== "Python"

    ```python
    socket.set_tls_server("server.crt", "server.key", 0)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer("server.crt", "server.key", 0);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer("server.crt", "server.key", 0);
    ```

=== "Rust"

    ```rust
    socket.set_tls_server("server.crt", "server.key", false)?;
    ```

=== "Go"

    ```go
    socket.SetTLSServer("server.crt", "server.key", false)
    ```

- 반드시 `zlink_bind()` **이전에** 설정
- PEM 형식만 지원
- 인증서와 키가 일치하지 않으면 핸드셰이크 실패

### zlink_set_tls_client()

클라이언트 측 TLS CA 인증서, 호스트명 검증, 시스템 CA 신뢰를 설정한다.

=== "C"

    ```c
    zlink_set_tls_client(socket, ca_cert_path, hostname, trust_system);
    ```

=== "C++"

    ```cpp
    socket.set_tls_client(ca_cert_path, hostname, trust_system);
    ```

=== "Java"

    ```java
    socket.setTlsClient(caCertPath, hostname, trustSystem);
    ```

=== "Python"

    ```python
    socket.set_tls_client(ca_cert_path, hostname, trust_system)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsClient(caCertPath, hostname, trustSystem);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsClient(caCertPath, hostname, trustSystem);
    ```

=== "Rust"

    ```rust
    socket.set_tls_client(ca_cert_path, hostname, trust_system)?;
    ```

=== "Go"

    ```go
    socket.SetTLSClient(caCertPath, hostname, trustSystem)
    ```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `ca_cert_path` | string | CA 인증서 경로 (서버 인증서 검증), 또는 NULL |
| `hostname` | string | 서버 호스트명 (CN/SAN 검증), 또는 NULL |
| `trust_system` | int | 시스템 CA 스토어 신뢰 여부 (0 = 아니오, 1 = 예) |

=== "C"

    ```c
    /* 사설 CA + 호스트명 검증 */
    zlink_set_tls_client(socket, "ca.crt", "server.example.com", 0);

    /* 시스템 CA만 사용 (사설 CA 없음, 호스트명 미검증) */
    zlink_set_tls_client(socket, NULL, NULL, 1);
    ```

=== "C++"

    ```cpp
    // 사설 CA + 호스트명 검증
    socket.set_tls_client("ca.crt", "server.example.com", 0);

    // 시스템 CA만 사용
    socket.set_tls_client("", "", true);
    ```

=== "Java"

    ```java
    // 사설 CA + 호스트명 검증
    socket.setTlsClient("ca.crt", "server.example.com", 0);

    // 시스템 CA만 사용
    socket.setTlsClient(null, null, true);
    ```

=== "Python"

    ```python
    # 사설 CA + 호스트명 검증
    socket.set_tls_client("ca.crt", "server.example.com", 0)

    # 시스템 CA만 사용
    socket.set_tls_client(None, None, True)
    ```

=== "Node/TypeScript"

    ```typescript
    // 사설 CA + 호스트명 검증
    socket.setTlsClient("ca.crt", "server.example.com", 0);

    // 시스템 CA만 사용
    socket.setTlsClient(null, null, true);
    ```

=== "C#/.NET"

    ```csharp
    // 사설 CA + 호스트명 검증
    socket.SetTlsClient("ca.crt", "server.example.com", 0);

    // 시스템 CA만 사용
    socket.SetTlsClient(null, null, true);
    ```

=== "Rust"

    ```rust
    // 사설 CA + 호스트명 검증
    socket.set_tls_client("ca.crt", "server.example.com", false)?;

    // 시스템 CA만 사용
    socket.set_tls_client(None, None, true)?;
    ```

=== "Go"

    ```go
    // Private CA with hostname verification
    socket.SetTLSClient("ca.crt", "server.example.com", false)

    // System CA only
    socket.SetTLSClient("", "", true)
    ```

- `ca_cert_path`가 NULL이면 시스템 CA 스토어만 사용 (`trust_system=1`인 경우)
- 사설 CA 사용 시 반드시 `ca_cert_path` 설정
- `hostname`이 NULL이면 호스트명 검증 생략 (보안 경고)
- 프로덕션에서는 호스트명 검증 반드시 권장
- 인증서의 CN 또는 SAN과 일치해야 함

> 참고: `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp` -- `trust_system = 0` 설정 후 사설 CA 사용

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
해결: zlink_set_tls_client()의 ca_cert_path 설정 또는 trust_system 파라미터 확인
```

### 호스트명 불일치

```
증상: 핸드셰이크 실패
원인: zlink_set_tls_client()의 hostname 파라미터와 인증서 CN/SAN 불일치
해결: 인증서에 올바른 CN/SAN 포함, 또는 hostname 파라미터 수정
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

!!! note "C-only: 모니터 이벤트 API"
    모니터 이벤트 API는 현재 C 인터페이스로만 노출된다.
    각 바인딩은 자체 이벤트/콜백 메커니즘으로 동등한 모니터링을 제공한다.

```c
void on_tls_error(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("핸드셰이크 실패: event=0x%llx value=%llu\n",
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

- [ ] TLS 1.2 이상 사용 (OpenSSL 기본 설정)
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

=== "C"

    ```c
    #include <zlink.h>
    #include <stdio.h>
    #include <string.h>

    int main(void) {
        void *ctx = zlink_ctx_new();

        /* TLS 서버 */
        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_set_tls_server(server, "server.crt", "server.key", 0);
        zlink_bind(server, "tls://*:5555");

        /* TLS 클라이언트 */
        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_set_tls_client(client, "ca.crt", "localhost", 0);
        zlink_connect(client, "tls://127.0.0.1:5555");

        /* 암호화된 통신 — 서버는 핸들러 콜백으로 수신 */
        zlink_msg_t part;
        zlink_msg_init_size(&part, 12);
        memcpy(zlink_msg_data(&part), "Secure Hello", 12);
        zlink_send(client, &part, 1, 0);

        /* on_message 콜백 수신: parts[0] = "Secure Hello" */

        zlink_close(client);
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>

    int main() {
        zlink::context_t ctx;

        // TLS 서버
        zlink::pair_socket_t server(ctx);
        server.set_tls_server("server.crt", "server.key", 0);
        server.bind("tls://*:5555");

        // TLS 클라이언트
        zlink::pair_socket_t client(ctx);
        client.set_tls_client("ca.crt", "localhost", 0);
        client.connect("tls://127.0.0.1:5555");

        // 암호화된 통신
        client.send(zlink::message_t("Secure Hello", 12));

        auto [rid, parts] = server.recv();
        std::cout << "수신: " << parts[0].to_string() << "\n";

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class TlsExample {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                PairSocket server = new PairSocket(ctx);
                server.setTlsServer("server.crt", "server.key", 0);
                server.bind("tls://*:5555");

                PairSocket client = new PairSocket(ctx);
                client.setTlsClient("ca.crt", "localhost", 0);
                client.connect("tls://127.0.0.1:5555");

                client.send(new Message("Secure Hello".getBytes()));

                RecvResult result = server.recv();
                System.out.println("수신: "
                    + new String(result.parts()[0].data()));
            }
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    server = zlink.PairSocket(ctx)
    server.set_tls_server("server.crt", "server.key", 0)
    server.bind("tls://*:5555")

    client = zlink.PairSocket(ctx)
    client.set_tls_client("ca.crt", "localhost", 0)
    client.connect("tls://127.0.0.1:5555")

    client.send(b"Secure Hello")

    rid, parts = server.recv()
    print(f"수신: {parts[0].data().decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from "zlink";

    const ctx = new zlink.Context();

    const server = new zlink.PairSocket(ctx);
    server.setTlsServer("server.crt", "server.key", 0);
    server.bind("tls://*:5555");

    const client = new zlink.PairSocket(ctx);
    client.setTlsClient("ca.crt", "localhost", 0);
    client.connect("tls://127.0.0.1:5555");

    client.send(Buffer.from("Secure Hello"));

    const { sourceRid, parts } = server.recv();
    console.log(`수신: ${parts[0].data().toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    using var server = new PairSocket(ctx);
    server.SetTlsServer("server.crt", "server.key", 0);
    server.Bind("tls://*:5555");

    using var client = new PairSocket(ctx);
    client.SetTlsClient("ca.crt", "localhost", 0);
    client.Connect("tls://127.0.0.1:5555");

    client.Send(new Message("Secure Hello"u8));

    var (rid, parts) = server.Recv();
    Console.WriteLine($"수신: {parts[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> zlink::Result<()> {
        let ctx = Context::new()?;

        let server = ctx.pair_socket()?;
        server.set_tls_server("server.crt", "server.key", false)?;
        server.bind("tls://*:5555")?;

        let client = ctx.pair_socket()?;
        client.set_tls_client("ca.crt", "localhost", false)?;
        client.connect("tls://127.0.0.1:5555")?;

        client.send(&zlink::Message::from("Secure Hello"))?;

        let (rid, parts) = server.recv()?;
        println!("수신: {}", parts[0].as_str()?);

        Ok(())
    }
    ```

=== "Go"

    ```go
    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }

        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        server.SetTLSServer("server.crt", "server.key", false)
        server.Bind("tls://*:5555")

        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        client.SetTLSClient("ca.crt", "localhost", false)
        client.Connect("tls://127.0.0.1:5555")

        client.Send(zlink.NewMessage([]byte("Secure Hello")))

        rid, parts, _ := server.Recv()
        _ = rid
        fmt.Printf("Received: %s\n", string(parts[0].Data()))
    }
    ```

### WSS STREAM 서버

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* WSS 서버 (STREAM) */
    void *server = zlink_socket(ctx, ZLINK_STREAM);
    zlink_set_tls_server(server, "server.crt", "server.key", 0);
    int linger = 0;
    zlink_set_option(server, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    zlink_bind(server, "wss://*:8443");

    /* 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
     * STREAM 서버는 [routing_id][0x01] 이벤트 수신 후 데이터 프레임을 처리한다.
     */

    zlink_close(server);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    zlink::context_t ctx;

    zlink::stream_socket_t server(ctx);
    server.set_tls_server("server.crt", "server.key", 0);
    server.set_option(zlink::opt::linger, 0);
    server.bind("wss://*:8443");

    // 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
    ```

=== "Java"

    ```java
    Context ctx = new Context();

    StreamSocket server = new StreamSocket(ctx);
    server.setTlsServer("server.crt", "server.key", 0);
    server.setLinger(0);
    server.bind("wss://*:8443");

    // 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    server = zlink.StreamSocket(ctx)
    server.set_tls_server("server.crt", "server.key", 0)
    server.set_option(zlink.OPT_LINGER, 0)
    server.bind("wss://*:8443")

    # 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    const server = new zlink.StreamSocket(ctx);
    server.setTlsServer("server.crt", "server.key", 0);
    server.setOption(zlink.OPT_LINGER, 0);
    server.bind("wss://*:8443");

    // 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new Context();

    using var server = new StreamSocket(ctx);
    server.SetTlsServer("server.crt", "server.key", 0);
    server.Linger = 0;
    server.Bind("wss://*:8443");

    // 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;

    let server = ctx.stream_socket()?;
    server.set_tls_server("server.crt", "server.key", false)?;
    server.set_linger(0)?;
    server.bind("wss://*:8443")?;

    // 외부 raw WSS 클라이언트가 이 엔드포인트로 접속한다.
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }

    server, err := ctx.StreamSocket()
    if err != nil { log.Fatal(err) }
    server.SetTLSServer("server.crt", "server.key", false)
    server.SetOption(zlink.OptionLinger, 0)
    server.Bind("wss://*:8443")

    // External raw WSS client connects to this endpoint.
    ```

---
[← Transport](04-transports.ko.md) | [모니터링 →](06-monitoring.ko.md)
