# zlink 개요 및 시작하기

## 1. zlink이란?

zlink는 [libzmq](https://github.com/zeromq/libzmq) v4.3.5 기반의 현대적 메시징 라이브러리이다.
핵심 패턴에 집중하고, Boost.Asio 기반 I/O와 개발 친화적 API를 제공한다.

### libzmq 대비 변경 사항

| 항목 | libzmq | zlink |
|------|--------|-------|
| **Socket Types** | 17종 (draft 포함) | **8종** — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM |
| **I/O Engine** | 자체 poll/epoll/kqueue | **Boost.Asio** (번들, 외부 의존성 없음) |
| **암호화** | CURVE (libsodium) | **TLS** (OpenSSL) — `tls://`, `wss://` |
| **Transport** | 10종+ (PGM, TIPC, VMCI 등) | **6종** — `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` |
| **의존성** | libsodium, libbsd 등 | **OpenSSL만** |

참고: `pgm://`, `epgm://`는 현재 zlink에서 임시 비활성화 상태이며 지원하지 않는다.

## 2. 아키텍처 개요

```
┌──────────────────────────────────────────────────────┐
│  Application / Bindings                               │
│  C callers · cpp · dotnet · java · node · python      │
├──────────────────────────────────────────────────────┤
│  Public API Facade  (core/src/api/)                   │
│  context_api · socket_api · message_api               │
│  service_api · poller_api · monitor_api               │
│  validate + delegate, per-handle admission guard      │
├──────────────────────────────────────────────────────┤
│  Service Layer                                        │
│  Discovery · SPOT · Registry                          │
│  service access seam (*_access) · lifecycle · runtime │
├──────────────────────────────────────────────────────┤
│  Socket Semantic / Runtime                            │
│  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM│
│  semantic entrypoint + runtime components             │
│  (dispatch · monitor · endpoint · lifecycle)          │
├──────────────────────────────────────────────────────┤
│  Runtime Core  (core/src/core/)                       │
│  ctx · own · reaper · multipart_send_txn              │
│  options dispatch (core_socket · transport · protocol)│
│  close/drain/finalization contract                    │
├──────────────────────────────────────────────────────┤
│  Engine Layer (Boost.Asio)                            │
│  asio_zmp_engine — ZMP v1.0 Protocol (8B 고정 헤더)   │
│  Proactor 패턴 · Speculative I/O · Backpressure       │
├──────────────────────────────────────────────────────┤
│  Transport / Protocol                                 │
│  tcp · ipc · inproc · ws — 평문                       │
│  tls · wss             — OpenSSL 암호화               │
├──────────────────────────────────────────────────────┤
│  Core Infrastructure                                  │
│  msg_t(64B 고정) · pipe_t(Lock-free YPipe)            │
│  ctx_t(I/O Thread Pool) · session_base_t(Bridge)      │
└──────────────────────────────────────────────────────┘
```

각 계층의 핵심 역할:

| 계층 | 역할 |
|------|------|
| Public API Facade | C API 진입점. validate + delegate만 수행 |
| Service Layer | 서비스 의미와 lifecycle. access seam으로 API와 연결 |
| Socket Semantic/Runtime | socket family 의미와 공통 runtime이 분리 |
| Runtime Core | context, shutdown, option dispatch, multipart send |
| Engine Layer | Boost.Asio 기반 poller, io_context 실행 기반 |
| Transport/Protocol | wire format, TLS handshake, address scheme |

## 3. 핵심 설계

| 설계 원칙 | 설명 |
|-----------|------|
| **Zero-Copy** | VSM(33B 이하)은 inline 저장, 대용량은 참조 카운팅 |
| **Lock-Free** | Thread 간 통신에 YPipe(CAS 기반 FIFO) 사용 |
| **True Async** | Proactor 패턴 기반 비동기 I/O |
| **Protocol Agnostic** | Transport와 Protocol의 명확한 분리 |

## 4. 소켓 타입

| 소켓 타입 | 패턴 | 설명 |
|-----------|------|------|
| PAIR | 1:1 양방향 | 스레드 간 시그널링, 단순 통신 |
| PUB/SUB | 발행-구독 | 토픽 기반 메시지 분배 |
| XPUB/XSUB | 고급 발행-구독 | 구독 메시지 접근, 프록시 |
| DEALER/ROUTER | 비동기 라우팅 | 요청-응답, 로드밸런싱 |
| STREAM | RAW 통신 | 외부 클라이언트 연동 (tcp/tls/ws/wss) |

## 5. Transport

| Transport | URI 형식 | 설명 |
|-----------|----------|------|
| tcp | `tcp://host:port` | 표준 TCP |
| ipc | `ipc://path` | Unix 도메인 소켓 |
| inproc | `inproc://name` | 프로세스 내 통신 |
| ws | `ws://host:port` | WebSocket |
| wss | `wss://host:port` | WebSocket + TLS |
| tls | `tls://host:port` | 네이티브 TLS |

## 6. 빠른 시작

### 요구 사항

- CMake 3.10+, C++17 컴파일러, OpenSSL

### 빌드

```bash
cmake -B build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build build
```

### 첫 번째 프로그램

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void) {
        void *ctx = zlink_ctx_new();

        /* 서버 */
        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "tcp://*:5555");

        /* 클라이언트 */
        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, "tcp://127.0.0.1:5555");

        /* 송신 */
        zlink_msg_t part;
        zlink_msg_init_size(&part, 12);
        memcpy(zlink_msg_data(&part), "Hello zlink!", 12);
        zlink_send(client, &part, 1, 0);

        /* 수신 */
        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        int rc = zlink_recv(server, &source_rid, &parts, &part_count, 0);
        if (rc == 0)
            printf("수신: %.*s\n",
                   (int)zlink_msg_size(&parts[0]),
                   (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);

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

        // 서버
        zlink::pair_socket_t server(ctx);
        server.bind("tcp://*:5555");

        // 클라이언트
        zlink::pair_socket_t client(ctx);
        client.connect("tcp://127.0.0.1:5555");

        // 송신
        zlink::message_t msg("Hello zlink!", 12);
        client.send(msg);

        // 수신
        auto [source_rid, parts] = server.recv();
        if (!parts.empty())
            std::cout << "수신: " << parts[0].to_string() << "\n";

        client.close();
        server.close();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class HelloZlink {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                // 서버
                PairSocket server = new PairSocket(ctx);
                server.bind("tcp://*:5555");

                // 클라이언트
                PairSocket client = new PairSocket(ctx);
                client.connect("tcp://127.0.0.1:5555");

                // 송신
                Message msg = new Message("Hello zlink!".getBytes());
                client.send(msg);

                // 수신
                RecvResult result = server.recv();
                System.out.println("수신: "
                    + new String(result.parts()[0].data()));

                client.close();
                server.close();
            }
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    # 서버
    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    # 클라이언트
    client = zlink.PairSocket(ctx)
    client.connect("tcp://127.0.0.1:5555")

    # 송신
    client.send(b"Hello zlink!")

    # 수신
    source_rid, parts = server.recv()
    print(f"수신: {parts[0].data().decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from "zlink";

    const ctx = new zlink.Context();

    // 서버
    const server = new zlink.PairSocket(ctx);
    server.bind("tcp://*:5555");

    // 클라이언트
    const client = new zlink.PairSocket(ctx);
    client.connect("tcp://127.0.0.1:5555");

    // 송신
    client.send(Buffer.from("Hello zlink!"));

    // 수신
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

    // 서버
    using var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    // 클라이언트
    using var client = new PairSocket(ctx);
    client.Connect("tcp://127.0.0.1:5555");

    // 송신
    client.Send(new Message("Hello zlink!"u8));

    // 수신
    var (sourceRid, parts) = server.Recv();
    Console.WriteLine($"수신: {parts[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::{Context, SocketType};

    fn main() -> zlink::Result<()> {
        let ctx = Context::new()?;

        // 서버
        let server = ctx.pair_socket()?;
        server.bind("tcp://*:5555")?;

        // 클라이언트
        let client = ctx.pair_socket()?;
        client.connect("tcp://127.0.0.1:5555")?;

        // 송신
        client.send(b"Hello zlink!")?;

        // 수신
        let (source_rid, parts) = server.recv()?;
        println!("수신: {}", parts[0].as_str()?);

        client.close()?;
        server.close()?;
        ctx.term()?;
        Ok(())
    }
    ```

## 7. 다음 단계

- [Core API 상세](02-core-api.ko.md)
- [소켓 패턴별 사용법](03-0-socket-patterns.ko.md)
- [Transport 가이드](04-transports.ko.md)
- [TLS 보안 설정](05-tls-security.ko.md)

---
[Core API →](02-core-api.ko.md)
