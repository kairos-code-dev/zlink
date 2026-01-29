# Thread-safe 소켓 스펙 (Thread-safe Socket)

> **우선순위**: 2 (Core Feature)
> **상태**: Draft
> **버전**: 0.8

## 목차
1. [개요](#1-개요)
2. [현재 상태 분석](#2-현재-상태-분석)
3. [아키텍처](#3-아키텍처)
4. [제안 API](#4-제안-api)
5. [구현 방식](#5-구현-방식)
6. [사용 예시](#6-사용-예시)
7. [구현 영향 범위](#7-구현-영향-범위)
8. [검증 방법](#8-검증-방법)

---

## 1. 개요

### 1.1 배경

기존 libzlink의 소켓은 **스레드에 안전하지 않다**. 하나의 소켓을 여러 스레드에서 동시에 사용하면 정의되지 않은 동작(Undefined Behavior)이 발생한다. 사용자는 외부에서 뮤텍스 등으로 동기화를 처리해야 하며, 이는 복잡성과 잠재적 버그를 초래한다.

### 1.2 목표

**Proxy 패턴**으로 기존 소켓을 감싸고, **ASIO Strand**를 이용해 모든 접근을 직렬화함으로써 멀티스레드 환경에서도 안전하게 소켓을 공유할 수 있도록 한다.
또한 **별도의 소켓 타입을 추가하지 않는다**.
일반 애플리케이션이 직접 작업 큐/strand(직렬 executor)/락을 구성하지 않도록 **직렬화 책임을 라이브러리로 이동**한다.

### 1.3 핵심 장점

| 장점 | 설명 |
|------|------|
| **사용자 편의성** | 소켓 동기화 문제를 라이브러리 레벨에서 해결 |
| **Lock-free 효과** | Lock 대기 대신 작업 큐잉으로 효율적 자원 사용 |
| **일관성** | Request/Reply API와 결합하여 안전한 비동기 패턴 제공 |

---

## 2. 현재 상태 분석

### 2.1 현재 libzlink 스레드 모델

```
[Thread 1] ─────> zlink_send() ─────┐
                                  │  ❌ Race Condition!
[Thread 2] ─────> zlink_recv() ─────┘
                                  ↓
                          [Raw Socket]
```

**문제점:**
- 동일 소켓에 대한 동시 접근 시 데이터 손상
- 내부 상태(큐, 버퍼 등) 동시 수정으로 인한 충돌
- 사용자가 외부 Lock으로 보호해야 함

### 2.2 기존 해결 방법의 한계

| 방법 | 한계 |
|------|------|
| 외부 Mutex | 성능 저하, 데드락 위험, 코드 복잡성 |
| 소켓 복제 | 각 스레드별 소켓 생성 시 리소스 낭비 |
| inproc 프록시 | 추가 소켓/스레드 필요, 설정 복잡 |

---

## 3. 아키텍처

### 3.1 Proxy Wrapper 구조

사용자는 내부 Raw Socket에 직접 접근하지 않고, `thread_safe_socket_t` 래퍼 클래스를 통해 모든 명령을 수행한다.

```
[Multi-Threads]
       |
       |  send() / recv() / request() / reply()
       v
+-------------------------------------------------------+
|  thread_safe_socket_t (Frontend)                      |
|                                                       |
|        +-----------------------------------------+    |
|        | ctx 소유 io_context::strand(직렬 executor) |    |
|        +--------------------+--------------------+    |
|                             | (Serialized Dispatch)   |
|                             v                         |
|        +-----------------------------------------+    |
|        | Internal Raw Socket (Backend)           |    |
|        | - socket_base_t 상속                    |    |
|        | - Not Thread-safe                       |    |
|        +-----------------------------------------+    |
+-------------------------------------------------------+
```

### 3.2 ASIO Strand의 역할

| 역할 | 설명 |
|------|------|
| **직렬화** | 여러 스레드에서 들어오는 요청을 큐잉하여 순차 실행 |
| **안전성** | 내부 소켓 상태의 동시 변경 방지, Mutex 불필요 |
| **핸들러 바인딩** | Request/Reply 콜백도 **proxy의 strand(직렬 executor)**에서 실행되도록 설계 (03 스펙 연계) |

### 3.2.1 I/O Thread vs Proxy Worker Thread

- **I/O thread (libzlink 기존)**:
  - 네트워크 I/O, 폴링, reconnect, handshake 등 **전송 엔진** 담당
  - 애플리케이션 호출과 무관하게 내부에서 동작
- **proxy worker thread (본 스펙)**:
  - thread-safe 소켓의 **API 호출 직렬화** 담당
  - `zlink_send/recv` 같은 호출을 strand 큐에서 순차 실행
  - Request/Reply 콜백도 이 스레드에서 실행됨

> 두 스레드는 **역할과 생명주기가 다르다**.
> I/O thread는 기존 libzlink 전송 엔진을 담당하며,
> proxy worker thread는 thread-safe 소켓의 **API 직렬화 전용**이다.
> 따라서 proxy worker thread는 I/O thread를 대체하지 않는다.

### 3.3 동작 흐름

```
1. Thread A: proxy->send(msg1)
   → strand(직렬 executor).post(send_task_1)

2. Thread B: proxy->send(msg2)  (동시 호출)
   → strand(직렬 executor).post(send_task_2)

3. Strand 내부 큐:
   [send_task_1] → [send_task_2]

4. ctx io_context 워커가 순차적으로 실행:
   - raw_socket->xsend(msg1)
   - raw_socket->xsend(msg2)
```

### 3.4 모니터링 연동

- **Proxy 소켓을 대상으로 모니터링해도 무방하다.**
  - 이벤트는 내부 Raw Socket 상태를 반영한다.
  - `CONNECTION_READY` 기준은 핸드셰이크 완료 시점으로 동일하다.
- Thread-safe 소켓이라면 `zlink_socket_monitor*` 호출도 strand(직렬 executor)를 통해 **직렬화**된다.
- Thread-safe가 아닌 소켓은 **소켓 소유 스레드에서만** 모니터 설정/해제를 수행한다.
- 모니터 소켓은 별도 소켓이므로 기본적으로 **thread-safe하지 않다**.

### 3.5 실행 모델 (ctx 소유 io_context)

- ctx 생성 시 **io_context + 전용 워커 스레드**를 함께 생성한다.
- thread-safe 소켓은 **ctx의 io_context를 공유**하고, 소켓별로 **전용 strand(직렬 executor)**를 사용한다.
- ctx 종료 시 워커 스레드를 정리(join)한다.

### 3.6 바인딩 주의사항 (기존 libzlink 특성과 동일)

- thread-safe 소켓 사용 시 **백그라운드 워커 스레드가 생성**된다.
  - 임베디드 환경에서는 사용 여부를 명확히 안내.
- **일반 send/recv에 대한 비동기 C API는 제공하지 않는다.**
  - 필요 시 `ZLINK_DONTWAIT` + `zlink_poll` 또는 바인딩 레이어에서 비동기 래핑을 제공한다.
  - 단, **Request/Reply API는 03번 문서의 콜백/폴링 인터페이스를 사용**한다.

---

## 4. 제안 API

### 4.1 Thread-safe 소켓 생성 (네이밍 통일)

```c
// 표준 생성 함수 (thread-safe 소켓은 _threadsafe 접미사로 통일)
void *socket = zlink_socket_threadsafe(ctx, ZLINK_DEALER);
```

- `zlink_socket_threadsafe()`를 **단일 표준**으로 사용한다.
- **별도의 소켓 타입 플래그는 제공하지 않는다.**
- 동일 소켓에 대해 **raw 소켓과 thread-safe 소켓을 동시에 사용하지 않는다** (택1).

### 4.2 동기 API (기존 API 호환)

```c
// 기존 API와 동일하게 사용 가능
int zlink_send(void *socket, const void *buf, size_t len, int flags);
int zlink_recv(void *socket, void *buf, size_t len, int flags);
int zlink_msg_send(zlink_msg_t *msg, void *socket, int flags);
int zlink_msg_recv(zlink_msg_t *msg, void *socket, int flags);
```

**내부 동작:**
- 호출 스레드에서 strand(직렬 executor)에 작업 post
- 완료까지 블로킹 대기 (condition_variable 사용)
- strand(직렬 executor) 실행 중(워커 스레드) 호출이면 **inline dispatch**로 즉시 실행
- **일반 send/recv에 대한 추가 비동기 C API는 제공하지 않는다.**

### 4.3 모니터링 연계 API (참고)

```c
/* thread-safe 소켓에서도 사용 가능 */
void *zlink_socket_monitor_open(void *socket, int events);

/* 모니터 소켓은 일반 소켓으로 수신 */
```

### 4.4 C API 상세 명세 (zlink.h 추가 내용)

```c
/* ============================================================
 * Thread-safe Socket - Functions
 * ============================================================ */

/*
 * zlink_socket_threadsafe - Thread-safe 소켓 생성
 *
 * @param ctx   유효한 context
 * @param type  소켓 타입 (ZLINK_DEALER, ZLINK_ROUTER, ZLINK_PUB, ZLINK_SUB, etc.)
 * @return      성공 시 소켓 핸들, 실패 시 NULL (errno 설정)
 *
 * 에러:
 *   EINVAL  - 잘못된 context 또는 소켓 타입
 *   EMFILE  - 최대 소켓 수 초과
 *   ETERM   - context가 종료됨
 *
 * 참고:
 *   - 기존 소켓을 감싼 proxy 객체를 반환한다
 */
ZLINK_EXPORT void *zlink_socket_threadsafe(void *ctx, int type);

/*
 * zlink_is_threadsafe - 소켓이 thread-safe인지 확인
 *
 * @param socket  소켓 핸들
 * @return        1: thread-safe, 0: 일반 소켓, -1: 에러
 */
ZLINK_EXPORT int zlink_is_threadsafe(void *socket);
```

---

## 5. 구현 방식

### 5.1 핵심 클래스 구조

```cpp
namespace zlink {

class thread_safe_socket_t {
public:
    thread_safe_socket_t(ctx_t *ctx, int type);
    ~thread_safe_socket_t();

    // 동기 API (블로킹)
    int send(msg_t *msg, int flags);
    int recv(msg_t *msg, int flags);

private:
    io_context::strand _strand; // ctx 소유 io_context 기반 (직렬 executor)
    std::unique_ptr<socket_base_t> _socket;

    // 동기 API를 위한 동기화
    std::mutex _sync_mutex;
    std::condition_variable _sync_cv;
};

} // namespace zlink
```

### 5.2 동기 send 구현 (Completion 대기)

```cpp
int thread_safe_socket_t::send(msg_t *msg, int flags) {
    if (_strand.running_in_this_thread()) {
        return _socket->xsend(msg, flags); // 재진입 시 inline 실행
    }

    int result = 0;
    bool done = false;

    // strand(직렬 executor)에 작업 post
    boost::asio::post(_strand, [this, msg, flags, &result, &done]() {
        result = _socket->xsend(msg, flags);

        std::lock_guard<std::mutex> lock(_sync_mutex);
        done = true;
        _sync_cv.notify_one();
    });

    // 완료 대기
    std::unique_lock<std::mutex> lock(_sync_mutex);
    _sync_cv.wait(lock, [&done]() { return done; });

    return result;
}
```

**동기 대기의 의미:**
- 여기서 “동기”는 **strand 작업 완료까지 호출 스레드가 대기**한다는 뜻이다.
- 실제 I/O의 blocking/non-blocking 여부는 `flags` (`ZLINK_DONTWAIT`)에 의해 결정된다.

### 5.3 재진입(inline dispatch)과 데드락 방지

Request/Reply 콜백은 proxy worker thread의 strand에서 실행된다.
이 콜백이 다시 `send/recv`를 호출하면 **재진입**이 발생한다.
이때 inline dispatch가 없으면 다음과 같이 데드락이 가능하다.

```cpp
int thread_safe_socket_t::send(msg_t *msg, int flags) {
    int result = 0;
    bool done = false;

    boost::asio::post(_strand, [this, msg, flags, &result, &done]() {
        result = _socket->xsend(msg, flags);
        std::lock_guard<std::mutex> lock(_sync_mutex);
        done = true;
        _sync_cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(_sync_mutex);
    _sync_cv.wait(lock, [&done]() { return done; });
    return result;
}

// 콜백이 strand에서 실행 중일 때
void on_request(/* ... */) {
    socket->send(&msg, 0); // post + wait
    // 현재 핸들러가 끝나야 post된 작업이 실행됨
    // => 자기 자신이 큐를 막고 대기 -> 데드락
}
```

따라서 `_strand.running_in_this_thread()`인 경우에는 **즉시 실행**하여
자기 자신이 큐를 막고 기다리는 상황을 피한다.

---

## 6. 사용 예시

### 6.1 기본 사용 (동기)

```c
// Thread-safe 소켓 생성
void *ctx = zlink_ctx_new();
void *socket = zlink_socket_threadsafe(ctx, ZLINK_DEALER);
zlink_connect(socket, "tcp://localhost:5555");

// 여러 스레드에서 안전하게 사용
std::thread t1([socket]() {
    zlink_send(socket, "Hello from T1", 13, 0);
});

std::thread t2([socket]() {
    zlink_send(socket, "Hello from T2", 13, 0);
});

t1.join();
t2.join();
```

### 6.2 멀티스레드 워커 풀

```cpp
// 워커 풀에서 공유 소켓 사용
zlink::thread_safe_socket<zlink::dealer_t> shared_socket(ctx);
shared_socket.connect("tcp://localhost:5555");

std::vector<std::thread> workers;
for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&shared_socket, i]() {
        for (int j = 0; j < 100; ++j) {
            zlink::msg_t request;
            request.init_data(fmt::format("Worker {} Request {}", i, j));
            shared_socket.send(request, 0);

            zlink::msg_t reply;
            shared_socket.recv(reply, 0);
        }
    });
}

for (auto& w : workers) w.join();
```

---

## 7. 구현 영향 범위

### 7.1 신규 파일

| 파일 | 내용 |
|------|------|
| `include/zlink_threadsafe.h` | Thread-safe API 선언 |
| `src/sockets/thread_safe_socket.hpp` | thread_safe_socket_t 클래스 선언 |
| `src/sockets/thread_safe_socket.cpp` | thread_safe_socket_t 구현 |

### 7.2 수정 파일

| 파일 | 변경 내용 |
|------|----------|
| `include/zlink.h` | thread-safe API 선언 (`zlink_socket_threadsafe`, `zlink_is_threadsafe`) |
| `src/api/zlink.cpp` | Thread-safe 소켓 생성/API 라우팅 |
| `src/ctx.cpp` | Thread-safe 소켓 관리 |

### 7.3 아키텍처 다이어그램

```
┌─────────────────────────────────────────────────────────────┐
│                       Public API Layer                       │
├─────────────────────────────────────────────────────────────┤
│  zlink.h / zlink_threadsafe.h                                   │
│  ├─ zlink_socket_threadsafe(...)                             │
│  └─ 기존 zlink_send/recv/zlink_msg_send/recv                   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Thread-safe Socket Layer                   │
├─────────────────────────────────────────────────────────────┤
│  thread_safe_socket_t                                       │
│  ├─ ctx io_context + worker (실행)                         │
│  ├─ io_context::strand (직렬 executor)                     │
│  ├─ 동기 API 재사용                                         │
│  └─ 내부 socket_base_t 래핑                                 │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Raw Socket Layer                          │
├─────────────────────────────────────────────────────────────┤
│  socket_base_t (기존)                                       │
│  ├─ dealer_t, router_t, pub_t, sub_t, ...                  │
│  └─ Not thread-safe (단일 스레드 접근만 허용)               │
└─────────────────────────────────────────────────────────────┘
```

---

## 8. 검증 방법

**테스트 작성 규칙**
- 기능별로 `tests/<feature>/` 폴더를 만들고 그 아래에 `test_*.cpp`를 둔다.
  - 예: `tests/threadsafe/test_thread_safe_socket.cpp`

### 8.1 단위 테스트

**테스트 파일:** `test_thread_safe_socket.cpp`

| 테스트 케이스 | 설명 |
|--------------|------|
| `test_concurrent_send` | 여러 스레드에서 동시 send 시 데이터 무결성 |
| `test_concurrent_recv` | 여러 스레드에서 동시 recv 시 데이터 무결성 |
| `test_mixed_operations` | send/recv 혼합 동시 호출 |
| `test_reentrant_sync_call` | strand(직렬 executor) 내부 호출 시 데드락 없음 |
| `test_high_contention` | 높은 경합 상황에서 정확성 |
| `test_is_threadsafe_flag` | zlink_is_threadsafe() 결과 확인 |

### 8.2 스트레스 테스트

```cpp
// 10개 스레드, 각 10,000개 메시지
const int NUM_THREADS = 10;
const int MSGS_PER_THREAD = 10000;

std::atomic<int> total_sent{0};
std::atomic<int> total_recv{0};

// ... 스트레스 테스트 코드 ...

ASSERT_EQ(total_sent.load(), NUM_THREADS * MSGS_PER_THREAD);
ASSERT_EQ(total_recv.load(), NUM_THREADS * MSGS_PER_THREAD);
```

### 8.3 성능 벤치마크

| 측정 항목 | 설명 |
|----------|------|
| 단일 스레드 오버헤드 | Thread-safe vs Raw 소켓 성능 비교 |
| 멀티스레드 스케일링 | 스레드 수 증가에 따른 처리량 |
| 레이턴시 분포 | P50, P99, P99.9 레이턴시 측정 |

### 8.4 데이터 레이스 검증

```bash
# ThreadSanitizer로 빌드
cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=thread"
cmake --build build

# 테스트 실행
./build/tests/test_thread_safe_socket
```

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 0.1 | 2025-01-25 | 초안 작성 |
| 0.2 | 2026-01-26 | proxy 방식 명시, 생성/네이밍 통일, 모니터링 정책 정리 |
| 0.3 | 2026-01-26 | 비동기 recv 콜백 분리, msg/buffer API 명세 정리 |
| 0.4 | 2026-01-26 | ctx io_context 모델 확정, 재진입 규칙 추가 |
| 0.5 | 2026-01-26 | C API 비동기 콜백 제거, 기존 동기 API 재사용으로 정리 |
| 0.6 | 2026-01-26 | 재진입 데드락 설명 보강, 동기 대기 의미 명시 |
| 0.7 | 2026-01-28 | Request/Reply 콜백 예외 명시 + is_threadsafe 테스트 추가 |
| 0.8 | 2026-01-28 | 테스트를 기능별 폴더에 배치하는 규칙 추가 |
