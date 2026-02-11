# 소켓 패턴 개요 및 선택 가이드

## 1. 개요

zlink는 8종의 소켓 타입을 제공한다. 각 소켓은 고유한 메시징 패턴을 구현하며, 유효한 소켓 조합 내에서만 통신이 가능하다.

## 2. 소켓 요약

| 소켓 | 패턴 | 방향 | 라우팅 전략 | 주요 용도 |
|------|------|------|-------------|-----------|
| **PAIR** | 1:1 양방향 | 양방향 | 단일 파이프 (1:1 독점) | 스레드 간 시그널링, 워커 조정 |
| **PUB** | 발행 | 단방향 (송신) | `dist_t` (Fan-out) | 이벤트 브로드캐스트 |
| **SUB** | 구독 | 단방향 (수신) | `fq_t` (Fair-queue) | 토픽 필터링 수신 |
| **XPUB** | 고급 발행 | 양방향 | `dist_t` + 구독 수신 | 프록시/브로커, 구독 모니터링 |
| **XSUB** | 고급 구독 | 양방향 | `fq_t` + 구독 송신 | 프록시/브로커 |
| **DEALER** | 비동기 요청 | 양방향 | 송신: `lb_t` (Round-robin), 수신: `fq_t` | 로드밸런싱, 비동기 요청 |
| **ROUTER** | ID 라우팅 | 양방향 | routing_id 기반 지정 전송 | 서버, 브로커, 멀티 클라이언트 |
| **STREAM** | RAW 통신 | 양방향 | routing_id 기반 (4B uint32) | 외부 클라이언트 연동 |

## 3. 소켓 호환성 매트릭스

유효한 소켓 조합만 연결이 가능하다. 비호환 소켓을 연결하면 핸드셰이크가 실패한다.

| | PAIR | PUB | SUB | XPUB | XSUB | DEALER | ROUTER | STREAM |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **PAIR** | **O** | | | | | | | |
| **PUB** | | | **O** | | **O** | | | |
| **SUB** | | **O** | | **O** | | | | |
| **XPUB** | | | **O** | | **O** | | | |
| **XSUB** | | **O** | | **O** | | | | |
| **DEALER** | | | | | | **O** | **O** | |
| **ROUTER** | | | | | | **O** | **O** | |
| **STREAM** | | | | | | | | **외부** |

> STREAM 소켓은 zlink 내부 소켓과 호환되지 않으며, 외부 RAW 클라이언트와만 통신한다.

## 4. 라우팅 전략 요약

| 전략 | 동작 | 사용 소켓 |
|------|------|-----------|
| **단일 파이프** | 하나의 피어와만 통신 (N:1 불가) | PAIR |
| **Round-robin** (`lb_t`) | 연결된 피어에 순환 분배 | DEALER 송신 |
| **Fair-queue** (`fq_t`) | 모든 피어에서 공정하게 수신 | DEALER/SUB 수신 |
| **Fan-out** (`dist_t`) | 모든 구독자에게 복제 전송 | PUB/XPUB |
| **ID 라우팅** | routing_id 프레임으로 특정 피어 지정 | ROUTER/STREAM |

> 라우팅 전략의 내부 구현 상세는 [architecture.md](../internals/architecture.md)를 참고.

## 5. 패턴 선택 가이드

### 의사결정 플로우

```
통신 상대가 외부 클라이언트(브라우저, 게임)인가?
├── Yes → STREAM (ws/wss/tcp/tls)
└── No → zlink 소켓 간 통신
         ├── 1:1 전용인가?
         │   └── Yes → PAIR
         └── No → N:M 통신
              ├── 발행-구독 (브로드캐스트)인가?
              │   ├── 프록시/브로커 필요 → XPUB/XSUB
              │   └── 단순 발행-구독 → PUB/SUB
              └── 요청-응답 / 라우팅인가?
                  └── DEALER/ROUTER
```

### 사용 사례별 추천

| 사용 사례 | 추천 패턴 | 설명 |
|-----------|-----------|------|
| 스레드 간 시그널링 | PAIR + inproc | 가장 빠른 1:1 통신 |
| 이벤트 브로드캐스트 | PUB/SUB | 토픽 기반 필터링 |
| 메시지 브로커/프록시 | XPUB/XSUB | 구독 메시지 접근 및 변환 |
| 비동기 요청-응답 서버 | DEALER/ROUTER | 다중 클라이언트 처리 |
| 로드밸런싱 | 다중 DEALER → ROUTER | Round-robin 분배 |
| 특정 피어 전송 | ROUTER | routing_id로 대상 지정 |
| 웹 클라이언트 연동 | STREAM + ws/wss | WebSocket RAW 통신 |
| 외부 TCP 클라이언트 | STREAM + tcp/tls | Length-Prefix RAW 통신 |

## 6. 하위 문서

각 소켓 타입의 상세 사용법은 개별 문서를 참고한다.

| 문서 | 소켓 | 설명 |
|------|------|------|
| [03-1-pair.md](03-1-pair.md) | PAIR | 1:1 양방향 독점 연결 |
| [03-2-pubsub.md](03-2-pubsub.md) | PUB/SUB/XPUB/XSUB | 발행-구독 패밀리 |
| [03-3-dealer.md](03-3-dealer.md) | DEALER | 비동기 요청, Round-robin |
| [03-4-router.md](03-4-router.md) | ROUTER | ID 기반 라우팅 |
| [03-5-stream.md](03-5-stream.md) | STREAM | 외부 클라이언트 RAW 통신 |

## 7. 기본 사용 흐름

모든 소켓 타입에 공통되는 기본 패턴:

```c
/* 1. Context 생성 */
void *ctx = zlink_ctx_new();

/* 2. 소켓 생성 */
void *socket = zlink_socket(ctx, ZLINK_<TYPE>);

/* 3. 소켓 옵션 설정 (bind/connect 전) */
zlink_setsockopt(socket, ZLINK_<OPTION>, &value, sizeof(value));

/* 4. 연결 (bind 또는 connect) */
zlink_bind(socket, "tcp://*:5555");
// 또는
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* 5. 메시지 송수신 */
zlink_send(socket, data, size, flags);
zlink_recv(socket, buf, buf_size, flags);

/* 6. 정리 */
zlink_close(socket);
zlink_ctx_term(ctx);
```

> 소켓 옵션은 반드시 `zlink_bind()`/`zlink_connect()` **이전에** 설정해야 한다.
