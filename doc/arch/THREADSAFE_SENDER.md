# Thread-Safe Sender (Core API Draft)

## 목적
다중 스레드 환경에서 `send`만 안전하게 사용할 수 있도록, 기존 소켓의
연결/세션/라우팅을 그대로 유지한 채 **send 전용 핸들**을 제공한다.

- 원 소켓은 단일 스레드에서 `recv` 및 이벤트 처리를 수행
- 다른 스레드는 send 전용 핸들을 통해 **큐에 enqueue**
- 실제 전송은 **원 소켓 스레드**가 수행

## 핵심 원칙
- **연결/라우팅/세션은 원 소켓과 1:1로 공유**된다.
- sender 핸들은 **send 전용**이며, `recv`/`bind`/`connect`/`setsockopt`를 지원하지 않는다.
- thread-safe 소켓을 재도입하지 않으며, 오해를 피하기 위해 `threadsafe_sender`로 명명한다.

---

## API 제안 (C API 기준)

### 핸들 생성/파괴
```c
// send 전용 핸들 생성
// socket_ : 기존 소켓 (원 소켓)
// opts_   : 큐 옵션 (NULL 가능)
// returns : 핸들 또는 NULL
ZLINK_API void *zlink_threadsafe_sender_create (void *socket_, const zlink_threadsafe_sender_opts_t *opts_);

// 핸들 해제 (큐 drain/flush 정책은 opts에 따름)
ZLINK_API int zlink_threadsafe_sender_close (void *sender_);
```

### send 전용 호출
```c
// sender를 통한 전송 (thread-safe)
ZLINK_API int zlink_threadsafe_sender_send (void *sender_, const void *buf_, size_t len_, int flags_);

// msg 전송 (zero-copy msg 지원 여부는 옵션으로 구분)
ZLINK_API int zlink_threadsafe_sender_msg_send (void *sender_, zlink_msg_t *msg_, int flags_);
```

---

## 옵션 구조체 (초안)
```c
typedef enum zlink_threadsafe_sender_policy_t {
    ZLINK_THREADSAFE_SENDER_DROP = 0,   // 큐가 가득 차면 즉시 실패
    ZLINK_THREADSAFE_SENDER_BLOCK = 1,  // 큐 공간이 생길 때까지 블록
    ZLINK_THREADSAFE_SENDER_TIMEOUT = 2 // 지정 시간 대기 후 실패
} zlink_threadsafe_sender_policy_t;

typedef struct zlink_threadsafe_sender_opts_t {
    size_t capacity;          // 큐 길이 (메시지 개수 기준)
    zlink_threadsafe_sender_policy_t policy;
    int timeout_ms;           // ZLINK_THREADSAFE_SENDER_TIMEOUT일 때 사용
    int flush_on_close;       // close 시 drain 여부 (0/1)
    int close_timeout_ms;     // flush 시 최대 대기 시간
    int allow_msg;            // zlink_msg_t 전송 허용 여부
} zlink_threadsafe_sender_opts_t;
```

기본값 제안:
- `capacity = 65536`
- `policy = ZLINK_SENDQ_DROP`
- `timeout_ms = 0`
- `flush_on_close = 0`
- `close_timeout_ms = 0`
- `allow_msg = 0`

---

## 동작 정의

### 1) 큐의 소유권
- 큐는 **원 소켓에 1:1**로 연결된다.
- 동일 소켓에서 여러 sender 생성 시:
  - 허용 여부를 명확히 정의해야 함 (권장: 1개만 허용)

### 2) send 동작
- `zlink_threadsafe_sender_send()`는 내부 큐에 메시지를 enqueue한다.
- 실제 전송은 원 소켓 스레드의 **in_event/process_commands** 루프에서 drain.
- enqueue 순서는 **FIFO**가 보장된다 (동일 sendq 기준).

### 3) backpressure
- `DROP`: 큐가 가득 차면 `EAGAIN` 반환
- `BLOCK`: 큐 공간이 생길 때까지 대기
- `TIMEOUT`: 지정 시간 대기 후 `EAGAIN`

### 4) close 정책
- `flush_on_close=1`이면 남은 큐를 drain하려 시도
- `close_timeout_ms` 초과 시 `ETIMEDOUT` 반환

---

## 제약/주의
- **recv는 반드시 원 소켓 단일 스레드에서만 수행**
- sender는 `bind/connect/setsockopt` 불가
- ROUTER/STREAM 등 라우팅이 중요한 소켓은 **routing_id가 원 소켓 기준**
- sender는 **thread-safe send 편의 기능**이지, thread-safe socket이 아님
- 원 소켓이 `close`/`term`되면 sender는 `ETERM`을 반환
- `zlink_threadsafe_sender_msg_send()`의 소유권:
  - 성공 시 메시지 소유권이 sendq로 이동
  - 실패 시 호출자가 msg를 정리해야 함

---

## 구현 메모 (코어 관점)
- 내부 MPSC 큐 (lock-free 또는 mutex 기반)
- 원 소켓의 event loop에서 큐 drain
- `sender` 생성 시 원 소켓에 reference 추가 (lifecycle 관리)
- `close` 시 안전한 drain/cleanup

---

## 문서화 가이드 (요약)
- "thread-safe socket" 대신 "threadsafe sender" 명칭 사용
- 연결/라우팅 공유는 원 소켓에만 존재함을 강조
- 사용 예제는 **send-only, recv 단일 스레드** 패턴으로 제공

---

## 테스트 시나리오 (초안)

### 1) 기본 동작 (단일 sender)
- 원 소켓 1개 + sender 1개 생성
- 별도 스레드에서 sender로 1,000회 전송
- 원 소켓에서 모든 메시지 수신 확인

### 2) 다중 sender
- 동일 소켓에 sender 2개 생성 허용 시
- 서로 다른 스레드에서 동시에 send
- 수신 측에서 메시지 개수/순서 검증 (FIFO는 sender 단위 보장)

### 3) backpressure 정책
- DROP/BLOCK/TIMEOUT 각각에 대해
- 큐 용량을 작은 값으로 설정 후 overflow 유발
- 반환 코드(EAGAIN/ETIMEDOUT) 검증

### 4) close/flush 정책
- flush_on_close=0: 남은 큐 폐기 확인
- flush_on_close=1 + close_timeout_ms 설정:
  - 정상 drain 완료 시 성공
  - timeout 시 ETIMEDOUT 반환

### 5) 원 소켓 종료
- sender 사용 중 원 소켓 close/ctx term
- sender send 호출 시 ETERM 반환 확인

### 6) 소켓 타입별 검증
- PUB/SUB, DEALER/ROUTER, STREAM 각각에서
- send 전용 동작이 정상인지 확인

### 7) 성능/지연 영향
- sender on/off 비교
- send 경로 성능 저하 여부 측정
