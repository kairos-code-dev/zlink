# zlink 개요 및 시작하기

## 1. zlink이란?

zlink는 [libzmq](https://github.com/zeromq/libzmq) v4.3.5 기반의 현대적 메시징 라이브러리이다. 핵심 패턴에 집중하고, Boost.Asio 기반 I/O와 개발 친화적 API를 제공한다.

### libzmq 대비 변경 사항

| | libzmq | zlink |
|---|--------|-------|
| **Socket Types** | 17종 (draft 포함) | **7종** — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM |
| **I/O Engine** | 자체 poll/epoll/kqueue | **Boost.Asio** (번들, 외부 의존성 없음) |
| **암호화** | CURVE (libsodium) | **TLS** (OpenSSL) — `tls://`, `wss://` |
| **Transport** | 10종+ (PGM, TIPC, VMCI 등) | **6종** — `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` |
| **의존성** | libsodium, libbsd 등 | **OpenSSL만** |

## 2. 아키텍처 개요

```
┌──────────────────────────────────────────────────────┐
│  Application Layer                                    │
│  zlink_ctx_new() · zlink_socket() · zlink_send/recv() │
├──────────────────────────────────────────────────────┤
│  Socket Logic Layer                                   │
│  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM│
│  라우팅: lb_t(Round-robin) · fq_t · dist_t            │
├──────────────────────────────────────────────────────┤
│  Engine Layer (Boost.Asio)                            │
│  asio_zmp_engine — ZMP v2.0 Protocol (8B 고정 헤더)   │
│  Proactor 패턴 · Speculative I/O · Backpressure       │
├──────────────────────────────────────────────────────┤
│  Transport Layer                                      │
│  tcp · ipc · inproc · ws — 평문                       │
│  tls · wss             — OpenSSL 암호화               │
├──────────────────────────────────────────────────────┤
│  Core Infrastructure                                  │
│  msg_t(64B 고정) · pipe_t(Lock-free YPipe)            │
│  ctx_t(I/O Thread Pool) · session_base_t(Bridge)      │
└──────────────────────────────────────────────────────┘
```

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
    const char *msg = "Hello zlink!";
    zlink_send(client, msg, strlen(msg), 0);

    /* 수신 */
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

## 7. 다음 단계

- [Core API 상세](02-core-api.md)
- [소켓 패턴별 사용법](03-0-socket-patterns.md)
- [Transport 가이드](04-transports.md)
- [TLS 보안 설정](05-tls-security.md)
