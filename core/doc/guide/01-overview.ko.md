[English](01-overview.md) | [한국어](01-overview.ko.md)

<!-- zlink-nav:start -->
[Core API →](02-core-api.ko.md)
<!-- zlink-nav:end -->

# zlink 개요 및 시작하기

## 1. zlink이란?

zlink는 [libzmq](https://github.com/zeromq/libzmq) v4.3.5 기반의 현대적 메시징 라이브러리이다.
핵심 패턴에 집중하면서 Boost.Asio 기반 I/O와 개발 친화적 API를 제공한다.

### libzmq 대비 변경 사항

| 항목 | libzmq | zlink |
|------|--------|-------|
| **Socket Types** | 17종 (draft 포함) | **8종** — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM |
| **I/O Engine** | 자체 poll/epoll/kqueue | **Boost.Asio** (번들, 외부 의존성 없음) |
| **암호화** | CURVE (libsodium) | **TLS**(Transport Layer Security) (OpenSSL) — `tls://`, `wss://` |
| **Transport** | 10종+ (PGM, TIPC, VMCI 등) | **6종** — `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` |
| **의존성** | libsodium, libbsd 등 | **OpenSSL만** |

참고: `pgm://`, `epgm://`는 기본 빌드에 포함되지 않는다(OpenPGM을 켠 빌드에서만 제공).

## 2. 아키텍처 개요

```
+------------------------------------------------------+
|  Application / Bindings                              |
|  C callers · cpp · dotnet · java · node · python     |
+------------------------------------------------------+
|  Public API Facade  (core/src/api/)                  |
|  context_api · socket_api · message_api              |
|  service_api · poller_api · monitor_api              |
|  validate + delegate, per-handle admission guard     |
+------------------------------------------------------+
|  Service Layer                                       |
|  SPOT · Actor (공개) · 내부 위치 런타임               |
|  service access seam (*_access) · lifecycle · runtime|
+------------------------------------------------------+
|  Socket Semantic / Runtime                           |
|  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM |
|  semantic entrypoint + runtime components            |
|  (dispatch · monitor · endpoint · lifecycle)         |
+------------------------------------------------------+
|  Runtime Core  (core/src/core/)                      |
|  ctx · own · reaper · multipart_send_txn             |
|  options dispatch (core_socket · transport · protocol|
|  close/drain/finalization contract                   |
+------------------------------------------------------+
|  Engine Layer (Boost.Asio)                           |
|  asio_zmp_engine — ZMP v1.0 Protocol (8B fixed hdr)  |
|  Proactor pattern · Speculative I/O · Backpressure   |
+------------------------------------------------------+
|  Transport / Protocol                                |
|  tcp · ipc · inproc · ws — plaintext                 |
|  tls · wss             — OpenSSL encrypted           |
+------------------------------------------------------+
|  Core Infrastructure                                 |
|  msg_t(64B fixed) · pipe_t(Lock-free YPipe)          |
|  ctx_t(I/O Thread Pool) · session_base_t(Bridge)     |
+------------------------------------------------------+
```

각 계층의 핵심 역할:

| 계층 | 역할 |
|------|------|
| Public API Facade | C API 진입점. validate + delegate만 수행 |
| Service Layer | SPOT(+Actor) 의미와 lifecycle. access seam으로 API와 연결 |
| Socket Semantic/Runtime | socket family 의미와 공통 runtime이 분리 |
| Runtime Core | context, shutdown, option dispatch, multipart send |
| Engine Layer | Boost.Asio 기반 poller, io_context 실행 기반 |
| Transport/Protocol | wire format, TLS 핸드셰이크(연결 수립 시 암호화 협상 과정), address scheme |

## 3. 핵심 설계

| 설계 원칙 | 설명 |
|-----------|------|
| **Zero-Copy** | VSM(Very Small Message, 41B 이하 메시지를 별도 힙 할당 없이 객체 안에 직접 저장)은 inline 저장, 대용량은 참조 카운팅 |
| **Lock-Free** | Thread 간 통신에 YPipe(락 없이 CAS(Compare-And-Swap) 연산으로 구현한 FIFO 큐) 사용 |
| **True Async** | Proactor 패턴(I/O 완료 이벤트를 핸들러로 전달하는 비동기 설계) 기반 비동기 I/O |
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
| inproc | `inproc://name` | 프로세스 내 통신 (같은 프로세스 안의 스레드 간 통신; 네트워크 없이 메모리를 직접 공유) |
| ws | `ws://host:port` | WebSocket |
| wss | `wss://host:port` | WebSocket + TLS |
| tls | `tls://host:port` | 네이티브 TLS |

## 6. 서비스 계층

서비스 계층은 소켓 위에 놓이는 고수준 분산 기능이다. 소켓 연결 관리, 피어 주소 추적,
서비스 수명주기를 자동으로 처리한다.

| 서비스 | 역할 |
|--------|------|
| **SPOT** | MeshNode 위의 동적 상태 단위. channel 구독과 Logical Multicast, direct 메시징을 제공하며 `MeshNode`가 transport를 소유하고 `Spot` facade가 data plane을 제공 |
| **Actor** | Spot에 join하는 세션 기반 주소 지정 단위. `MeshNode`가 Actor registry를 관리하고 `Entry Spot`에서 초기 메시지를 전달한다 |

자세한 내용은 [서비스 계층 개요](07-0-services.ko.md), [SPOT 가이드](07-3-spot.ko.md),
[SPOT Actor 가이드](07-4-actor.ko.md)를 본다.

## 7. 빠른 시작

### 요구 사항

- CMake 3.10+, C++17 컴파일러, OpenSSL

### 빌드

```bash
cmake -S core -B core/build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build core/build
```

### 첫 번째 프로그램

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zlink_ctx_new();

    /* Server */
    void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_bind(server, "tcp://*:5555");

    /* Client */
    void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_connect(client, "tcp://127.0.0.1:5555");

    /* Send */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 12);
    memcpy(zlink_msg_data(&part), "Hello zlink!", 12);
    zlink_send(client, &part, 1, 0);

    /* Receive */
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_recv_result_t rc = zlink_recv(server, &source_rid, &parts, &part_count, 0);
    if (rc == ZLINK_RECV_OK)
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
    zlink_multipart_close(parts, part_count);

    zlink_close(client);
    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
```

## 8. 다음 단계

- [Core API 상세](02-core-api.ko.md)
- [소켓 패턴별 사용법](03-0-socket-patterns.ko.md)
- [Transport 가이드](04-transports.ko.md)
- [TLS 보안 설정](05-tls-security.ko.md)
- [서비스 계층 개요](07-0-services.ko.md)
- [SPOT 가이드](07-3-spot.ko.md)
- [SPOT Actor 가이드](07-4-actor.ko.md)

---
<!-- zlink-nav:bottom:start -->
[Core API →](02-core-api.ko.md)
<!-- zlink-nav:bottom:end -->
