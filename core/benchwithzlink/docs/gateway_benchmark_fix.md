# Gateway 벤치마크 수정 보고서

**작성일**: 2026-02-03
**수정 파일**: `core/benchwithzlink/current/bench_current_gateway.cpp`

---

## 1. 문제 요약

| 항목 | 상태 |
|------|------|
| `test_gateway` (유닛 테스트) | PASS |
| `comp_current_gateway` (벤치마크) | FAIL (throughput=0, latency=0) |

동일한 Gateway/Provider 구조를 사용하는데, 테스트는 통과하고 벤치마크만 실패하는 현상이 발생했습니다.

---

## 2. Gateway 아키텍처 개요

### 2.1 구성 요소

```
┌─────────────────────────────────────────────────────────────────┐
│                         zlink Context                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────┐         ┌──────────┐         ┌──────────┐        │
│  │ Registry │◄────────│ Discovery│────────►│ Provider │        │
│  │  (PUB)   │ notify  │          │ query   │          │        │
│  │ (ROUTER) │         └──────────┘         └──────────┘        │
│  └──────────┘              │                    │               │
│                            │                    │               │
│                            ▼                    ▼               │
│                      ┌──────────┐         ┌──────────┐         │
│                      │ Gateway  │◄───────►│ Provider │         │
│                      │ (ROUTER) │  ROUTER │ (ROUTER) │         │
│                      └──────────┘         └──────────┘         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 컴포넌트 역할

| 컴포넌트 | 역할 | 소켓 타입 |
|----------|------|-----------|
| **Registry** | 서비스 등록 정보 관리 및 브로드캐스트 | PUB + ROUTER |
| **Discovery** | Registry로부터 서비스 정보 수신 | SUB |
| **Provider** | 서비스 제공자, 요청 수신 및 응답 | ROUTER |
| **Gateway** | 클라이언트 측, Discovery 기반 라우팅 | ROUTER (서비스별) |

### 2.3 메시지 흐름

```
[Client Request Flow]

  Gateway                                    Provider
     │                                           │
     │  1. zlink_gateway_send(service, msg)     │
     │  ────────────────────────────────────►   │
     │     [routing_id][payload...]             │
     │                                           │
     │  2. Provider receives via ROUTER         │
     │     pump_provider():                     │
     │       - recv routing_id (MORE flag)      │
     │       - recv payload                     │
     │                                           │
     │  3. Provider recv-only (no reply)        │
     ▼                                           ▼
```

### 2.4 ROUTER-ROUTER 통신 특성

ROUTER 소켓 간 통신에서는 **routing_id** 프레임이 필수입니다:

```
송신 측:
  Frame 1: [routing_id]     (SNDMORE)
  Frame 2: [payload]        (마지막 프레임)

수신 측:
  Frame 1: [peer_routing_id] (MORE flag 설정됨)
  Frame 2: [payload]         (MORE flag 없음)
```

---

## 3. zlink API 반환값 규칙

### 3.1 Low-level 메시지 API

| 함수 | 성공 시 반환값 | 실패 시 반환값 |
|------|---------------|---------------|
| `zlink_msg_send()` | **메시지 크기 (≥0)** | -1 |
| `zlink_msg_recv()` | **메시지 크기 (≥0)** | -1 |

**구현 코드** (`core/src/api/zlink.cpp`):

```cpp
static inline int s_sendmsg(socket_handle_t handle_, zlink_msg_t *msg_, int flags_)
{
    size_t sz = zlink_msg_size(msg_);
    int rc = handle_.socket->send(reinterpret_cast<zlink::msg_t *>(msg_), flags_);
    if (unlikely(rc < 0))
        return -1;

    size_t max_msgsz = INT_MAX;
    return static_cast<int>(sz < max_msgsz ? sz : max_msgsz);  // 크기 반환!
}

static int s_recvmsg(socket_handle_t handle_, zlink_msg_t *msg_, int flags_)
{
    int rc = handle_.socket->recv(reinterpret_cast<zlink::msg_t *>(msg_), flags_);
    if (unlikely(rc < 0))
        return -1;

    const size_t sz = zlink_msg_size(msg_);
    return static_cast<int>(sz < INT_MAX ? sz : INT_MAX);  // 크기 반환!
}
```

### 3.2 High-level Gateway/Spot API

| 함수 | 성공 시 반환값 | 실패 시 반환값 |
|------|---------------|---------------|
| `zlink_gateway_send()` | **0** | -1 |
| `zlink_spot_publish()` | **0** | -1 |
| `zlink_spot_recv()` | **0** | -1 |

---

## 4. 테스트 vs 벤치마크 코드 비교

### 4.1 테스트 코드 (PASS)

**파일**: `core/tests/discovery/test_gateway.cpp`

```cpp
// 테스트에서 사용하는 recv_one_with_timeout
static void recv_one_with_timeout(void *sock, zlink_msg_t *msg, int timeout_ms)
{
    zlink_pollitem_t items[1];
    items[0].socket = sock;
    items[0].events = ZLINK_POLLIN;
    int rc = zlink_poll(items, 1, timeout_ms);
    TEST_ASSERT_TRUE_MESSAGE(rc > 0, "recv timeout");
    TEST_ASSERT_SUCCESS_ERRNO(zlink_msg_recv(msg, sock, 0));
}
```

**`TEST_ASSERT_SUCCESS_ERRNO` 매크로** (`core/tests/testutil_unity.cpp`):

```cpp
#define TEST_ASSERT_SUCCESS_ERRNO(expr)                    \
    do {                                                   \
        int rc_ = (expr);                                  \
        if (rc_ == -1) {  // ← 실패 조건: -1                \
            char msg_[256];                                \
            snprintf(msg_, sizeof(msg_),                   \
                     "Assertion failed: %s returned -1",   \
                     #expr);                               \
            TEST_FAIL_MESSAGE(msg_);                       \
        }                                                  \
    } while (0)
```

**핵심**: `rc_ == -1`로 실패를 판단 → **올바른 체크**

### 4.2 벤치마크 코드 (FAIL - 수정 전)

**파일**: `core/benchwithzlink/current/bench_current_gateway.cpp`

```cpp
// 벤치마크에서 사용하던 recv_one_with_timeout (수정 전)
static bool recv_one_with_timeout(void *socket, zlink_msg_t *msg, int timeout_ms)
{
    zlink_pollitem_t items[1];
    items[0].socket = socket;
    items[0].fd = 0;
    items[0].events = ZLINK_POLLIN;
    items[0].revents = 0;

    int rc = zlink_poll(items, 1, timeout_ms);
    if (rc <= 0)
        return false;

    rc = zlink_msg_recv(msg, socket, 0);
    if (rc != 0) {        // ← 문제: 크기가 0이 아니면 실패로 처리!
        return false;
    }
    return true;
}

// pump_provider에서 reply 전송 (수정 전)
if (zlink_msg_send(&rid, router, ZLINK_SNDMORE) != 0) {  // ← 문제!
    return false;
}
if (zlink_msg_send(&reply, router, 0) != 0) {            // ← 문제!
    return false;
}
```

**핵심**: `rc != 0`로 실패를 판단 → **잘못된 체크**

---

## 5. 실패 시나리오 분석

### 5.1 실패 흐름

```
1. Gateway가 Provider에게 64바이트 메시지 전송
   └─ zlink_gateway_send() → 0 반환 (성공)

2. Provider의 pump_provider() 호출
   ├─ recv_one_with_timeout(router, &rid, ...)
   │   └─ zlink_msg_recv() → 5 반환 (routing_id 5바이트)
   │   └─ 벤치마크: if (5 != 0) → return false  ← 여기서 실패!
   │
   └─ (실행되지 않음)

3. prime_gateway() 타임아웃으로 실패
   └─ throughput=0, latency=0 출력
```

### 5.2 디버그 출력 분석

```
provider router after prime peer[0] recv=754
RESULT,current,GATEWAY,tcp,64,throughput,0.00
```

- `recv=754`: Provider의 내부 소켓은 754개의 메시지를 수신함
- 하지만 벤치마크 레벨에서 반환값 체크 실패로 인해 결과는 0

### 5.3 근본 원인

| 비교 항목 | 테스트 | 벤치마크 (수정 전) |
|-----------|--------|-------------------|
| 반환값 체크 | `rc == -1` (실패) | `rc != 0` (실패) |
| routing_id recv (5 bytes) | 5 ≠ -1 → 성공 | 5 ≠ 0 → **실패** |
| payload recv (64 bytes) | 64 ≠ -1 → 성공 | (도달 안 함) |

---

## 6. 수정 내역

### 6.1 recv_one_with_timeout 함수 (Line 130)

```diff
 rc = zlink_msg_recv(msg, socket, 0);
-if (rc != 0) {
+if (rc < 0) {
     if (bench_debug_enabled()) {
         std::cerr << "recv_one_with_timeout: recv after poll failed errno="
                   << zlink_errno() << std::endl;
     }
     return false;
 }
```

### 6.2 pump_provider 함수 (Lines 221, 227)

```diff
 if (replyable && *replyable) {
-    if (zlink_msg_send(&rid, router, ZLINK_SNDMORE) != 0) {
+    if (zlink_msg_send(&rid, router, ZLINK_SNDMORE) < 0) {
         zlink_msg_close(&rid);
         zlink_msg_close(&payload);
         zlink_msg_close(&reply);
         return false;
     }
-    if (zlink_msg_send(&reply, router, 0) != 0) {
+    if (zlink_msg_send(&reply, router, 0) < 0) {
         zlink_msg_close(&rid);
         zlink_msg_close(&payload);
         zlink_msg_close(&reply);
         return false;
     }
 }
```

---

## 7. 수정 후 결과

### 7.1 벤치마크 실행 결과

```bash
$ ./bin/comp_current_gateway current tcp 64
RESULT,current,GATEWAY,tcp,64,throughput,184.92
RESULT,current,GATEWAY,tcp,64,latency,5524.10
```

| 메트릭 | 값 | 단위 |
|--------|-----|------|
| Throughput | 184.92 | msg/s |
| Latency | 5524.10 | μs |

### 7.2 기존 테스트 영향

```bash
$ ctest --output-on-failure -R test_gateway
# 모든 테스트 PASS (변경 없음)
```

---

## 8. 교훈 및 권장 사항

### 8.1 API 반환값 규칙

zlink API 사용 시 반환값 체크 규칙:

```cpp
// Low-level 메시지 API (zlink_msg_send/recv)
int rc = zlink_msg_send(&msg, socket, flags);
if (rc < 0) {          // 실패: -1
    // 에러 처리
}
// 성공: rc >= 0 (메시지 크기)

// High-level API (gateway/spot)
int rc = zlink_gateway_send(gw, service, parts, count, 0);
if (rc != 0) {         // 실패: -1, 성공: 0
    // 에러 처리
}
```

### 8.2 권장 코딩 패턴

```cpp
// 권장: 명시적 실패 조건
if (zlink_msg_recv(&msg, socket, 0) < 0) {
    // 실패 처리
}

// 비권장: 모호한 성공 조건
if (zlink_msg_recv(&msg, socket, 0) != 0) {  // 위험!
    // 실패 처리
}
```

### 8.3 테스트 매크로 참고

새로운 테스트/벤치마크 작성 시 `testutil_unity.cpp`의 매크로 패턴 참고:

```cpp
#define TEST_ASSERT_SUCCESS_ERRNO(expr)  \
    do { if ((expr) == -1) TEST_FAIL(); } while(0)
```

---

## 9. 관련 파일

| 파일 | 설명 |
|------|------|
| `core/benchwithzlink/current/bench_current_gateway.cpp` | 수정된 벤치마크 |
| `core/src/api/zlink.cpp` | zlink API 구현 (s_sendmsg, s_recvmsg) |
| `core/src/discovery/gateway.cpp` | Gateway 구현체 |
| `core/tests/discovery/test_gateway.cpp` | Gateway 유닛 테스트 |
| `core/tests/testutil_unity.cpp` | 테스트 유틸리티 매크로 |

---

## 10. 변경 이력

| 날짜 | 변경 내용 |
|------|-----------|
| 2026-02-03 | `recv_one_with_timeout`: `rc != 0` → `rc < 0` |
| 2026-02-03 | `pump_provider`: `zlink_msg_send` 반환값 체크 수정 |
