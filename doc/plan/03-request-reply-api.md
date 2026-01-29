# Request/Reply API 스펙 (Request/Reply API Extension)

> **우선순위**: 3 (Core Feature)
> **상태**: Draft
> **버전**: 1.0
> **의존성**:
> - [00-routing-id-unification.md](00-routing-id-unification.md) (routing_id_t)
> - [02-thread-safe-socket.md](02-thread-safe-socket.md) (thread-safe 소켓)

## 목차
1. [개요](#1-개요)
2. [지원 소켓 타입](#2-지원-소켓-타입)
3. [지원 패턴](#3-지원-패턴)
4. [제안 API](#4-제안-api)
5. [내부 구현](#5-내부-구현)
6. [사용 예시](#6-사용-예시)
7. [구현 영향 범위](#7-구현-영향-범위)
8. [검증 방법](#8-검증-방법)

---

## 1. 개요

### 1.1 배경

zlink는 기존의 **REQ/REP 소켓을 제거**하고, ROUTER/DEALER 소켓에 Request-Reply 기능을 직접 통합한다. 이를 통해 더 높은 유연성과 성능을 제공한다.

**기존 REQ/REP의 문제점:**
- 엄격한 send-recv-send-recv 순서 강제
- 한 요청이 실패하면 소켓이 "stuck" 상태
- 비동기 처리 어려움
- 다중 요청 동시 처리 불가

### 1.2 목표

**Routing ID 처리를 자동화**하여 사용자가 Envelope 관리에 신경 쓰지 않고도 간단한 API로 비동기 요청-응답 패턴을 구현할 수 있게 한다.

### 1.3 핵심 장점

| 장점 | 설명 |
|------|------|
| **단순성** | 복잡한 ID 프레임 처리를 API 내부로 캡슐화 |
| **유연성** | DEALER/ROUTER 모두 클라이언트/서버 역할 가능 |
| **비동기 최적화** | ASIO 기반 콜백으로 성능 저하 없이 Request-Reply 제공 |
| **다중 요청** | 여러 요청을 동시에 처리 가능 (파이프라인) |
| **Multipart** | 헤더/바디 분리 등 1..N 프레임 요청/응답 지원 |

---

## 2. 지원 소켓 타입

이 확장 API는 다음 두 가지 소켓 타입에서만 지원된다.  
또한 **thread-safe 소켓에서만 활성화된다**.

| 소켓 타입 | 역할 | 설명 |
|----------|------|------|
| **ZLINK_ROUTER (thread-safe)** | Server / Client | 가장 범용적. `reply()`로 특정 클라이언트에 응답, `request(target)`으로 특정 서버에 요청 |
| **ZLINK_DEALER (thread-safe)** | Client | 주로 클라이언트용. `request()` 호출 시 연결된 피어들에게 로드 밸런싱 |

> **주의**:
> - PUB, SUB, PAIR, XPUB, XSUB 등 다른 소켓 타입에서는 이 API를 사용할 수 없다.
> - thread-safe가 아닌 소켓에서 호출 시 `ENOTSUP`로 실패한다.
> - 생성은 `zlink_socket_threadsafe(ctx, type)` 사용을 권장한다.
> - 대상 지정 API는 `zlink_routing_id_t` 기반으로 동작한다.

---

## 3. 지원 패턴

### 3.1 Dealer-to-Router (Load Balanced Request)

```
┌─────────┐                      ┌─────────┐
│ DEALER  │ ─── request() ────→  │ ROUTER  │
│ Client  │                      │ Server  │
│         │ ←── reply() ───────  │         │
└─────────┘                      └─────────┘
           (Load Balancing)
```

- 클라이언트(DEALER)가 대상을 지정하지 않고 요청
- 연결된 서버 중 하나로 **로드 밸런싱**
- 서버는 요청한 클라이언트에게 `reply()`

### 3.2 Router-to-Router (Targeted Request)

```
┌─────────┐                      ┌─────────┐
│ ROUTER  │ ─ request(target) ─→ │ ROUTER  │
│ Client  │                      │ Server  │
│         │ ←── reply() ───────  │         │
└─────────┘                      └─────────┘
           (Targeted by routing_id)
```

- 클라이언트(ROUTER)가 **특정 Routing ID**를 지정하여 요청
- 서버는 동일하게 `reply()`로 응답

### 3.3 Multiple Dealers to Router (Fan-in)

```
┌─────────┐
│ DEALER  │ ────┐
│ Client1 │     │
└─────────┘     │              ┌─────────┐
                ├─ request() → │ ROUTER  │
┌─────────┐     │              │ Server  │
│ DEALER  │ ────┘              └─────────┘
│ Client2 │
└─────────┘
```

- 여러 DEALER 클라이언트가 하나의 ROUTER 서버에 요청
- 서버는 각 클라이언트의 routing_id로 개별 응답

### 3.4 Request Pipeline (다중 동시 요청)

```
Client                          Server
  │                               │
  ├─ request(msg1, cb1) ────────→ │
  ├─ request(msg2, cb2) ────────→ │  (동시에 여러 요청)
  ├─ request(msg3, cb3) ────────→ │
  │                               │
  │ ←────────── reply(msg1) ───── │
  │ ←────────── reply(msg3) ───── │  (순서 무관 응답)
  │ ←────────── reply(msg2) ───── │
```

### 3.5 Group Request (그룹 단위 순서 보장)

- `group_id`별로 **inflight=1**을 보장한다.
- 같은 `group_id`의 요청은 **응답 순서(요청 순서)**가 보장된다.
- `group_id=0`은 기본 그룹(기존 동작과 동일)으로 취급한다.
- 그룹 내 첫 요청이 지연/타임아웃되면 **해당 그룹이 대기**하므로,
  타임아웃 설정이 필수다.
- C++ 오버로드에서는 timeout 생략 시 `ZLINK_REQUEST_TIMEOUT` 기본값(5000ms)이 적용된다.
- C API에서는 timeout_ms에 `ZLINK_REQUEST_TIMEOUT_DEFAULT`를 전달하면 기본값이 적용된다.

---

## 4. 제안 API

### 4.1 C++ API

```cpp
namespace zlink {

// routing_id는 zlink_routing_id_t 기반으로 처리
using routing_id_t = zlink_routing_id_t;
using msg_vec_t = std::vector<msg_t>;

class thread_safe_socket_t {
public:
    // ============================================================
    // Client Side API
    // ============================================================

    // 단일 프레임 Request (Load-balancing)
    void request(
        msg_t&& payload,
        std::function<void(msg_t& reply)> callback
    );

    // 단일 프레임 Request (Targeted)
    void request(
        const routing_id_t &target_id,
        msg_t&& payload,
        std::function<void(msg_t& reply)> callback
    );

    // 단일 프레임 Request + timeout
    void request(
        msg_t&& payload,
        std::function<void(msg_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    void request(
        const routing_id_t &target_id,
        msg_t&& payload,
        std::function<void(msg_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    // multipart Request
    void request(
        msg_vec_t&& parts,
        std::function<void(msg_vec_t& reply)> callback
    );

    void request(
        const routing_id_t &target_id,
        msg_vec_t&& parts,
        std::function<void(msg_vec_t& reply)> callback
    );

    // multipart Request + timeout
    void request(
        msg_vec_t&& parts,
        std::function<void(msg_vec_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    void request(
        const routing_id_t &target_id,
        msg_vec_t&& parts,
        std::function<void(msg_vec_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    // 그룹 단위 순서 보장 요청 (단일 프레임)
    void group_request(
        uint64_t group_id,
        msg_t&& payload,
        std::function<void(msg_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    void group_request(
        const routing_id_t &target_id,
        uint64_t group_id,
        msg_t&& payload,
        std::function<void(msg_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    void group_request(
        uint64_t group_id,
        msg_t&& payload,
        std::function<void(msg_t* reply, int error)> callback
    );

    void group_request(
        const routing_id_t &target_id,
        uint64_t group_id,
        msg_t&& payload,
        std::function<void(msg_t* reply, int error)> callback
    );

    // 그룹 단위 순서 보장 요청 (multipart)
    void group_request(
        uint64_t group_id,
        msg_vec_t&& parts,
        std::function<void(msg_vec_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    void group_request(
        const routing_id_t &target_id,
        uint64_t group_id,
        msg_vec_t&& parts,
        std::function<void(msg_vec_t* reply, int error)> callback,
        std::chrono::milliseconds timeout
    );

    void group_request(
        uint64_t group_id,
        msg_vec_t&& parts,
        std::function<void(msg_vec_t* reply, int error)> callback
    );

    void group_request(
        const routing_id_t &target_id,
        uint64_t group_id,
        msg_vec_t&& parts,
        std::function<void(msg_vec_t* reply, int error)> callback
    );

    // ============================================================
    // Server Side API
    // ============================================================

    // 요청 수신 핸들러 등록 (multipart 포함)
    void on_request(
        std::function<void(msg_vec_t& parts,
                           const routing_id_t &src_id,
                           uint64_t request_id)> handler
    );

    // 특정 ID에게 응답 전송 (단일/멀티)
    void reply(const routing_id_t &id, uint64_t request_id, msg_t&& payload);
    void reply(const routing_id_t &id, uint64_t request_id, msg_vec_t&& parts);

    // ============================================================
    // Pending Request Management
    // ============================================================

    // 대기 중인 요청 수 조회
    size_t pending_requests() const;

    // 모든 대기 요청 취소
    void cancel_all_requests();
};

} // namespace zlink
```

**C++ 소유권 규칙 요약:**
- `msg_t`/`msg_vec_t` 인자는 **성공 시 라이브러리로 소유권 이전**.
- 콜백의 `msg_vec_t&`/`msg_vec_t*`는 **콜백 범위 내에서만 유효**.
- 응답을 보관하려면 콜백 내부에서 **복사/이동**한다.
- `msg_t` 오버로드는 **단일 프레임 전용**이며, multipart 응답은 첫 프레임만 전달됨.

### 4.2 C API 상세 명세 (zlink.h 추가 내용)

```c
/* ============================================================
 * Request/Reply API - Thread-safe Requirement
 * ============================================================ *
 *
 * 모든 zlink_request* / zlink_reply* / zlink_request_recv 계열은
 * thread-safe 소켓에서만 동작한다.
 * (zlink_socket_threadsafe()로 생성된 소켓)
 */

/* ============================================================
 * Request/Reply API - Constants & Macros
 * ============================================================ */

/* 요청 관련 소켓 옵션 */
#define ZLINK_REQUEST_TIMEOUT         90   /* 기본 요청 타임아웃 (ms, 기본값=5000) */
#define ZLINK_REQUEST_CORRELATE       91   /* 요청-응답 자동 상관관계 활성화 */
/* 요청 타임아웃 상수 */
#define ZLINK_REQUEST_TIMEOUT_DEFAULT -2   /* 소켓 기본값 사용 */

/* 에러 코드 (errno에 설정됨) */
#define ETIMEDOUT_ZLINK   110   /* 요청 타임아웃 */
#define ECANCELED_ZLINK   125   /* 요청 취소됨 */

/* ============================================================
 * Request/Reply API - Type Definitions
 * ============================================================ */

/*
 * 응답 수신 콜백
 *
 * @param request_id  요청 ID (zlink_request*() 반환값)
 * @param reply_parts 응답 메시지 배열 (에러 시 NULL)
 * @param reply_count 응답 프레임 개수
 * @param error       0: 성공
 *                    ETIMEDOUT: 타임아웃
 *                    ECANCELED: 취소됨
 *                    ECONNRESET: 연결 끊김
 *                    EHOSTUNREACH: 피어 없음
 *
 * 참고:
 *   - request_id를 키로 사용하여 애플리케이션 컨텍스트 관리 가능
 *   - reply_parts는 호출자 소유, 사용 후 zlink_msgv_close() 필요
 *   - reply_parts는 콜백 반환 전까지 유효하며, 보관하려면 복사/이동 필요
 *   - 에러 시 reply_parts=NULL, reply_count=0
 *   - 언어 바인딩에서는 zlink_request_recv() 폴링 API 사용 권장
 */
typedef void (*zlink_request_cb_fn)(
    uint64_t request_id,
    zlink_msg_t *reply_parts,
    size_t reply_count,
    int error
);

/*
 * 요청 수신 핸들러 (서버측)
 *
 * @param request_parts  수신된 요청 메시지 배열
 * @param part_count     수신된 프레임 개수
 * @param routing_id     요청자의 routing_id (zlink_routing_id_t)
 * @param request_id     요청 ID (응답 시 사용)
 *
 * 참고:
 *   - request_parts 소유권은 핸들러에게 있음
 *   - request_parts는 핸들러 반환 전까지 유효하며, 사용 후 zlink_msgv_close() 필요
 *   - 애플리케이션 컨텍스트는 전역 또는 소켓별 상태로 관리
 */
typedef void (*zlink_server_cb_fn)(
    zlink_msg_t *request_parts,
    size_t part_count,
    const zlink_routing_id_t *routing_id,
    uint64_t request_id
);

/* ============================================================
 * Request/Reply API - Client Functions
 * ============================================================ */

/*
 * zlink_request - 요청 전송 (routing_id 선택)
 *
 * @param socket      DEALER 또는 ROUTER 소켓
 * @param routing_id  대상의 routing_id (NULL이면 기본 동작)
 * @param parts       요청 메시지 배열 (소유권 이전됨)
 * @param part_count  요청 프레임 개수 (>= 1)
 * @param callback    응답 수신 콜백 (필수)
 * @param timeout_ms  타임아웃 (밀리초)
 *                   - ZLINK_REQUEST_TIMEOUT_DEFAULT: 소켓 기본값 사용
 *                   - -1: 무제한
 * @return            요청 ID (>0), 실패 시 0 (errno 설정)
 *
 * 에러:
 *   ENOTSUP   - 지원하지 않는 소켓 타입 또는 thread-safe 아님
 *   EINVAL    - callback이 NULL이거나 parts가 NULL/part_count=0
 *              (ROUTER인데 routing_id가 NULL/size=0)
 *              (DEALER인데 routing_id가 NULL이 아니고 size>0)
 *   EAGAIN    - 리소스 부족
 *   EHOSTUNREACH - 연결된 피어 없음
 *   EINVAL    - timeout_ms가 유효하지 않음
 *
 * 참고:
 *   - 소켓 기본 타임아웃은 ZLINK_REQUEST_TIMEOUT 옵션으로 설정 (기본값 5000ms)
 *   - 반환된 request_id가 콜백에 전달되므로 이를 키로 컨텍스트 관리
 *   - parts는 연속된 zlink_msg_t 배열 (parts[0..part_count-1])
 *   - 성공 시 parts의 각 msg 소유권은 라이브러리로 이전됨
 *   - 실패 시 parts의 소유권은 호출자에게 남음
 */
ZLINK_EXPORT uint64_t zlink_request(
    void *socket,
    const zlink_routing_id_t *routing_id,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_request_cb_fn callback,
    int timeout_ms
);

/*
 * zlink_group_request - 그룹 단위 요청 전송 (routing_id 선택)
 *
 * @param socket      DEALER 또는 ROUTER 소켓
 * @param routing_id  대상의 routing_id (NULL이면 기본 동작)
 * @param group_id    그룹 ID (0은 기본 그룹)
 * @param parts       요청 메시지 배열
 * @param part_count  요청 프레임 개수 (>= 1)
 * @param callback    응답 수신 콜백
 * @param timeout_ms  타임아웃 (밀리초)
 *                   - ZLINK_REQUEST_TIMEOUT_DEFAULT: 소켓 기본값 사용
 *                   - -1: 무제한
 * @return            요청 ID (>0), 실패 시 0
 *
 * 에러:
 *   EINVAL    - callback이 NULL이거나 parts가 NULL/part_count=0
 *              (ROUTER인데 routing_id가 NULL/size=0)
 *              (DEALER인데 routing_id가 NULL이 아니고 size>0)
 *
 * 참고:
 *   - 동일 group_id는 inflight=1로 직렬 처리되어 순서 보장
 *   - parts는 연속된 zlink_msg_t 배열 (parts[0..part_count-1])
 *   - 성공 시 parts의 각 msg 소유권은 라이브러리로 이전됨
 *   - 실패 시 parts의 소유권은 호출자에게 남음
 */
ZLINK_EXPORT uint64_t zlink_group_request(
    void *socket,
    const zlink_routing_id_t *routing_id,
    uint64_t group_id,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_request_cb_fn callback,
    int timeout_ms
);

/* ============================================================
 * Request/Reply API - Server Functions
 * ============================================================ */

/*
 * zlink_on_request - 요청 핸들러 등록
 *
 * @param socket     ROUTER 또는 DEALER 소켓
 * @param handler    요청 수신 핸들러
 * @return           0: 성공, -1: 실패
 *
 * 에러:
 *   ENOTSUP   - 지원하지 않는 소켓 타입 또는 thread-safe 아님
 *   EINVAL    - handler가 NULL
 *
 * 참고:
 *   - 핸들러는 NULL 전달로 해제 가능
 *   - 핸들러가 등록되면 zlink_recv()로 메시지 수신 불가
 *   - 핸들러는 proxy worker thread(직렬 executor)에서 호출됨
 *   - 애플리케이션 컨텍스트는 전역 또는 소켓별로 관리
 */
ZLINK_EXPORT int zlink_on_request(
    void *socket,
    zlink_server_cb_fn handler
);

/*
 * zlink_reply - 요청에 대한 응답 전송
 *
 * @param socket       소켓 핸들
 * @param routing_id   응답 대상의 routing_id (zlink_routing_id_t)
 * @param request_id   원본 요청 ID (핸들러에서 받은 값)
 * @param parts        응답 메시지 배열 (소유권 이전됨)
 * @param part_count   응답 프레임 개수 (>= 1)
 * @return             0: 성공, -1: 실패
 *
 * 에러:
 *   EHOSTUNREACH - 해당 routing_id의 피어가 없음 (연결 끊김)
 *   EINVAL       - 잘못된 파라미터 또는 routing_id가 NULL/size=0
 *                  parts가 NULL/part_count=0
 *
 * 참고:
 *   - request_id는 클라이언트가 콜백 매칭에 사용
 *   - request_id가 0이면 상관관계 프레임 없이 전송
 *   - parts는 연속된 zlink_msg_t 배열 (parts[0..part_count-1])
 *   - 성공 시 parts의 각 msg 소유권은 라이브러리로 이전됨
 *   - 실패 시 parts의 소유권은 호출자에게 남음
 */
ZLINK_EXPORT int zlink_reply(
    void *socket,
    const zlink_routing_id_t *routing_id,
    uint64_t request_id,
    zlink_msg_t *parts,
    size_t part_count
);

/*
 * zlink_reply_simple - 단순화된 응답 전송 (request_id 자동 처리)
 *
 * 서버에서 on_request 핸들러 내에서 호출하는 경우,
 * 현재 처리 중인 요청에 대한 응답을 자동으로 전송
 *
 * @param socket  소켓 핸들
 * @param parts       응답 메시지 배열
 * @param part_count  응답 프레임 개수 (>= 1)
 * @return        0: 성공, -1: 실패
 *
 * 참고:
 *   - on_request 핸들러 밖에서 호출 시 EINVAL 에러
 *   - 성공 시 parts의 각 msg 소유권은 라이브러리로 이전됨
 *   - 실패 시 parts의 소유권은 호출자에게 남음
 */
ZLINK_EXPORT int zlink_reply_simple(
    void *socket,
    zlink_msg_t *parts,
    size_t part_count
);

/* ============================================================
 * Request/Reply API - Polling Functions (Language Bindings)
 * ============================================================ */

/*
 * Completion 구조체 - 완료된 요청 정보
 *
 * 참고:
 *   - 언어 바인딩에서 콜백 대신 폴링 방식으로 응답 수신 시 사용
 *   - Java Virtual Thread, C# Task, Kotlin Coroutine 등과 함께 사용 권장
 *   - completion.parts는 호출자가 zlink_msgv_close() 필요
 *   - completion.parts는 연속된 zlink_msg_t 배열
 */
typedef struct zlink_completion_t {
    uint64_t request_id;     /* 완료된 요청 ID */
    zlink_msg_t *parts;        /* 응답 메시지 배열 */
    size_t part_count;       /* 응답 프레임 개수 */
    int error;               /* 에러 코드 (0 = 성공) */
} zlink_completion_t;

/*
 * zlink_msgv_close - multipart 메시지 해제 헬퍼
 *
 * @param parts       메시지 배열
 * @param part_count  배열 길이
 *
 * 참고:
 *   - parts 배열과 내부 메시지를 모두 해제
 *   - on_request/콜백/recv에서 받은 배열에만 사용
 */
ZLINK_EXPORT void zlink_msgv_close(zlink_msg_t *parts, size_t part_count);

/*
 * zlink_request_send - 요청 전송 (콜백 없는 버전, routing_id 선택)
 *
 * @param socket      DEALER 또는 ROUTER 소켓
 * @param routing_id  대상의 routing_id (NULL이면 기본 동작)
 * @param parts       요청 메시지 배열 (소유권 이전됨)
 * @param part_count  요청 프레임 개수 (>= 1)
 * @return            요청 ID (>0), 실패 시 0
 *
 * 에러:
 *   ENOTSUP      - 지원하지 않는 소켓 타입 또는 thread-safe 아님
 *   EINVAL       - parts가 NULL/part_count=0
 *                  (ROUTER인데 routing_id가 NULL/size=0)
 *                  (DEALER인데 routing_id가 NULL이 아니고 size>0)
 *   EHOSTUNREACH - 연결된 피어 없음
 *
 * 참고:
 *   - zlink_request_recv()로 응답 수신
 *   - 언어 바인딩에서 coroutine/Task와 함께 사용 권장
 *   - 콜백 기반 zlink_request()와 혼용 가능
 *   - parts는 연속된 zlink_msg_t 배열 (parts[0..part_count-1])
 *   - 성공 시 parts의 각 msg 소유권은 라이브러리로 이전됨
 *   - 실패 시 parts의 소유권은 호출자에게 남음
 */
ZLINK_EXPORT uint64_t zlink_request_send(
    void *socket,
    const zlink_routing_id_t *routing_id,
    zlink_msg_t *parts,
    size_t part_count
);

/*
 * zlink_request_recv - 완료된 응답 수신
 *
 * @param socket      소켓 핸들
 * @param completion  완료 정보를 받을 구조체 (out)
 * @param timeout_ms  타임아웃 (-1: 무제한, 0: non-blocking)
 * @return            0: 성공, -1: 에러 또는 타임아웃
 *
 * 에러:
 *   EAGAIN    - timeout_ms=0이고 완료된 요청 없음
 *   ETIMEDOUT - 타임아웃 만료
 *
 * 참고:
 *   - completion.parts는 호출자가 zlink_msgv_close() 필요
 *   - 성공 시 completion.parts가 유효하며, 에러 시 NULL
 *   - 여러 요청이 완료되었으면 가장 먼저 완료된 것 반환
 *   - zlink_request_send()로 전송한 요청의 응답만 수신
 *   - 콜백 기반 zlink_request()의 응답은 콜백으로만 수신됨
 */
ZLINK_EXPORT int zlink_request_recv(
    void *socket,
    zlink_completion_t *completion,
    int timeout_ms
);

/* ============================================================
 * Request/Reply API - Request Management Functions
 * ============================================================ */

/*
 * zlink_pending_requests - 대기 중인 요청 수 조회
 *
 * @param socket  소켓 핸들
 * @return        대기 중인 요청 수, -1: 에러
 */
ZLINK_EXPORT int zlink_pending_requests(void *socket);

/*
 * zlink_cancel_all_requests - 모든 대기 요청 취소
 *
 * @param socket  소켓 핸들
 * @return        취소된 요청 수, -1: 에러
 *
 * 참고:
 *   - 모든 대기 중인 요청의 콜백이 error=ECANCELED로 호출됨
 */
ZLINK_EXPORT int zlink_cancel_all_requests(void *socket);
```

### 4.2.1 소유권 규칙 (C)

- **송신 계열**(`zlink_request`, `zlink_group_request`, `zlink_request_send`, `zlink_reply`):
  - 성공 시 **각 msg는 라이브러리가 소유/해제**한다.
  - `parts` 배열 메모리는 **호출자가 소유**한다 (스택/힙 모두 가능).
  - 실패 시 **호출자가 msg를 해제**해야 한다.
- **수신 계열**(`zlink_on_request` 핸들러, `zlink_request` 콜백, `zlink_request_recv`):
  - `parts` 배열은 **라이브러리에서 할당**되며 호출자가 소유한다.
  - 사용 후 반드시 `zlink_msgv_close(parts, part_count)`로 해제한다.

### 4.3 C API 에러 코드 요약

| 에러 코드 | 값 | 설명 |
|----------|-----|------|
| `ENOTSUP` | 95 | 지원하지 않는 소켓 타입 또는 thread-safe 아님 |
| `EINVAL` | 22 | 잘못된 파라미터 |
| `EAGAIN` | 11 | 리소스 부족 |
| `EHOSTUNREACH` | 113 | 대상 피어 없음 또는 연결 끊김 |
| `ETIMEDOUT` | 110 | 요청 타임아웃 |
| `ECANCELED` | 125 | 요청이 취소됨 |
| `ECONNRESET` | 104 | 연결이 리셋됨 |

### 4.4 C API 사용 흐름

```
┌─────────────────────────────────────────────────────────────┐
│                      Client Flow                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. zlink_socket_threadsafe(ctx, ZLINK_DEALER)                 │
│  2. zlink_connect(socket, "tcp://server:5555")               │
│  3. uint64_t req_id = zlink_request(socket, NULL, &msg, 1,   │
│                                 cb, ZLINK_REQUEST_TIMEOUT_DEFAULT)│
│       ↓                                                     │
│     [요청 전송됨, req_id로 추적 가능]                        │
│       ↓                                                     │
│  4. ... (비동기 처리, 이벤트 루프)                          │
│       ↓                                                     │
│  5. cb(req_id, reply_parts, reply_count, 0) 호출됨 ← 응답 수신│
│                                                             │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      Server Flow                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. zlink_socket_threadsafe(ctx, ZLINK_ROUTER)                 │
│  2. zlink_bind(socket, "tcp://*:5555")                       │
│  3. zlink_on_request(socket, handler)                        │
│       ↓                                                     │
│     [핸들러 등록됨]                                         │
│       ↓                                                     │
│  4. handler(parts, part_count, routing_id, req_id) 호출    │
│       │         └─ routing_id_t (zlink_routing_id_t)          │
│       ↓                                                     │
│  5. zlink_reply(socket, routing_id, req_id, &reply, 1)       │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 4.5 요청 ID 및 상관관계

```cpp
// 내부적으로 사용되는 요청 구조
struct pending_request_t {
    uint64_t request_id;           // 고유 요청 ID
    routing_id_t target;           // 요청 대상 routing_id (DEALER는 size=0)
    uint64_t group_id;             // 그룹 ID (0은 기본 그룹)
    msg_vec_t parts;               // 요청 페이로드 (1..N 프레임)
    std::function<void(msg_vec_t*, int)> callback;
    std::chrono::steady_clock::time_point deadline;  // 타임아웃 시점
};
```

**요청-응답 매칭:**
- 내부적으로 `request_id`를 메시지에 포함하여 전송
- 응답 수신 시 `request_id`로 대기 중인 콜백을 찾아 호출

---

## 5. 내부 구현

### 5.1 메시지 프레임 구조

**Request 메시지:**
```
[routing_id]     (ROUTER만, 자동 처리)
[request_id]     (8 bytes, 내부 생성)
[payload...]     (사용자 데이터, 1..N 프레임)
```

**Reply 메시지:**
```
[routing_id]     (ROUTER만, 자동 처리)
[request_id]     (8 bytes, 원본 요청과 동일)
[payload...]     (사용자 데이터, 1..N 프레임)
```

### 5.2 Request 처리 흐름

```cpp
void thread_safe_socket_t::request(msg_t&& payload,
                            std::function<void(msg_t&)> callback) {
    // 1. 고유 request_id 생성
    uint64_t req_id = generate_request_id();

    // 2. 요청 정보 저장
    pending_request_t req;
    req.request_id = req_id;
    req.target = {};     // DEALER는 비어 있음
    req.group_id = 0;    // 기본 그룹
    req.parts.emplace_back(std::move(payload)); // 단일 프레임을 내부 벡터로 래핑
    req.callback = [callback](msg_vec_t *reply_parts, int err) {
        if (err == 0 && reply_parts && !reply_parts->empty()) {
            callback((*reply_parts)[0]);
        }
    };
    req.deadline = calc_deadline();
    _pending_requests[req_id] = std::move(req);

    // 3. 프레임 구성: [request_id][payload...]
    msg_t id_frame;
    id_frame.init_size(8);
    memcpy(id_frame.data(), &req_id, 8);

    // 4. 송신 (DEALER는 LB가 자동 처리)
    xsend(&id_frame, ZLINK_SNDMORE);
    for (size_t i = 0; i < req.parts.size(); ++i) {
        int flags = (i + 1 < req.parts.size()) ? ZLINK_SNDMORE : 0;
        xsend(&req.parts[i], flags);
    }
}
```

### 5.3 Reply 처리 흐름

```cpp
// 서버 측: 요청 수신 시 호출되는 내부 핸들러
void thread_safe_socket_t::handle_incoming_request(pipe_t *pipe) {
    msg_t request_id_frame;
    msg_vec_t parts;
    routing_id_t routing_id = {};

    // ROUTER인 경우 routing_id 수신 (메시지 헤더에서 추출)
    if (options.type == ZLINK_ROUTER) {
        routing_id = pipe->get_routing_id();  // routing_id_t
    }

    xrecv(&request_id_frame, 0);

    // payload 수신 (multipart 지원)
    while (true) {
        msg_t part;
        xrecv(&part, 0);
        bool more = part.more();
        parts.emplace_back(std::move(part));
        if (!more)
            break;
    }

    uint64_t request_id = *reinterpret_cast<uint64_t*>(request_id_frame.data());

    // 사용자 핸들러 호출
    if (_request_handler) {
        _request_handler(parts, routing_id, request_id);
    }
}

// 서버 측: 응답 전송
void thread_safe_socket_t::reply(const routing_id_t &routing_id, uint64_t request_id, msg_t&& payload) {
    // routing_id로 파이프 조회 후 응답 전송
    // request_id는 원본 요청에서 보존하여 클라이언트가 매칭
    // payload는 단일 프레임
}

void thread_safe_socket_t::reply(const routing_id_t &routing_id,
                                 uint64_t request_id,
                                 msg_vec_t&& parts) {
    // multipart 응답 전송
}
```

### 5.4 타임아웃 처리

```cpp
void thread_safe_socket_t::check_timeouts() {
    auto now = std::chrono::steady_clock::now();

    for (auto it = _pending_requests.begin(); it != _pending_requests.end(); ) {
        if (it->second.deadline <= now) {
            // 타임아웃 콜백 호출
            it->second.callback(nullptr, ETIMEDOUT);
            it = _pending_requests.erase(it);
        } else {
            ++it;
        }
    }
}
```

### 5.5 Group Request 스케줄링

```cpp
// group_id별 요청 큐 (inflight=1)
struct group_queue_t {
    bool inflight = false;
    std::deque<uint64_t> pending;
};

std::unordered_map<uint64_t, group_queue_t> _group_queues;

void enqueue_group_request(uint64_t group_id, pending_request_t&& req) {
    auto &q = _group_queues[group_id];
    _pending_requests[req.request_id] = std::move(req);

    if (!q.inflight) {
        q.inflight = true;
        send_request(_pending_requests[req.request_id]);
    } else {
        q.pending.push_back(req.request_id);
    }
}

void on_group_response(uint64_t group_id, uint64_t request_id) {
    auto &q = _group_queues[group_id];
    q.inflight = false;

    if (!q.pending.empty()) {
        uint64_t next_id = q.pending.front();
        q.pending.pop_front();
        q.inflight = true;
        send_request(_pending_requests[next_id]);
    }
}
```

- `group_id=0`은 기본 그룹으로 처리 (기존 동작과 동일)
- 타임아웃 발생 시에도 `inflight`를 해제하여 다음 요청이 진행되도록 한다

---

## 6. 사용 예시

### 6.1 Server (ROUTER) - C API

```c
// 서버 설정
void *ctx = zlink_ctx_new();
void *server = zlink_socket_threadsafe(ctx, ZLINK_ROUTER);
const char *server_rid = "router-A";
zlink_setsockopt(server, ZLINK_ROUTING_ID, server_rid, strlen(server_rid));
zlink_bind(server, "tcp://*:5555");

// routing_id는 자동 생성됨 (5B [0x00][u32])
uint8_t rid_buf[255];
size_t rid_size = sizeof(rid_buf);
zlink_getsockopt(server, ZLINK_ROUTING_ID, rid_buf, &rid_size);

zlink_routing_id_t my_rid;
my_rid.size = (uint8_t)rid_size;
memcpy(my_rid.data, rid_buf, rid_size);

printf("Server routing_id(size=%zu) = ", rid_size);
for (size_t i = 0; i < rid_size; ++i)
    printf("%02x", rid_buf[i]);
printf("\n");

// 요청 핸들러 등록
zlink_on_request(server, [](zlink_msg_t *parts,
                          size_t part_count,
                          const zlink_routing_id_t *routing_id,
                          uint64_t request_id) {
    printf("Request from routing_id(size=%u) (req_id: %llu) = ",
           routing_id ? routing_id->size : 0, request_id);
    if (routing_id) {
        for (uint8_t i = 0; i < routing_id->size; ++i)
            printf("%02x", routing_id->data[i]);
    }
    printf("\n");
    if (part_count > 0) {
        printf("Payload[0]: %.*s\n", (int)zlink_msg_size(&parts[0]),
               (char*)zlink_msg_data(&parts[0]));
    }
    zlink_msgv_close(parts, part_count);

    // 응답 전송
    zlink_msg_t reply;
    zlink_msg_init_data(&reply, "World", 5, NULL, NULL);
    zlink_reply(server, routing_id, request_id, &reply, 1);
});
```

### 6.2 Client (DEALER) - Callback API

```c
// 클라이언트 설정
void *client = zlink_socket_threadsafe(ctx, ZLINK_DEALER);
const char *client_rid = "client-1";
zlink_setsockopt(client, ZLINK_ROUTING_ID, client_rid, strlen(client_rid));
zlink_connect(client, "tcp://localhost:5555");

// routing_id는 자동 생성됨 (5B [0x00][u32])
uint8_t rid_buf[255];
size_t rid_size = sizeof(rid_buf);
zlink_getsockopt(client, ZLINK_ROUTING_ID, rid_buf, &rid_size);

zlink_routing_id_t my_rid;
my_rid.size = (uint8_t)rid_size;
memcpy(my_rid.data, rid_buf, rid_size);

printf("Client routing_id(size=%zu) = ", rid_size);
for (size_t i = 0; i < rid_size; ++i)
    printf("%02x", rid_buf[i]);
printf("\n");

// 요청 전송 (로드 밸런싱)
zlink_msg_t request;
zlink_msg_init_data(&request, "Hello", 5, NULL, NULL);

// request_id가 콜백에 전달됨 - 이를 키로 컨텍스트 관리 가능
uint64_t req_id = zlink_request(client, NULL, &request, 1,
    [](uint64_t request_id, zlink_msg_t *reply_parts, size_t reply_count, int err) {
        if (err == 0) {
            if (reply_count > 0) {
                printf("Got reply for req %llu: %.*s\n",
                       request_id,
                       (int)zlink_msg_size(&reply_parts[0]),
                       (char*)zlink_msg_data(&reply_parts[0]));
            }
            zlink_msgv_close(reply_parts, reply_count);
        } else {
            printf("Request %llu failed: %d\n", request_id, err);
        }
    },
    ZLINK_REQUEST_TIMEOUT_DEFAULT);
printf("Sent request with ID: %llu\n", req_id);
```

### 6.3 Client (ROUTER) - Targeted Request

```c
// ROUTER 클라이언트 설정
void *client = zlink_socket_threadsafe(ctx, ZLINK_ROUTER);
zlink_connect(client, "tcp://server1:5555");

// 연결된 피어의 routing_id 조회
zlink_routing_id_t peer_rid;
if (zlink_socket_peer_routing_id(client, 0, &peer_rid) != 0) {
    /* handle error */
}

// 특정 서버에 요청 (routing_id로 지정)
zlink_msg_t request;
zlink_msg_init_data(&request, "Hello", 5, NULL, NULL);

zlink_request(client, &peer_rid, &request, 1,
    [](uint64_t request_id, zlink_msg_t *reply_parts, size_t reply_count, int err) {
        if (err == 0) {
            /* use reply_parts */
            zlink_msgv_close(reply_parts, reply_count);
        }
    },
    ZLINK_REQUEST_TIMEOUT_DEFAULT);
```

### 6.4 Client - Polling API (언어 바인딩용)

```c
// 콜백 없이 폴링 방식으로 응답 수신
zlink_msg_t request;
zlink_msg_init_data(&request, "Hello", 5, NULL, NULL);

// 요청 전송 (콜백 없음)
uint64_t req_id = zlink_request_send(client, NULL, &request, 1);

// 응답 대기 (blocking)
zlink_completion_t completion;
if (zlink_request_recv(client, &completion, -1) == 0) {
    if (completion.part_count > 0) {
        printf("Got reply for req %llu: %.*s\n",
               completion.request_id,
               (int)zlink_msg_size(&completion.parts[0]),
               (char*)zlink_msg_data(&completion.parts[0]));
    }
    zlink_msgv_close(completion.parts, completion.part_count);
}
```

### 6.5 C++ 사용 예시

```cpp
// 서버 (thread-safe)
zlink::thread_safe_socket<zlink::router_t> server(ctx);
server.bind("tcp://*:5555");

server.on_request([&server](zlink::msg_vec_t& parts,
                            const zlink::routing_id_t &src_id,
                            uint64_t req_id) {
    std::cout << "Request from routing_id(size=" << (int)src_id.size << ")" << std::endl;
    if (!parts.empty()) {
        std::cout << "Payload[0]: " << parts[0].to_string() << std::endl;
    }

    zlink::msg_t reply("World");
    server.reply(src_id, req_id, std::move(reply));
});

// 클라이언트 (thread-safe)
zlink::thread_safe_socket<zlink::dealer_t> client(ctx);
client.connect("tcp://localhost:5555");

client.request(zlink::msg_t("Hello"), [](zlink::msg_t& reply) {
    std::cout << "Got reply: " << reply.to_string() << std::endl;
});

// routing_id 조회
zlink::routing_id_t my_id = client.routing_id();  // 자동 생성된 ID
```

### 6.6 파이프라인 요청 (다중 동시 요청)

```cpp
// 여러 요청을 동시에 보내고 응답을 개별 처리
for (int i = 0; i < 100; ++i) {
    client.request(
        zlink::msg_t(fmt::format("Request {}", i)),
        [i](zlink::msg_t& reply) {
            std::cout << "Reply for request " << i << ": "
                      << reply.to_string() << std::endl;
        }
    );
}
// 응답은 도착 순서대로 콜백 호출 (순서 보장 없음)
```

### 6.7 그룹 요청 (순서 보장)

```cpp
// 동일 group_id 내 요청은 순서 보장 (inflight=1)
uint64_t group_id = 42;

client.group_request(
    group_id,
    zlink::msg_t("A"),
    [](zlink::msg_t* reply, int error) {
        /* handle */
    }
);

client.group_request(
    group_id,
    zlink::msg_t("B"),
    [](zlink::msg_t* reply, int error) {
        /* handle */
    }
);
```

### 6.8 타임아웃 처리

```cpp
client.request(
    zlink::msg_t("Hello"),
    [](zlink::msg_t* reply, int error) {
        if (error == 0) {
            std::cout << "Got reply: " << reply->to_string() << std::endl;
        } else if (error == ETIMEDOUT) {
            std::cout << "Request timed out" << std::endl;
        } else {
            std::cout << "Request failed: " << error << std::endl;
        }
    },
    std::chrono::seconds(5)  // 5초 타임아웃
);
```

### 6.9 Multipart 요청/응답 예시

```cpp
// multipart 요청
zlink::msg_vec_t parts;
parts.emplace_back("header");
parts.emplace_back("body");

client.request(std::move(parts),
    [](zlink::msg_vec_t& reply_parts) {
        if (!reply_parts.empty()) {
            std::cout << "Reply[0]: " << reply_parts[0].to_string() << std::endl;
        }
    }
);
```

### 6.10 언어 바인딩 패턴 (Polling API 활용)

언어 바인딩에서는 콜백 대신 Polling API를 사용하여 각 언어의 동시성 기능과 통합합니다.

**Java (Virtual Thread):**
```java
public final class ZlinkSocket {
    private final Pointer socket;
    private final ConcurrentHashMap<Long, CompletableFuture<ZlinkMessage>> pending =
        new ConcurrentHashMap<>();
    private final AtomicBoolean running = new AtomicBoolean(true);
    private final Thread recvThread;

    public ZlinkSocket(Pointer socket) {
        this.socket = socket;
        this.recvThread = Thread.ofVirtual().name("zlink-recv").start(this::recvLoop);
    }

    public CompletableFuture<ZlinkMessage> request(ZlinkMessage msg) {
        long requestId = zlink_request_send(socket, null, msg.toNative(), 1);
        if (requestId == 0)
            throw new ZlinkException(getLastError());

        CompletableFuture<ZlinkMessage> fut = new CompletableFuture<>();
        pending.put(requestId, fut);
        return fut;
    }

    private void recvLoop() {
        while (running.get()) {
            ZlinkCompletion completion = new ZlinkCompletion();
            if (zlink_request_recv(socket, completion, -1) != 0)
                continue;

            CompletableFuture<ZlinkMessage> fut = pending.remove(completion.requestId);
            if (fut == null)
                continue;

            if (completion.error != 0)
                fut.completeExceptionally(new ZlinkException(completion.error));
            else {
                ZlinkMessage reply = ZlinkMessage.fromParts(completion.parts, completion.partCount);
                zlink_msgv_close(completion.parts, completion.partCount);
                fut.complete(reply);
            }
        }
    }

    public void stop() {
        running.set(false);
    }
}

// 사용 예시
var reply = socket.request(new ZlinkMessage("Hello")).get();
```

**C# (async/await):**
```csharp
public sealed class ZlinkSocket {
    private readonly IntPtr _socket;
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<ZlinkMessage>> _pending =
        new ConcurrentDictionary<ulong, TaskCompletionSource<ZlinkMessage>>();
    private readonly CancellationTokenSource _cts = new CancellationTokenSource();
    private readonly Thread _recvThread;

    public ZlinkSocket(IntPtr socket) {
        _socket = socket;
        _recvThread = new Thread(RecvLoop) { IsBackground = true };
        _recvThread.Start();
    }

    public Task<ZlinkMessage> RequestAsync(ZlinkMessage msg) {
        ulong requestId = zlink_request_send(_socket, null, msg.ToNative(), 1);
        if (requestId == 0)
            throw new ZlinkException(Marshal.GetLastWin32Error());

        var tcs = new TaskCompletionSource<ZlinkMessage>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[requestId] = tcs;
        return tcs.Task;
    }

    private void RecvLoop() {
        while (!_cts.IsCancellationRequested) {
            var completion = new ZlinkCompletion();
            if (zlink_request_recv(_socket, ref completion, -1) != 0)
                continue;

            if (_pending.TryRemove(completion.RequestId, out var tcs)) {
                if (completion.Error != 0)
                    tcs.TrySetException(new ZlinkException(completion.Error));
                else {
                    var reply = ZlinkMessage.FromParts(completion.Parts, completion.PartCount);
                    zlink_msgv_close(completion.Parts, completion.PartCount);
                    tcs.TrySetResult(reply);
                }
            }
        }
    }

    public void Stop() => _cts.Cancel();
}

// 사용 예시
var reply = await socket.RequestAsync(new ZlinkMessage("Hello"));
```

**Kotlin (Coroutine):**
```kotlin
class ZlinkSocket(private val socket: Pointer) {
    private val pending = ConcurrentHashMap<Long, CompletableDeferred<ZlinkMessage>>()
    private val running = AtomicBoolean(true)
    private val recvThread = Thread {
        recvLoop()
    }.apply { isDaemon = true; start() }

    suspend fun request(msg: ZlinkMessage): ZlinkMessage {
        val requestId = zlink_request_send(socket, null, msg.toNative(), 1)
        if (requestId == 0L) {
            throw ZlinkException(getLastError())
        }

        val deferred = CompletableDeferred<ZlinkMessage>()
        pending[requestId] = deferred
        return deferred.await()
    }

    private fun recvLoop() {
        while (running.get()) {
            val completion = ZlinkCompletion()
            if (zlink_request_recv(socket, completion, -1) != 0) continue

            val deferred = pending.remove(completion.requestId) ?: continue
            if (completion.error != 0)
                deferred.completeExceptionally(ZlinkException(completion.error))
            else {
                val reply = ZlinkMessage.fromParts(completion.parts, completion.partCount)
                zlink_msgv_close(completion.parts, completion.partCount)
                deferred.complete(reply)
            }
        }
    }

    fun stop() {
        running.set(false)
    }
}

// 사용 예시
val reply = socket.request(ZlinkMessage("Hello"))
```

**장점:**
- Native 콜백 marshalling 오버헤드 없음
- 각 언어의 네이티브 동시성 기능 활용 (Virtual Thread, Task, Coroutine)
- GC 압박 최소화
- 동기식 코드처럼 읽히는 자연스러운 API

---

## 7. 구현 영향 범위

### 7.1 신규 파일

| 파일 | 내용 |
|------|------|
| `src/sockets/request_reply.hpp` | Request/Reply 로직 클래스 |
| `src/sockets/request_reply.cpp` | Request/Reply 구현 |

### 7.2 수정 파일

| 파일 | 변경 내용 |
|------|----------|
| `include/zlink.h` | multipart 지원 API 및 msgv 헬퍼 선언 |
| `src/api/zlink.cpp` | 새 API 구현 |
| `src/sockets/socket_base.hpp` | request/reply 메서드 추가, pending_requests 관리 |
| `src/sockets/socket_base.cpp` | request/reply 기본 구현 |
| `src/sockets/router.cpp` | ROUTER 특화 request/reply 처리 |
| `src/sockets/dealer.cpp` | DEALER 특화 request 처리 |

### 7.3 아키텍처 다이어그램

```
┌─────────────────────────────────────────────────────────────┐
│                       Public API Layer                       │
├─────────────────────────────────────────────────────────────┤
│  zlink.h                                                      │
│  ├─ zlink_request()           - 요청 (routing_id 선택)        │
│  ├─ zlink_group_request()     - 그룹 요청 (routing_id 선택)   │
│  ├─ zlink_request_send()      - 콜백 없는 요청 (routing_id 선택)│
│  ├─ zlink_request_recv()      - 완료 응답 수신                │
│  ├─ zlink_msgv_close()        - multipart 응답 해제           │
│  ├─ zlink_on_request()        - 요청 핸들러 등록              │
│  ├─ zlink_reply()             - 응답 전송                     │
│  └─ zlink_pending_requests()  - 대기 요청 수 조회             │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Request/Reply Layer                       │
├─────────────────────────────────────────────────────────────┤
│  request_reply_t (믹스인 클래스)                            │
│  ├─ pending_requests map<request_id, pending_request_t>    │
│  ├─ group_queues map<group_id, group_queue_t>              │
│  ├─ request_handler callback                               │
│  ├─ generate_request_id()                                  │
│  └─ check_timeouts()                                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                       Socket Layer                           │
├─────────────────────────────────────────────────────────────┤
│  router_t                         dealer_t                  │
│  ├─ request(target, parts, cb)    ├─ request(parts, cb)   │
│  ├─ reply(id, req_id, parts)      └─ (응답은 콜백으로 수신) │
│  └─ on_request(handler)                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 8. 검증 방법

**테스트 작성 규칙**
- 기능별로 `tests/<feature>/` 폴더를 만들고 그 아래에 `test_*.cpp`를 둔다.
  - 예: `tests/request-reply/test_request_reply.cpp`

### 8.1 단위 테스트

**테스트 파일:** `test_request_reply.cpp`

| 테스트 케이스 | 설명 |
|--------------|------|
| `test_dealer_router_basic` | DEALER→ROUTER 기본 요청/응답 |
| `test_router_router_targeted` | ROUTER→ROUTER 타겟 요청 |
| `test_multiple_dealers` | 여러 DEALER가 하나의 ROUTER에 요청 |
| `test_pipeline_requests` | 다중 동시 요청 처리 |
| `test_multipart_request_reply` | multipart 요청/응답 처리 |
| `test_request_timeout` | 타임아웃 동작 검증 |
| `test_server_handler` | 서버 핸들러 등록/호출 |

### 8.2 통합 테스트 시나리오

```
시나리오 1: 단순 요청/응답
1. ROUTER 서버 bind
2. DEALER 클라이언트 connect
3. 클라이언트 request() 호출
4. 서버 on_request 핸들러에서 reply()
5. 클라이언트 콜백에서 응답 수신 확인

시나리오 2: 타임아웃
1. ROUTER 서버 bind (응답하지 않음)
2. DEALER 클라이언트 connect
3. 클라이언트 request(1초 타임아웃) 호출
4. 1초 후 콜백에서 ETIMEDOUT 확인

시나리오 3: 파이프라인
1. 100개 요청을 연속으로 전송
2. 서버에서 역순으로 응답
3. 모든 콜백이 올바른 request_id와 매칭되는지 확인

시나리오 4: 그룹 순서 보장
1. 동일 group_id로 요청 3개 전송
2. 서버에서 역순으로 응답
3. 콜백이 요청 순서대로 호출되는지 확인

시나리오 5: multipart
1. 클라이언트가 2프레임 요청 전송
2. 서버가 2프레임 응답 전송
3. 콜백/recv에서 part_count 및 순서 확인
```

### 8.3 성능 벤치마크

| 측정 항목 | 설명 |
|----------|------|
| 요청/응답 레이턴시 | 단일 요청-응답 왕복 시간 |
| 처리량 | 초당 요청/응답 수 |
| 파이프라인 효율 | 동시 요청 시 처리량 향상 비율 |
| 메모리 사용량 | pending_requests 관리 오버헤드 |

### 8.4 에러 케이스 테스트

| 테스트 | 예상 동작 |
|--------|----------|
| PUB 소켓에서 request() 호출 | ENOTSUP 반환 |
| 연결 끊김 시 대기 요청 | 콜백에 ECONNRESET 전달 |
| 서버 없이 요청 | 타임아웃 또는 EHOSTUNREACH |

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 0.1 | 2025-01-25 | 초안 작성 |
| 0.2 | 2025-01-25 | C API 상세 명세 추가, Polling API 추가 |
| 0.3 | 2025-01-25 | 00번 스펙 반영: routing_id를 uint32_t로 변경 |
| 0.4 | 2026-01-26 | thread-safe 소켓 의존성 및 API 제약 명시 |
| 0.5 | 2026-01-26 | routing_id 변환 규칙/예시 정리, peer routing_id 조회 업데이트 |
| 0.6 | 2026-01-26 | routing_id_t 적용, group_request API 추가, timeout 인자 순서 정리 |
| 0.7 | 2026-01-26 | C API 단일 함수로 통합, timeout 파라미터 일원화 |
| 0.8 | 2026-01-26 | routing_id 변환 함수 제거, 문자열 routing_id 예시 추가 |
| 0.9 | 2026-01-26 | multipart 지원 추가, C API 시그니처 통합 |
| 1.0 | 2026-01-28 | 테스트를 기능별 폴더에 배치하는 규칙 추가 |
