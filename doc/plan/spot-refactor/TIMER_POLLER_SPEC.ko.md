# Timer Poller Spec

이 문서는 공통 timer 와 poller 를 함께 쓰는 기준을 정리한다.

## 목적

소켓 이벤트와 timer 만료를 같은 스레드에서 함께 처리할 수 있어야 한다.
기본 사용 방식은 `recv` 와 `poller` 이고, callback 은 선택적으로만 쓴다.

이 규칙이 필요한 이유는 다음과 같다.

- 한 스레드에서 메시지와 시간을 함께 다루는 event loop 구성이 쉬워진다.
- 기존 수신 정책과 같은 방식으로 timer 도 이해할 수 있다.
- callback 과 pull 모델을 섞었을 때 생기는 혼란을 줄일 수 있다.

## 기준 API

새 기준 공개 표면은 다음과 같다.

```c
typedef void (*zlink_timer_handler_fn) (
    void *timer,
    uint64_t fire_count,
    void *userdata);

void *zlink_timer_new(void);
void *zlink_spot_timer_new(void *spot);
int zlink_timer_destroy(void **timer_p);
int zlink_timer_start(void *timer,
                      uint64_t interval_ns,
                      uint64_t repeat_count);
int zlink_timer_stop(void *timer);
int zlink_timer_recv(void *timer,
                     uint64_t *fire_count_out,
                     int flags);
int zlink_timer_handler(void *timer,
                        zlink_timer_handler_fn handler,
                        void *userdata);

int zlink_poller_add_timer(void *poller,
                           void *timer,
                           void *user_data);
int zlink_poller_remove_timer(void *poller,
                              void *timer);
```

## 기존 API 처리

기존 `zlink_timers_*()` 계열은 유지하지 않는다.

- `zlink_timers_new()`
- `zlink_timers_destroy()`
- `zlink_timers_add()`
- `zlink_timers_cancel()`
- `zlink_timers_set_interval()`
- `zlink_timers_reset()`
- `zlink_timers_timeout()`
- `zlink_timers_execute()`

이번 전환에서는 호환 경로를 두지 않는다.
bindings, 샘플, 테스트도 모두 새 기준으로 옮긴다.

## 기본 수신 모델

timer 의 기본 수신 모델은 `recv` 또는 `poller` 이다.

- `zlink_timer_recv()` 로 직접 fire event 를 읽을 수 있다.
- `zlink_poller_add_timer()` 로 poller 에 등록할 수 있다.
- `poller` 에서 timer readable 이벤트를 받은 뒤 `zlink_timer_recv()` 로 소비한다.

즉 timer 도 소켓과 같은 식으로 “읽을 것이 생겼다”는 신호와
실제 소비를 분리한다.

## callback 모델

callback 은 선택적 수신 모델이다.

- `zlink_timer_handler()` 를 등록하면 callback 전용 모드로 전환된다.
- callback 전용 모드에서는 `zlink_timer_recv()` 와 `zlink_poller_add_timer()`
  를 함께 쓰지 않는다.
- `recv` 또는 `poller` 모델을 쓰는 중 `zlink_timer_handler()` 를 등록하면 안 된다.

이 규칙은 기존 수신 정책과 맞춘 것이다.
기본은 pull 모델이고, callback 은 명시적으로 선택하는 방식이다.

## 충돌 규칙

수신 모델 충돌은 모두 `EBUSY` 로 처리한다.

- callback 등록 상태에서 `zlink_timer_recv()` 호출: `EBUSY`
- callback 등록 상태에서 `zlink_poller_add_timer()`: `EBUSY`
- `poller` 에 등록된 timer 에 callback 등록: `EBUSY`
- `recv` 사용 중 callback 등록: `EBUSY`

하나의 timer 는 한 시점에 하나의 수신 모델만 활성화될 수 있다.

## repeat 규칙

`repeat_count` 는 다음처럼 해석한다.

- `0`: 무한 반복
- `1`: 한 번만 실행
- `1` 보다 큰 값: 지정한 횟수만큼 반복

`interval_ns` 는 항상 0보다 커야 한다.

## spot timer

`zlink_spot_timer_new(void *spot)` 는 `Spot` 에 귀속되는 timer 를 만든다.

이 timer 는 일반 timer 와 공개 함수는 같지만, 내부 backend 는 다르다.

- 일반 timer:
  timer 하나가 thread 하나를 쓰는 단순 backend 를 사용한다.
- spot timer:
  `Spot` 수가 많고 timer 수도 많아질 수 있으므로 shared scheduler 를 사용한다.

즉 `spot timer` 는 timer 마다 thread 를 만들지 않는다.
하나의 scheduler 가 여러 timer 의 만료 시점을 함께 관리한다.

### spot timer 내부 모델

spot timer 는 다음 구조를 따른다.

1. `zlink_spot_timer_new()` 로 `Spot` 귀속 timer 생성
2. `zlink_timer_start()` 호출 시 shared scheduler 에 등록
3. deadline 이 되면 해당 timer 의 fire event queue 에 `fire_count` 적립
4. `Spot` 에는 `TIMER_READABLE` dispatch event 를 올림
5. 사용자는 `zlink_timer_recv()` 또는 callback 으로 소비

즉 scheduler 는 만료 시점만 관리하고,
실제 소비는 timer 별 queue 를 통해 이뤄진다.

## spot dispatch event 와의 관계

spot timer 는 `Spot` dispatch event 와 연결된다.

필수 event:

- `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE`
- `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE`
- `ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE`

`TIMER_READABLE` 은 “읽을 timer fire event 가 생겼다”는 뜻이다.
실제 fire count 는 `zlink_timer_recv()` 로 읽는다.

## poller event 구조

poller 는 timer source 도 구분해야 한다.

```c
typedef enum zlink_poller_source_kind_t
{
    ZLINK_POLLER_SOURCE_SOCKET = 1,
    ZLINK_POLLER_SOURCE_FD = 2,
    ZLINK_POLLER_SOURCE_TIMER = 3
} zlink_poller_source_kind_t;

typedef struct zlink_poller_event_t
{
    zlink_poller_source_kind_t source_kind;
    void *socket;
    zlink_fd_t fd;
    void *timer;
    void *user_data;
    short events;
} zlink_poller_event_t;
```

timer source 일 때는:

- `source_kind = ZLINK_POLLER_SOURCE_TIMER`
- `timer = 등록된 timer handle`
- `events = ZLINK_POLLIN`

## 수명 규칙

timer 를 destroy 하기 전에 poller 에서 먼저 제거해야 한다.

- poller 등록 중 `zlink_timer_destroy()`: `EBUSY`
- 제거 후 destroy 가능

이번 전환에서는 destroy 시 자동 detach 같은 호환 동작을 두지 않는다.

## 구현 기준

### 일반 timer

- low-count 일반 용도로 시작하더라도 구현 backend 는 global shared scheduler 로 통합한다
- timer 수가 늘어도 thread 수가 같이 늘지 않아야 한다
- request timeout 과 poller 연동이 같은 scheduler 계층 위에서 동작할 수 있어야 한다

### spot timer

- 수천에서 수만 개 timer 까지 고려
- `SpotNode` 단위 shared scheduler 사용
- scheduler 는 deadline 기준 자료구조를 사용한다
  예: min-heap, ordered set, timer wheel
- scheduler thread 수는 매우 적어야 한다

### request timeout 과의 통합

request-reply timeout 도 별도 thread 를 요청마다 띄우지 않는다.
일반 timer 와 spot timer 가 쓰는 shared scheduler 계층에 등록해서 처리한다.

이 규칙이 필요한 이유는 다음과 같다.

- 요청 수가 많아질 때 timeout thread 수가 같이 늘면 성능이 급격히 나빠진다
- timer 와 request timeout 이 같은 deadline 관리 계층을 공유하면 구현이 단순해진다
- poller, recv, callback 정책과 timeout 정책을 같은 기반에서 설명할 수 있다

## 회귀 테스트 기준

필수 회귀 테스트:

- one-shot timer 를 `zlink_timer_recv()` 로 읽을 수 있어야 한다
- 반복 timer 에서 `fire_count` 가 순서대로 증가해야 한다
- `zlink_poller_add_timer()` 후 `poller wait` 에서 timer readable 을 받을 수 있어야 한다
- poller wait 뒤 `zlink_timer_recv()` 로 fire event 를 소비할 수 있어야 한다
- callback 등록 상태에서 `recv` 와 `poller add` 는 `EBUSY`
- poller 등록 상태에서 callback 등록은 `EBUSY`
- poller 등록 상태에서 destroy 는 `EBUSY`
- `zlink_spot_timer_new()` 로 만든 timer 는 `TIMER_READABLE` 을 `Spot` dispatch event 로 올려야 한다
- 일반 timer 는 global shared scheduler 경로에서 동작해야 한다
- `spot timer` 는 `SpotNode` 단위 shared scheduler 경로에서 동작해야 한다
- request timeout 도 shared scheduler 경로를 사용해야 한다

## 결론

timer 는 이제 단순 callback helper 가 아니다.
소켓과 함께 같은 loop 에서 다룰 수 있는 공통 event source 로 본다.

기본은 `recv` 와 `poller`, callback 은 선택 모델이다.
그리고 일반 timer 와 `Spot` 귀속 timer, request timeout 은 모두
thread-per-timer 가 아닌 shared scheduler 계층 위에서 관리한다.
