
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

### 소켓별 Transport 지원

| Transport | PAIR | PUB/SUB | DEALER | ROUTER | STREAM |
|-----------|:----:|:-------:|:------:|:------:|:------:|
| tcp       |  O   |    O    |   O    |   O    | O (bind) |
| ipc       |  O   |    O    |   O    |   O    |   -    |
| inproc    |  O   |    O    |   O    |   O    |   -    |
| tls       |  O   |    O    |   O    |   O    | O (bind) |
| ws        |  O   |    O    |   O    |   O    | O (bind) |
| wss       |  O   |    O    |   O    |   O    | O (bind) |

- STREAM은 **bind만** 지원하며, 클라이언트는 raw socket/websocket으로 구현한다.
- STREAM은 ipc/inproc을 지원하지 않는다.

## 2. TCP

표준 TCP/IP 네트워크 통신.

### 기본 사용법

=== "C"

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

=== "C++"

    ```cpp
    socket.bind("tcp://192.168.1.10:5555");
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.connect("tcp://server.example.com:5555");
    ```

=== "Java"

    ```java
    socket.bind("tcp://192.168.1.10:5555");
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.connect("tcp://server.example.com:5555");
    ```

=== "Python"

    ```python
    socket.bind("tcp://192.168.1.10:5555")
    socket.bind("tcp://*:5555")
    socket.connect("tcp://127.0.0.1:5555")
    socket.connect("tcp://server.example.com:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://192.168.1.10:5555");
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.connect("tcp://server.example.com:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://192.168.1.10:5555");
    socket.Bind("tcp://*:5555");
    socket.Connect("tcp://127.0.0.1:5555");
    socket.Connect("tcp://server.example.com:5555");
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://192.168.1.10:5555")?;
    socket.bind("tcp://*:5555")?;
    socket.connect("tcp://127.0.0.1:5555")?;
    socket.connect("tcp://server.example.com:5555")?;
    ```

=== "Go"

    ```go
    socket.Bind("tcp://192.168.1.10:5555")
    socket.Bind("tcp://*:5555")
    socket.Connect("tcp://127.0.0.1:5555")
    socket.Connect("tcp://server.example.com:5555")
    ```

### 와일드카드 포트 (자동 할당)

OS가 사용 가능한 포트를 자동 할당한다. 테스트나 동적 포트 환경에서 유용하다.

=== "C"

    ```c
    /* 포트 0 또는 * 사용 */
    zlink_bind(socket, "tcp://127.0.0.1:*");

    /* 할당된 엔드포인트 조회 */
    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    /* endpoint = "tcp://127.0.0.1:53821" (예시) */

    /* 조회된 엔드포인트로 연결 */
    zlink_connect(other_socket, endpoint);
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://127.0.0.1:*");
    std::string endpoint = socket.last_endpoint();
    other_socket.connect(endpoint);
    ```

=== "Java"

    ```java
    socket.bind("tcp://127.0.0.1:*");
    String endpoint = socket.lastEndpoint();
    otherSocket.connect(endpoint);
    ```

=== "Python"

    ```python
    socket.bind("tcp://127.0.0.1:*")
    endpoint = socket.last_endpoint()
    other_socket.connect(endpoint)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://127.0.0.1:*");
    const endpoint = socket.lastEndpoint();
    otherSocket.connect(endpoint);
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://127.0.0.1:*");
    var endpoint = socket.LastEndpoint;
    otherSocket.Connect(endpoint);
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://127.0.0.1:*")?;
    let endpoint = socket.last_endpoint()?;
    other_socket.connect(&endpoint)?;
    ```

=== "Go"

    ```go
    socket.Bind("tcp://127.0.0.1:*")
    endpoint := socket.LastEndpoint()
    otherSocket.Connect(endpoint)
    ```

> 참고: `core/tests/test_pair_tcp.cpp` -- `bind_loopback_ipv4()` 와일드카드 바인드 패턴

### DNS 이름 사용

connect 시 호스트명을 사용하면 내부적으로 DNS 리졸빙이 수행된다.

=== "C"

    ```c
    /* DNS 이름으로 연결 */
    zlink_connect(socket, "tcp://localhost:5555");
    ```

=== "C++"

    ```cpp
    socket.connect("tcp://localhost:5555");
    ```

=== "Java"

    ```java
    socket.connect("tcp://localhost:5555");
    ```

=== "Python"

    ```python
    socket.connect("tcp://localhost:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.connect("tcp://localhost:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.Connect("tcp://localhost:5555");
    ```

=== "Rust"

    ```rust
    socket.connect("tcp://localhost:5555")?;
    ```

=== "Go"

    ```go
    socket.Connect("tcp://localhost:5555")
    ```

> 주의: DNS 리졸빙은 블로킹으로 수행된다. 프로덕션에서는 IP 주소 사용을 권장한다.
> 참고: `core/tests/test_pair_tcp.cpp` -- `test_pair_tcp_connect_by_name()`

### 에러 처리

=== "C"

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

=== "C++"

    ```cpp
    try {
        socket.bind("tcp://*:5555");
    } catch (const zlink::error_t& e) {
        // 포트 5555 이미 사용 중
    }

    try {
        socket.connect("tcp://invalid:99999");
    } catch (const zlink::error_t& e) {
        std::cerr << "연결 실패: " << e.what() << "\n";
    }
    ```

=== "Java"

    ```java
    try {
        socket.bind("tcp://*:5555");
    } catch (ZlinkException e) {
        // 포트 5555 이미 사용 중
    }

    try {
        socket.connect("tcp://invalid:99999");
    } catch (ZlinkException e) {
        System.err.println("연결 실패: " + e.getMessage());
    }
    ```

=== "Python"

    ```python
    try:
        socket.bind("tcp://*:5555")
    except zlink.ZlinkError:
        pass  # 포트 5555 이미 사용 중

    try:
        socket.connect("tcp://invalid:99999")
    except zlink.ZlinkError as e:
        print(f"연결 실패: {e}")
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.bind("tcp://*:5555");
    } catch (e) {
        // 포트 5555 이미 사용 중
    }

    try {
        socket.connect("tcp://invalid:99999");
    } catch (e) {
        console.error(`연결 실패: ${e}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Bind("tcp://*:5555");
    } catch (ZlinkException) {
        // 포트 5555 이미 사용 중
    }

    try {
        socket.Connect("tcp://invalid:99999");
    } catch (ZlinkException e) {
        Console.Error.WriteLine($"연결 실패: {e.Message}");
    }
    ```

=== "Rust"

    ```rust
    if let Err(e) = socket.bind("tcp://*:5555") {
        // 포트 5555 이미 사용 중
    }

    if let Err(e) = socket.connect("tcp://invalid:99999") {
        eprintln!("연결 실패: {}", e);
    }
    ```

=== "Go"

    ```go
    if err := socket.Bind("tcp://*:5555"); err != nil {
        // Port 5555 already in use
    }

    if err := socket.Connect("tcp://invalid:99999"); err != nil {
        fmt.Printf("Connection failed: %v\n", err)
    }
    ```

### 특성

- **TCP_NODELAY** 활성화 (Nagle 알고리즘 비활성화)
- **Speculative write** -- 동기 쓰기 먼저 시도 후 실패 시 비동기 전환
- **Gather write** -- 헤더와 바디를 한번에 전송 (시스템콜 감소)

> Speculative write 등 내부 최적화 상세는 [architecture.md](../internals/architecture.ko.md)를 참고.

## 3. IPC

Unix 도메인 소켓 기반 로컬 프로세스 간 통신.

### 기본 사용법

=== "C"

    ```c
    /* 서버 */
    zlink_bind(socket, "ipc:///tmp/myapp.ipc");

    /* 클라이언트 */
    zlink_connect(socket, "ipc:///tmp/myapp.ipc");
    ```

=== "C++"

    ```cpp
    socket.bind("ipc:///tmp/myapp.ipc");
    socket.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Java"

    ```java
    socket.bind("ipc:///tmp/myapp.ipc");
    socket.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Python"

    ```python
    socket.bind("ipc:///tmp/myapp.ipc")
    socket.connect("ipc:///tmp/myapp.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("ipc:///tmp/myapp.ipc");
    socket.connect("ipc:///tmp/myapp.ipc");
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("ipc:///tmp/myapp.ipc");
    socket.Connect("ipc:///tmp/myapp.ipc");
    ```

=== "Rust"

    ```rust
    socket.bind("ipc:///tmp/myapp.ipc")?;
    socket.connect("ipc:///tmp/myapp.ipc")?;
    ```

=== "Go"

    ```go
    socket.Bind("ipc:///tmp/myapp.ipc")
    socket.Connect("ipc:///tmp/myapp.ipc")
    ```

### 와일드카드 바인드

=== "C"

    ```c
    /* IPC 와일드카드 — 임시 경로 자동 할당 */
    zlink_bind(socket, "ipc://*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    ```

=== "C++"

    ```cpp
    socket.bind("ipc://*");
    std::string endpoint = socket.last_endpoint();
    ```

=== "Java"

    ```java
    socket.bind("ipc://*");
    String endpoint = socket.lastEndpoint();
    ```

=== "Python"

    ```python
    socket.bind("ipc://*")
    endpoint = socket.last_endpoint()
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("ipc://*");
    const endpoint = socket.lastEndpoint();
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("ipc://*");
    var endpoint = socket.LastEndpoint;
    ```

=== "Rust"

    ```rust
    socket.bind("ipc://*")?;
    let endpoint = socket.last_endpoint()?;
    ```

=== "Go"

    ```go
    socket.Bind("ipc://*")
    endpoint := socket.LastEndpoint()
    ```

> 참고: `core/tests/test_router_multiple_dealers.cpp` -- `zlink_bind(router, "ipc://*")`

### 에러 처리

=== "C"

    ```c
    /* 경로가 너무 긴 경우 */
    int rc = zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
    if (rc == -1 && errno == ENAMETOOLONG) {
        printf("IPC 경로가 시스템 제한(108자)을 초과\n");
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (const zlink::error_t& e) {
        // IPC 경로가 시스템 제한(108자)을 초과
    }
    ```

=== "Java"

    ```java
    try {
        socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (ZlinkException e) {
        // IPC 경로가 시스템 제한(108자)을 초과
    }
    ```

=== "Python"

    ```python
    try:
        socket.bind("ipc:///very/long/path/.../endpoint.ipc")
    except zlink.ZlinkError:
        pass  # IPC 경로가 시스템 제한(108자)을 초과
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (e) {
        // IPC 경로가 시스템 제한(108자)을 초과
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (ZlinkException) {
        // IPC 경로가 시스템 제한(108자)을 초과
    }
    ```

=== "Rust"

    ```rust
    if let Err(e) = socket.bind("ipc:///very/long/path/.../endpoint.ipc") {
        // IPC 경로가 시스템 제한(108자)을 초과
    }
    ```

=== "Go"

    ```go
    if err := socket.Bind("ipc:///very/long/path/.../endpoint.ipc"); err != nil {
        // IPC path exceeds system limit (108 characters)
    }
    ```

> 참고: `core/tests/test_pair_ipc.cpp` -- `test_endpoint_too_long()`

### 특성

- **Linux/macOS에서만 지원** (Windows 미지원)
- TCP 대비 낮은 오버헤드 (네트워크 스택 우회)
- 파일 경로 기반 주소 (경로 최대 108자)

## 4. inproc

프로세스 내(in-process) 통신. 가장 빠른 transport.

### 기본 사용법

=== "C"

    ```c
    /* bind가 먼저 호출되어야 함 */
    zlink_bind(socket_a, "inproc://workers");
    zlink_connect(socket_b, "inproc://workers");
    ```

=== "C++"

    ```cpp
    socket_a.bind("inproc://workers");
    socket_b.connect("inproc://workers");
    ```

=== "Java"

    ```java
    socketA.bind("inproc://workers");
    socketB.connect("inproc://workers");
    ```

=== "Python"

    ```python
    socket_a.bind("inproc://workers")
    socket_b.connect("inproc://workers")
    ```

=== "Node/TypeScript"

    ```typescript
    socketA.bind("inproc://workers");
    socketB.connect("inproc://workers");
    ```

=== "C#/.NET"

    ```csharp
    socketA.Bind("inproc://workers");
    socketB.Connect("inproc://workers");
    ```

=== "Rust"

    ```rust
    socket_a.bind("inproc://workers")?;
    socket_b.connect("inproc://workers")?;
    ```

=== "Go"

    ```go
    socketA.Bind("inproc://workers")
    socketB.Connect("inproc://workers")
    ```

### 에러 처리

=== "C"

    ```c
    /* bind 없이 connect 시도 */
    int rc = zlink_connect(socket, "inproc://nonexistent");
    if (rc == -1) {
        printf("bind가 아직 없음\n");
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.connect("inproc://nonexistent");
    } catch (const zlink::error_t& e) {
        // bind가 아직 없음
    }
    ```

=== "Java"

    ```java
    try {
        socket.connect("inproc://nonexistent");
    } catch (ZlinkException e) {
        // bind가 아직 없음
    }
    ```

=== "Python"

    ```python
    try:
        socket.connect("inproc://nonexistent")
    except zlink.ZlinkError:
        pass  # bind가 아직 없음
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.connect("inproc://nonexistent");
    } catch (e) {
        // bind가 아직 없음
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Connect("inproc://nonexistent");
    } catch (ZlinkException) {
        // bind가 아직 없음
    }
    ```

=== "Rust"

    ```rust
    if let Err(e) = socket.connect("inproc://nonexistent") {
        // bind가 아직 없음
    }
    ```

=== "Go"

    ```go
    if err := socket.Connect("inproc://nonexistent"); err != nil {
        // No bind exists yet
    }
    ```

### 특성

- **동일 context 내에서만** 사용 가능
- **bind가 connect보다 먼저** 호출되어야 함
- Lock-free pipe 직접 연결 (네트워크 없음)
- 가장 낮은 지연시간, 가장 높은 처리량

> 참고: `core/tests/test_pair_inproc.cpp` -- bind -> connect -> bounce 패턴

## 5. WebSocket (ws)

웹 브라우저 및 외부 클라이언트 연동.

### 기본 사용법

=== "C"

    ```c
    /* 서버 */
    zlink_bind(socket, "ws://*:8080");

    /* 클라이언트 */
    zlink_connect(socket, "ws://server:8080");

    /* 와일드카드 포트 */
    zlink_bind(socket, "ws://127.0.0.1:*");
    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    ```

=== "C++"

    ```cpp
    socket.bind("ws://*:8080");
    socket.connect("ws://server:8080");
    socket.bind("ws://127.0.0.1:*");
    std::string endpoint = socket.last_endpoint();
    ```

=== "Java"

    ```java
    socket.bind("ws://*:8080");
    socket.connect("ws://server:8080");
    socket.bind("ws://127.0.0.1:*");
    String endpoint = socket.lastEndpoint();
    ```

=== "Python"

    ```python
    socket.bind("ws://*:8080")
    socket.connect("ws://server:8080")
    socket.bind("ws://127.0.0.1:*")
    endpoint = socket.last_endpoint()
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("ws://*:8080");
    socket.connect("ws://server:8080");
    socket.bind("ws://127.0.0.1:*");
    const endpoint = socket.lastEndpoint();
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("ws://*:8080");
    socket.Connect("ws://server:8080");
    socket.Bind("ws://127.0.0.1:*");
    var endpoint = socket.LastEndpoint;
    ```

=== "Rust"

    ```rust
    socket.bind("ws://*:8080")?;
    socket.connect("ws://server:8080")?;
    socket.bind("ws://127.0.0.1:*")?;
    let endpoint = socket.last_endpoint()?;
    ```

=== "Go"

    ```go
    socket.Bind("ws://*:8080")
    socket.Connect("ws://server:8080")
    socket.Bind("ws://127.0.0.1:*")
    endpoint := socket.LastEndpoint()
    ```

> 참고: `core/tests/test_stream_socket.cpp` -- `test_stream_ws_basic()`

### 특성

- RFC 6455 준수
- Beast 라이브러리 기반
- 바이너리 프레임 모드 (Opcode=0x02)
- 64KB write buffer

## 6. WebSocket + TLS (wss)

암호화된 WebSocket 통신.

### 기본 사용법

=== "C"

    ```c
    /* 서버 */
    zlink_set_tls_server(socket, cert_path, key_path, 0);
    zlink_bind(socket, "wss://*:8443");

    /* 클라이언트 */
    zlink_set_tls_client(socket, ca_path, "localhost", 0);
    zlink_connect(socket, "wss://server:8443");
    ```

=== "C++"

    ```cpp
    socket.set_tls_server(cert_path, key_path, 0);
    socket.bind("wss://*:8443");

    socket.set_tls_client(ca_path, "localhost", 0);
    socket.connect("wss://server:8443");
    ```

=== "Java"

    ```java
    socket.setTlsServer(certPath, keyPath, 0);
    socket.bind("wss://*:8443");

    socket.setTlsClient(caPath, "localhost", 0);
    socket.connect("wss://server:8443");
    ```

=== "Python"

    ```python
    socket.set_tls_server(cert_path, key_path, 0)
    socket.bind("wss://*:8443")

    socket.set_tls_client(ca_path, "localhost", 0)
    socket.connect("wss://server:8443")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer(certPath, keyPath, 0);
    socket.bind("wss://*:8443");

    socket.setTlsClient(caPath, "localhost", 0);
    socket.connect("wss://server:8443");
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer(certPath, keyPath, 0);
    socket.Bind("wss://*:8443");

    socket.SetTlsClient(caPath, "localhost", 0);
    socket.Connect("wss://server:8443");
    ```

=== "Rust"

    ```rust
    socket.set_tls_server(cert_path, key_path, false)?;
    socket.bind("wss://*:8443")?;

    socket.set_tls_client(ca_path, "localhost", false)?;
    socket.connect("wss://server:8443")?;
    ```

=== "Go"

    ```go
    socket.SetTLSServer(certPath, keyPath, false)
    socket.Bind("wss://*:8443")

    socket.SetTLSClient(caPath, "localhost", false)
    socket.Connect("wss://server:8443")
    ```

> 참고: `core/tests/test_stream_socket.cpp` -- `test_stream_wss_basic()`

### ws 대비 추가 설정

| 설정 | ws | wss |
|------|:--:|:---:|
| `zlink_set_tls_server()` (서버 cert+key) | - | 필수 |
| `zlink_set_tls_client()` (클라이언트 CA+hostname+trust) | - | 권장 |

## 7. TLS

네이티브 TLS 암호화 통신.

### 기본 사용법

=== "C"

    ```c
    /* 서버 */
    zlink_set_tls_server(socket, "/path/to/cert.pem", "/path/to/key.pem", 0);
    zlink_bind(socket, "tls://*:5555");

    /* 클라이언트 */
    zlink_set_tls_client(socket, "/path/to/ca.pem", NULL, 1);
    zlink_connect(socket, "tls://server:5555");
    ```

=== "C++"

    ```cpp
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("tls://*:5555");

    socket.set_tls_client("/path/to/ca.pem", "", true);
    socket.connect("tls://server:5555");
    ```

=== "Java"

    ```java
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("tls://*:5555");

    socket.setTlsClient("/path/to/ca.pem", null, true);
    socket.connect("tls://server:5555");
    ```

=== "Python"

    ```python
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0)
    socket.bind("tls://*:5555")

    socket.set_tls_client("/path/to/ca.pem", None, True)
    socket.connect("tls://server:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("tls://*:5555");

    socket.setTlsClient("/path/to/ca.pem", null, true);
    socket.connect("tls://server:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.Bind("tls://*:5555");

    socket.SetTlsClient("/path/to/ca.pem", null, true);
    socket.Connect("tls://server:5555");
    ```

=== "Rust"

    ```rust
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", false)?;
    socket.bind("tls://*:5555")?;

    socket.set_tls_client("/path/to/ca.pem", None, true)?;
    socket.connect("tls://server:5555")?;
    ```

=== "Go"

    ```go
    socket.SetTLSServer("/path/to/cert.pem", "/path/to/key.pem", false)
    socket.Bind("tls://*:5555")

    socket.SetTLSClient("/path/to/ca.pem", "", true)
    socket.Connect("tls://server:5555")
    ```

상세 TLS 설정은 [TLS 보안 가이드](05-tls-security.ko.md)를 참고.

## 8. Transport 제약사항

| 제약 | 설명 |
|------|------|
| STREAM | bind만 지원, ipc/inproc 미지원 |
| inproc | bind가 connect보다 먼저 호출 필요 |
| ipc | Unix/Linux/macOS만 지원 (Windows 미지원) |
| inproc context | 동일 context 내에서만 사용 |
| IPC 경로 | Unix 도메인 소켓 경로 최대 108자 |

## 9. Transport 선택 가이드

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

=== "C"

    ```c
    /* 다중 bind — 여러 인터페이스에서 수신 */
    zlink_bind(router, "tcp://192.168.1.10:5555");
    zlink_bind(router, "tcp://10.0.0.1:5555");
    zlink_bind(router, "ipc:///tmp/router.ipc");

    /* 다중 connect — 여러 서버에 연결 */
    zlink_connect(dealer, "tcp://server1:5555");
    zlink_connect(dealer, "tcp://server2:5555");
    ```

=== "C++"

    ```cpp
    router.bind("tcp://192.168.1.10:5555");
    router.bind("tcp://10.0.0.1:5555");
    router.bind("ipc:///tmp/router.ipc");

    dealer.connect("tcp://server1:5555");
    dealer.connect("tcp://server2:5555");
    ```

=== "Java"

    ```java
    router.bind("tcp://192.168.1.10:5555");
    router.bind("tcp://10.0.0.1:5555");
    router.bind("ipc:///tmp/router.ipc");

    dealer.connect("tcp://server1:5555");
    dealer.connect("tcp://server2:5555");
    ```

=== "Python"

    ```python
    router.bind("tcp://192.168.1.10:5555")
    router.bind("tcp://10.0.0.1:5555")
    router.bind("ipc:///tmp/router.ipc")

    dealer.connect("tcp://server1:5555")
    dealer.connect("tcp://server2:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    router.bind("tcp://192.168.1.10:5555");
    router.bind("tcp://10.0.0.1:5555");
    router.bind("ipc:///tmp/router.ipc");

    dealer.connect("tcp://server1:5555");
    dealer.connect("tcp://server2:5555");
    ```

=== "C#/.NET"

    ```csharp
    router.Bind("tcp://192.168.1.10:5555");
    router.Bind("tcp://10.0.0.1:5555");
    router.Bind("ipc:///tmp/router.ipc");

    dealer.Connect("tcp://server1:5555");
    dealer.Connect("tcp://server2:5555");
    ```

=== "Rust"

    ```rust
    router.bind("tcp://192.168.1.10:5555")?;
    router.bind("tcp://10.0.0.1:5555")?;
    router.bind("ipc:///tmp/router.ipc")?;

    dealer.connect("tcp://server1:5555")?;
    dealer.connect("tcp://server2:5555")?;
    ```

=== "Go"

    ```go
    router.Bind("tcp://192.168.1.10:5555")
    router.Bind("tcp://10.0.0.1:5555")
    router.Bind("ipc:///tmp/router.ipc")

    dealer.Connect("tcp://server1:5555")
    dealer.Connect("tcp://server2:5555")
    ```

### ZLINK_OPT_LAST_ENDPOINT

와일드카드 바인드 후 실제 할당된 엔드포인트를 조회한다.

=== "C"

    ```c
    zlink_bind(socket, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    printf("바인드된 엔드포인트: %s\n", endpoint);
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://127.0.0.1:*");
    std::cout << "바인드된 엔드포인트: " << socket.last_endpoint() << "\n";
    ```

=== "Java"

    ```java
    socket.bind("tcp://127.0.0.1:*");
    System.out.println("바인드된 엔드포인트: " + socket.lastEndpoint());
    ```

=== "Python"

    ```python
    socket.bind("tcp://127.0.0.1:*")
    print(f"바인드된 엔드포인트: {socket.last_endpoint()}")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://127.0.0.1:*");
    console.log(`바인드된 엔드포인트: ${socket.lastEndpoint()}`);
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://127.0.0.1:*");
    Console.WriteLine($"바인드된 엔드포인트: {socket.LastEndpoint}");
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://127.0.0.1:*")?;
    println!("바인드된 엔드포인트: {}", socket.last_endpoint()?);
    ```

=== "Go"

    ```go
    socket.Bind("tcp://127.0.0.1:*")
    fmt.Printf("Bound endpoint: %s\n", socket.LastEndpoint())
    ```

성능 비교는 [성능 가이드](10-performance.ko.md)를 참고.

---
[← STREAM](03-5-stream.ko.md) | [TLS 보안 →](05-tls-security.ko.md)
