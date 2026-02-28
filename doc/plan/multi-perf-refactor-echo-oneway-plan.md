# Multi Perf 리팩토링 계획 (Echo + One-way)

## 1. 목적

`core/perf/multi` 전체를 성능 테스트 본래 목적(라이브러리 최대 성능 확인)에 맞게 정리한다.
핵심은 측정 경로(hot path)에서 불필요한 대기/복사/동적할당을 제거하고,
Echo/One-way 분류 및 메트릭 계산을 코드/러너/문서에서 완전히 일치시키는 것이다.

이 테스트의 목적은 **성능 테스트**이다. 라이브러리의 최대 성능을 정확히 확인하는 것이 목표이며,
측정 경로(hot path)에서 불필요한 대기·복사·할당이 존재하면 측정 자체가 왜곡된다.

핵심 목표:
- hot path 불필요 대기 제거 (sleep/backoff 등 측정과 무관한 대기. poll은 multi-socket readiness 확인용으로 필요한 패턴에서 허용)
- hot path 불필요 메모리 복사 제거
- hot path 반복 할당 제거 (루프 내 new/malloc/vector growth)
- `recv/send/pubsub` 실행 정책 통일
- 메시지 크기별 실제 부하 반영(평탄화 버그 제거)
- 코드 가독성 향상(긴 함수 분해, 중복 제거)

기본 옵션/정책 참조 기준: `core/perf/single` (단일 클라이언트 벤치마크)
- single 벤치에서 검증된 recv/send/pubsub 정책을 multi에도 동일하게 적용한다.
- single과 multi에서 동일 패턴의 기본 옵션(hwm, nodrop 등)이 달라지면 안 된다.

## 2. 범위

패턴 범위:
- `MULTI_DEALER_DEALER`
- `MULTI_DEALER_ROUTER`
- `MULTI_ROUTER_ROUTER`
- `MULTI_PUBSUB`
- `MULTI_GATEWAY`
- `MULTI_SPOT`
- `MULTI_STREAM`
- `MULTI_STREAM_CALLBACK`
- `MULTI_STREAM_LEN32BE`

파일 범위:
- `core/perf/multi/current/*.cpp`
- `core/perf/multi/common/*.hpp`
- `core/perf/common/streamclient/perf_stream_client.cpp`
- `core/perf/common/streamclient/*.hpp`
- `core/perf/run_comparison.py`
- `core/perf/PERF_MULTI_TEST_POLICY.md`
- 멀티 벤치 관련 가이드 문서

## 3. 고정 실행 정책 (필수)

### 3.1 Recv 정책

- 기본 경로: `poll + recv + batch drain` (multi client 다중 소켓 워커 모델)
- 단일 소켓 경로에서만: `blocking recv + batch drain`
- poll은 readiness 확인 전용이며, 실제 수신 처리는 drain 루프로 통일한다.

패턴별 recv 모드:

| 패턴 | recv 모드 |
|---|---|
| `MULTI_DEALER_DEALER` | `poll + recv + batch drain` |
| `MULTI_DEALER_ROUTER` | `poll + recv + batch drain` |
| `MULTI_ROUTER_ROUTER` | `poll + recv + batch drain` |
| `MULTI_PUBSUB` | `poll + recv + batch drain` |
| `MULTI_GATEWAY` | `poll + recv + batch drain` |
| `MULTI_SPOT` | `poll + recv + batch drain` |
| `MULTI_STREAM*` | `poll + recv + batch drain` |

### 3.2 Batch drain 정의

batch drain은 소켓에서 대기 중인 메시지를 한 번에 모두 소비하는 비차단 루프이다.

동작:
```
while (true) {
    rc = recv_one_message(socket, scratch, ZLINK_DONTWAIT)
    if rc < 0  → 에러, 루프 종료 (false 반환)
    if rc == 0 && errno == EAGAIN → 더 이상 메시지 없음 → 루프 종료 (true 반환)
    if rc == 0 && errno == EINTR  → 시그널 개입, 재시도 (continue)
    ++recv_count
    // 선택적: latency timestamp 추출
}
```

종료 조건:
1. `rc < 0`: 수신 에러 → 함수 실패
2. `EAGAIN`: 비차단 수신이 블록될 상태 → 정상 종료
3. 모든 대기 메시지 소진

EINTR 처리:
- `EINTR`은 시그널에 의한 중단이므로 재시도한다 (조기 종료 방지)

제약:
- batch size 상한 없음 (대기 메시지 전부 소비)
- 루프 내 동적할당 없음 (scratch 버퍼 재사용)

### 3.3 Send 정책

- 모든 패턴에서 send는 `blocking send`만 사용한다.
- `ZLINK_DONTWAIT` 기반 송신 경로는 제거한다.
- send backoff 기반 우회 로직은 throughput 경로에서 제거한다.

제거 대상 환경변수/코드:
- `PERF_MULTI_BLOCKING_SEND` — 더 이상 분기 불필요 (항상 blocking)
- `PERF_PROFILE` 기반 blocking 분기 — 제거
- `bench_send_flags()` 함수 — 제거
- `PERF_MULTI_SEND_BACKOFF_US` — 제거
- `PERF_MULTI_CLIENT_IDLE_SLEEP_US` — 제거

### 3.4 PubSub 정책

- `nodrop`을 기본 옵션으로 강제한다.
- 기본 동작/옵션은 `core/perf/single` 기준과 정렬한다.

### 3.5 고정 제약

- `hwm/sndhwm/rcvhwm` 정책/기본값은 변경하지 않는다.
- retry/attempt/inflight 로직은 추가하지 않는다.
- inflight 관련 코드/환경변수/문서는 삭제한다.

## 4. 구조 리팩토링 원칙 (Linus + Fowler 혼합)

### 4.1 최우선 원칙: 샘플 코드처럼 읽히는 벤치마크

리팩토링 후 각 벤치마크 소스의 핵심 로직은 **zlink API 샘플 코드를 보듯이** 한눈에 들어와야 한다.

목표 상태 (DEALER_ROUTER Echo 클라이언트 예시):
```cpp
// --- 소켓 생성 ---
void *sock = zlink_socket (ctx, ZLINK_DEALER);
zlink_setsockopt (sock, ZLINK_ROUTING_ID, id, id_len);
zlink_connect (sock, endpoint);

// --- warmup ---
run_warmup (sock, settings);

// --- throughput 측정 ---
while (now < deadline) {
    zlink_send (sock, payload, msg_size, 0);       // blocking send
    int len = zlink_recv (sock, buf, buf_size, 0);  // blocking recv
    ++recv_count;
}

// --- 결과 출력 ---
print_throughput (recv_count, duration);
```

원칙:
- 벤치마크를 처음 보는 사람이 **zlink API 사용법과 측정 흐름을 즉시 파악**할 수 있어야 한다.
- 핵심 경로(소켓 생성 → connect → send → recv → 집계)가 헬퍼 뒤에 숨지 않아야 한다.
- 유틸리티(warmup, 메트릭 출력, TLS 설정)는 추출하되, 측정 루프 자체는 인라인으로 유지한다.

### 4.2 Linus 스타일

- 불필요한 추상화/분기 제거
- 데이터 흐름이 파일 내에서 바로 보이게 유지
- 간접 참조 최소화: 코드를 읽을 때 다른 파일로 점프하지 않아도 흐름이 파악되어야 한다

### 4.3 Fowler 스타일

- 긴 함수를 역할 단위로 분해 (Extract Method)
- 중복 로직 제거 (공통 헬퍼로 추출)
- 작은 단계로 안전하게 변경 후 검증 (Phase별 빌드 검증)

### 4.4 공통화 경계

공통화 허용:
- `warmup/throughput/latency/drain` phase 오케스트레이션
- 메트릭 수집/출력 유틸리티
- CLI 파싱, TLS 설정, 환경변수 해석
- latency sampler, stopwatch, 통계 계산

분리 유지 (패턴별 소스에 명시):
- 소켓 생성 및 타입 지정
- bind/connect 주소 및 옵션 설정
- 프레이밍 (identity frame, SNDMORE 등)
- send/recv 루프의 호출 순서와 플래그

### 4.5 Stub 파일 금지 원칙

파일 분리 시 `#include` 한 줄로 다른 파일의 구현 전체를 위임하는 stub 파일을 금지한다.

금지 예시:
```cpp
// perf_multi_gateway_client.cpp — 이런 파일은 금지
#include "perf_multi_common_client_impl.hpp"  // 한 줄 위임
```

허용 예시:
```cpp
// perf_multi_gateway_client.cpp
#include "perf_common_multi.hpp"      // 설정/유틸리티 공유 OK
#include "perf_multi_client_helpers.hpp"  // 헬퍼 함수 공유 OK

// 이 파일 안에서 소켓 생성, 연결, send/recv 루프, phase 제어가 명시적으로 존재
void run_gateway_client(...) {
    void *gw = zlink_gateway_new(ctx, discovery, "client-0");
    void *rx = zlink_receiver_new(ctx, "client-0");
    // ... 실제 로직이 이 파일에 존재
}
```

핵심: 각 벤치마크 소스 파일은 독립적으로 읽었을 때 해당 패턴의 동작을 이해할 수 있어야 한다. 유틸리티 공유는 자유롭되, 코어 흐름 자체를 외부 파일 한 줄로 위임하지 않는다.

## 5. 기능 변경 상세

### 5.1 MULTI_GATEWAY Echo 전환

현재 상태:
- `k_pubsub_mode = true`, `k_server_router_echo = false`
- 서버: `publish_once()` — 일방 송신 전용 (ZLINK_DONTWAIT)
- 클라이언트: `run_pubsub_worker_loop()` — 수신 전용 (poll + drain)
- 분류: one-way (`run_comparison.py`에서 `MULTI_ECHO_PATTERNS`에 미포함)
- 서버에 `relay_dealer_once()` echo 코드가 존재하지만 현재 비활성

변경 목표:
- `MULTI_GATEWAY`를 one-way가 아닌 실질 Echo로 전환한다.

변경 후 토폴로지:

하나의 클라이언트 인스턴스 = gateway + receiver 쌍.
서버 인스턴스도 동일하게 gateway + receiver 쌍으로 구성된다.

```
 CLIENT (gateway + receiver)              SERVER (gateway + receiver)
 ┌─────────────┐                         ┌─────────────┐
 │ gateway(gw) │── req ────────────────> │ receiver(rx) │
 │  [send]     │                         │  [recv]      │
 └─────────────┘                         └──────┬───────┘
                                                │ (내부 전달)
 ┌─────────────┐                         ┌──────┴───────┐
 │ receiver(rx) │<── resp ──────────────│ gateway(gw)  │
 │  [recv]      │                        │  [send]      │
 └─────────────┘                         └──────────────┘
```

소켓 구성:
- 클라이언트 1개 = `zlink_gateway_new()` 1개 + `zlink_receiver_new()` 1개
- 클라이언트 N개 = gateway N + receiver N
- 서버 = `zlink_receiver_new()` 1개 + `zlink_gateway_new()` 1개

인프라 구성:
- Registry 1개 (PUB + ROUTER 엔드포인트)
- Discovery 2개: 클라이언트용 1개 + 서버용 1개

셋업 시퀀스 (측정 시작 전):
```cpp
// --- 1. Registry 생성 및 시작 ---
void *registry = zlink_registry_new(ctx);
zlink_registry_set_endpoints(registry, reg_pub_ep, reg_router_ep);
zlink_registry_start(registry);

// --- 2. 서버 측: Receiver bind → Registry 등록 ---
void *server_rx = zlink_receiver_new(ctx, "server-rx");
zlink_receiver_bind(server_rx, server_rx_endpoint);
zlink_receiver_connect_registry(server_rx, reg_router_ep);
zlink_receiver_register(server_rx, "perf-server", server_rx_endpoint, 1);

// --- 3. 서버 측: Discovery + Gateway 생성 ---
void *server_disc = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(server_disc, reg_pub_ep);
// 클라이언트 서비스명은 런타임에 동적으로 증가하므로 와일드카드 구독을 사용하지 않는다.
// (구독 필터를 비우거나, 필요 시 개별 서비스명을 명시적으로 추가)
void *server_gw = zlink_gateway_new(ctx, server_disc, "server-gw");

// --- 4. 클라이언트 측: Receiver bind → Registry 등록 ---
void *client_rx = zlink_receiver_new(ctx, "client-0");
zlink_receiver_bind(client_rx, client_rx_endpoint);
zlink_receiver_connect_registry(client_rx, reg_router_ep);
zlink_receiver_register(client_rx, "perf-client-0", client_rx_endpoint, 1);

// --- 5. 클라이언트 측: Discovery + Gateway 생성 (서버 서비스 구독) ---
void *client_disc = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(client_disc, reg_pub_ep);
zlink_discovery_subscribe(client_disc, "perf-server");
void *client_gw = zlink_gateway_new(ctx, client_disc, "client-0");

// --- 6. 연결 준비 대기 ---
wait_for_gateway(client_gw, "perf-server", timeout_ms);   // 클라이언트→서버 경로
wait_for_gateway(server_gw, "perf-client-0", timeout_ms);  // 서버→클라이언트 경로
```

메시지 프레이밍:

gateway/receiver 경로는 내부적으로 multipart envelope를 사용한다:
```
Gateway send 내부:
  Frame 1: [receiver_routing_id]  (SNDMORE)  ← gateway가 자동 추가
  Frame 2: [user_payload]                     ← 사용자 데이터

Receiver ROUTER recv 결과:
  Frame 1: [sender_routing_id]    (MORE)      ← ROUTER가 자동 추가
  Frame 2: [user_payload]                     ← 사용자 데이터
```

벤치마크 코드에서의 처리:
- **송신**: `zlink_gateway_send(gw, service, &parts, count, 0)` — routing ID는 API가 처리
- **수신**: `zlink_receiver_router_socket_unsafe()` → `zlink_msg_recv()` 로 routing ID 프레임을 먼저 수신 후 payload 프레임 수신. `zlink_msg_more()` 로 추가 프레임 여부 확인.
- **echo 응답**: 수신한 routing ID를 보존하여 서버 gateway가 해당 클라이언트 서비스로 응답

참조 구현: `core/perf/single/current/perf_gateway.cpp`

메시지 흐름 시퀀스 (hot path):
```cpp
// 1. 클라이언트 gateway → 서버 receiver (요청)
zlink_gateway_send(client_gw, "perf-server", &req_parts, 1, 0);

// 2. 서버: receiver ROUTER 소켓에서 multipart 수신
void *rx_sock = zlink_receiver_router_socket_unsafe(server_rx);
zlink_msg_recv(&rid_frame, rx_sock, 0);      // routing ID frame
zlink_msg_recv(&payload_frame, rx_sock, 0);  // payload frame

// 3. 서버: payload를 그대로 echo — gateway로 클라이언트 서비스에 전송
zlink_gateway_send(server_gw, client_service, &resp_parts, 1, 0);

// 4. 클라이언트: receiver ROUTER 소켓에서 multipart 수신
void *client_rx_sock = zlink_receiver_router_socket_unsafe(client_rx);
zlink_msg_recv(&rid_frame, client_rx_sock, 0);
zlink_msg_recv(&payload_frame, client_rx_sock, 0);
++recv_count;
```

서비스 네이밍:
- 서버 서비스명: `"perf-server"` (고정)
- 클라이언트 서비스명: `"perf-client-{N}"` (클라이언트 인덱스별)

측정:
- throughput 단위: `ops/s` (1 op = 요청 전송 + 응답 수신 1회)
- throughput 계측 지점: 클라이언트 측 recv_count 기준
- latency: 요청 payload에 timestamp 삽입, 응답 수신 시 round-trip ÷2

### 5.2 MULTI_SPOT 변경 여부

`MULTI_SPOT`은 **one-way를 유지**한다.

사유:
- SPOT은 subscribe/publish 패턴으로 설계됨
- 서버가 일방 발행, 클라이언트가 구독 수신하는 구조가 패턴 본래 의도
- Echo 전환 시 SPOT의 subscribe 기반 토폴로지와 충돌

변경 사항:
- 코드 변경 없음 (one-way 유지)
- `run_comparison.py`에서 one-way 분류 유지
- send 정책만 blocking으로 전환 (§3.3 적용)

### 5.3 메시지 크기 반영 버그 수정

현재 문제:
- 일부 경로에서 `max_size`(버퍼 capacity)를 전송 길이로 사용
- 결과: 64B와 1024B의 throughput이 동일하게 측정됨 (평탄화)

수정:
- 전송/수신 길이를 항상 `current_msg_size`로 고정한다.
- `max_size` 버퍼는 capacity 용도로만 사용한다.
- 결과적으로 size별 부하 차이가 실제 메트릭에 반영되도록 한다.

### 5.4 throughput 집계 기준 고정

현재 상태:
- 각 worker가 `recv_count`를 개별 누적
- 최종 집계 시 전체 worker의 recv_count를 합산하여 duration으로 나눔
- 이 구조 자체는 올바르나, 일부 패턴에서 worker별 평균으로 잘못 계산하는 경로가 존재

변경:
- 모든 패턴에서 동일한 집계 공식을 사용하도록 통일한다.
- 공식: `throughput = sum(all worker recv_count) / duration_seconds`
- Echo 패턴: 1 recv = 1 op (요청+응답 완료)
- One-way 패턴: 1 recv = 1 msg

### 5.5 hot path 성능 정리

현재 hot path 상태:
- `scratch` 벡터: 선할당, 재사용 (양호)
- `latency_sampler`: reservoir sampling, 1회 reserve 후 교체 방식 (양호)
- `poll_items` 배열: worker별 선할당 (양호)

제거/개선 대상:
- `ZLINK_DONTWAIT` send 경로의 EAGAIN 분기 + backoff sleep → blocking으로 전환하면 제거됨
- `classify_send_result()` 분기 → blocking에서는 OK/ERROR 이분법으로 단순화
- 서버 `publish_once()`의 ZLINK_DONTWAIT + EAGAIN 루프 → Echo 전환 시 blocking recv-send로 교체
- 측정 루프 중 상세 로그 출력 금지(phase 종료 후 집계 출력)

### 5.6 메모리 폭증(WSL) 대응 규칙

`MULTI_GATEWAY`에서 `clients=N`은 gateway+receiver 쌍 N개를 의미한다.
이는 gateway 패턴의 본래 구조이며, 추가 오버헤드가 아니다.

소켓 매핑:
- 클라이언트 1개 = gateway 1 + receiver 1 (패턴 고유 구조)
- 클라이언트 N개 = gateway N + receiver N
- 서버 = gateway 1 + receiver 1
- 총 소켓 수: `2N + 2`

메모리 안정성 보장 포인트:
- 고정 크기 재사용 버퍼 (루프 내 동적할당 금지)
- 크기 전환 시 재할당 최소화(`reserve` 기반 확장만 허용)
- 불필요 큐 누적/백로그 확대 경로 제거
- 소켓당 메모리 사용량을 HWM으로 제한 (HWM 초과 시 drop, 무한 큐잉 방지)
- WSL 환경에서 `clients=1000` 기준 OOM이 발생하지 않아야 한다. 발생 시 원인을 분석하고 코드 수준에서 해결한다 (N 축소는 우회이며 해결이 아님).

## 6. Echo/One-way 분류와 메트릭 정합화

### 6.1 패턴 분류 (변경 후)

| 패턴 | 분류 | throughput 단위 | bandwidth 계수 | latency 모드 | 비고 |
|---|---|---|---|---|---|
| `MULTI_DEALER_DEALER` | **One-way** (수정) | **msg/s** (수정) | **×1** (수정) | one-way (timestamp) | 주1 |
| `MULTI_DEALER_ROUTER` | Echo | ops/s | ×2 | round-trip ÷2 | |
| `MULTI_ROUTER_ROUTER` | Echo | ops/s | ×2 | round-trip ÷2 | |
| `MULTI_PUBSUB` | One-way | msg/s | ×1 | one-way (timestamp) | |
| `MULTI_GATEWAY` | **Echo** (변경) | **ops/s** (변경) | **×2** (변경) | **round-trip ÷2** | |
| `MULTI_SPOT` | One-way (유지) | msg/s | ×1 | one-way (timestamp) | |
| `MULTI_STREAM` | Echo | ops/s | ×2 | round-trip ÷2 | |
| `MULTI_STREAM_CALLBACK` | Echo | ops/s | ×2 | round-trip ÷2 | |
| `MULTI_STREAM_LEN32BE` | Echo | ops/s | ×2 | round-trip ÷2 | |

**주1: MULTI_DEALER_DEALER — One-way로 재분류**
- DEALER 소켓은 send 시 Load Balancer(round-robin)를 사용하며, 특정 peer를 지정할 수 없다.
- 서버가 `recv()` → `send()` 해도 응답이 원래 sender가 아닌 round-robin 다음 peer로 간다.
- 따라서 true Echo(요청자에게 응답 반환)가 불가능하다.
- `run_comparison.py`의 `MULTI_ECHO_PATTERNS`에서 `MULTI_DEALER_DEALER` 제거 필요.
- 서버 측 `relay_dealer_once()`는 제거하고 one-way로 전환한다.
- **방향: server → client** (서버가 DEALER 소켓으로 발행, 클라이언트가 DEALER 소켓으로 수신)
- throughput 측정 지점: **클라이언트 측 recv_count** (수신 완료 기준)
- 정책 문서(`PERF_MULTI_TEST_POLICY.md`)의 one-way 분류가 맞음.

### 6.2 메트릭 공식

- Echo throughput: `ops/s`
- One-way throughput: `msg/s`
- Echo bandwidth: `throughput * msg_size * 2 / 1_000_000` (MB/s)
- One-way bandwidth: `throughput * msg_size / 1_000_000` (MB/s)

### 6.3 반영 대상

- `core/perf/run_comparison.py`: `MULTI_ECHO_PATTERNS`에 `MULTI_GATEWAY` 추가
- `core/perf/PERF_MULTI_TEST_POLICY.md`: 패턴 분류표 갱신
- 멀티 실행/결과 가이드: 단위 표기 통일

## 7. 코드/문서 정리 및 파일 삭제 정책

### 7.1 inflight 제거

아래를 전부 제거한다:
- inflight 관련 코드 분기
- inflight 관련 환경변수
- inflight 관련 정책/문서 문구

### 7.2 제거 대상 환경변수 목록

| 환경변수 | 사유 |
|---|---|
| `PERF_MULTI_BLOCKING_SEND` | blocking이 기본이 되므로 분기 불필요 |
| `PERF_MULTI_SEND_BACKOFF_US` | blocking send에서는 backoff 불필요 |
| `PERF_MULTI_CLIENT_IDLE_SLEEP_US` | blocking recv에서는 idle backoff 불필요 |
| `PERF_PROFILE` 기반 blocking 분기 | 제거 |

### 7.3 미사용 파일 삭제 순서

1차 삭제 기준:
- CMake 타겟 미참조
- runner/script 미참조

2차 동기화:
- 정책/문서 참조 정리
- 삭제 반영 문서 업데이트

즉시 삭제 조건:
- 1차 기준 충족 시 코드 파일은 즉시 삭제
- 문서는 같은 변경셋에서 동기화한다

## 8. 실행 단계 (Phase)

각 Phase 완료 후 빌드 검증을 수행하고, 실패 시 해당 Phase 내에서 수정한다.

### Phase 1: Send/Recv 정책 통일 + DEALER_DEALER One-way 전환

범위: 전 패턴 (9개)

작업:
1. `ZLINK_DONTWAIT` 송신 경로를 blocking send로 전환
2. `bench_send_flags()`, `classify_send_result()` EAGAIN 분기 단순화
3. `PERF_MULTI_BLOCKING_SEND`, `PERF_MULTI_SEND_BACKOFF_US`, `PERF_MULTI_CLIENT_IDLE_SLEEP_US` 환경변수 제거
4. 서버 `publish_once()`의 ZLINK_DONTWAIT → blocking send 전환
5. recv 경로가 §3.1 정책에 맞는지 확인, 불일치 시 수정
6. pubsub `nodrop` 기본 적용 확인
7. `MULTI_DEALER_DEALER`: 서버 `relay_dealer_once()` 제거, one-way(서버 발행) 구조로 전환
8. `run_comparison.py`의 `MULTI_ECHO_PATTERNS`에서 `MULTI_DEALER_DEALER` 제거

검증: 전 패턴 빌드 성공

### Phase 2: 메시지 크기 버그 수정

범위: 전 패턴

작업:
1. 전송 길이가 `current_msg_size`를 사용하는지 전수 확인
2. `max_size`가 capacity로만 사용되는지 확인
3. 수정 후 size별 throughput 곡선이 차이를 보이는지 스모크 확인

검증: 빌드 성공 + 64B vs 1024B throughput 차이 확인

### Phase 3: GATEWAY Echo 전환

범위: `MULTI_GATEWAY` 서버/클라이언트

작업:
1. 서버 구조 변경: receiver(recv) + gateway(send) 쌍 구성
2. 클라이언트 구조 변경: gateway(send) + receiver(recv) 쌍 구성
3. 서비스 네이밍 구현 (`perf-server`, `perf-client-{N}`)
4. `run_comparison.py`의 `MULTI_ECHO_PATTERNS`에 `MULTI_GATEWAY` 추가
5. throughput/bandwidth 계산이 Echo 공식(×2)으로 전환되는지 확인

검증: GATEWAY 단독 빌드 + 스모크 (tcp, 64B)

### Phase 4: throughput 집계 통일

범위: 전 패턴

작업:
1. 모든 패턴의 throughput 계산이 `sum(worker recv_count) / duration` 공식인지 확인
2. worker별 평균을 쓰는 경로가 있으면 수정
3. Echo/One-way 단위(ops vs msg) 표기가 올바른지 확인

검증: 빌드 성공

### Phase 5: inflight/미사용 코드 정리

범위: 전 파일

작업:
1. §7.1 inflight 관련 코드/환경변수/문서 제거
2. §7.2 환경변수 목록 제거
3. §7.3 미사용 파일 삭제

검증: 빌드 성공 + 환경변수 grep 결과 0건

### Phase 6: 문서/러너 동기화

범위: 문서 + 스크립트

작업:
1. `core/perf/PERF_MULTI_TEST_POLICY.md` 갱신:
   - 패턴 분류표 (§8.1): DEALER_DEALER → one-way, GATEWAY → Echo
   - 메트릭/latency 규칙 (§8.3, §9.1, §9.3): throughput 단위/latency divisor 갱신
   - 환경변수 목록 (§12.4): 제거된 변수 반영
   - 표준 메시지 크기 (§11.2): STREAM* msg size 분리 반영
2. `core/perf/run_comparison.py` Echo/One-way 분류 갱신
3. 멀티 벤치 가이드 문서 갱신
4. AGENTS.md stub 파일 금지 원칙 문구 수정 (§4.5 반영)

검증: 문서 내 분류/환경변수/메트릭 정의가 코드와 일치

### Phase 7: 전체 스모크 + 성능 검증

범위: 전 패턴

작업:
1. §9.2 검증 매트릭스 전체 실행
2. 결과 리포트 생성

검증: §9 완료 기준 전항목 통과

## 9. 검증 계획

### 9.1 빌드 검증

- multi 벤치 타겟 전체 빌드
- 리팩토링 대상 패턴 빌드 실패/경고 증가 여부 확인

### 9.2 스모크 검증

공식 러너:
- 메인: `core/perf/run_benchmarks_multi.sh`
- 보조: `core/perf/multi/run_benchmarks.sh`
- 둘 다 사용 가능하며, 공식 결과는 메인 러너 기준으로 생성한다.

실행 순서:
1. `MULTI_GATEWAY` 단독 스모크
2. 전체 패턴 스모크

검증 매트릭스:
- transport: `tcp,tls,ws,wss`

msg sizes (패턴별):
- 기본: `64,256,1024,65536,131072,262144`
- `MULTI_STREAM*` (3종): `64,256,1024,65536` (stream 프레이밍 제약)

### 9.3 정책 준수 검증

- send 경로가 모든 패턴에서 blocking인지 확인
- recv 경로가 정책(§3.1)을 만족하는지 확인
- poll 패턴이 `poll + recv + batch drain`인지 확인
- pubsub nodrop 기본 적용 확인

### 9.4 결과/안정성 검증

- 필수 메트릭(`throughput/bandwidth/latency/p95/p99`) 누락 없음
- size 증가에 따른 곡선이 비정상 평탄화되지 않음
- 동일 조건 반복 시 WSL 메모리 폭증/OOM 재발 없음

## 10. 완료 기준 (Acceptance Criteria)

- multi 전체 패턴에 `recv/send/pubsub` 정책이 일관 적용됨
- `MULTI_GATEWAY`가 gateway/receiver Echo 모델로 동작함
- `MULTI_SPOT`은 one-way로 유지됨
- throughput이 전체 클라이언트 집계 기준으로 계산됨
- inflight 관련 흔적이 코드/문서에 남지 않음
- `hwm/sndhwm/rcvhwm` 정책 변경 없음
- 스모크 결과 메트릭/산출물이 정상 생성됨
- 미사용 파일 정리가 삭제 정책에 맞게 완료됨
- stub 파일(`#include` 한 줄 위임) 없음
