# Timer / Poller Integration Spec

## 목적

이 문서는 공통 timer 기능을 어떻게 노출할지 정의한다.

핵심 목표는 단순하다.

- 타이머도 소켓과 같은 event loop 안에서 다룰 수 있어야 한다
- 기본 사용 방식은 `recv` 또는 `poller` 기반이어야 한다
- callback 은 선택 기능으로 두고, callback 을 켠 동안에는 pull 모델을 막아야 한다
- 소켓 수신 모델과 timer 수신 모델이 같은 규칙을 가져야 한다

이 스펙은 `Spot` 전용 timer 를 따로 만드는 문서가 아니다.
공통 timer 를 `poller` 와 함께 쓰는 기준을 정의한다.


## 왜 별도 공통 timer 가 필요한가

현재 `zlink_timers_*()` 계열은 자체 callback 중심 구조다.
이 방식은 간단하지만, 다음과 같은 상황에서 설명이 어색해진다.

- 같은 thread 에서 socket message 와 timer fire 를 같이 기다리고 싶을 때
- 기존 `poller` 루프 하나로 socket 과 timer 를 함께 처리하고 싶을 때
- bindings 가 callback 과 polling 을 같은 방식으로 설명하고 싶을 때

그래서 timer 도 socket 과 같은 방식으로 event source 로 취급하는 편이 자연스럽다.

이번 전환에서 기존 `zlink_timers_*()` 계열은 유지하지 않는다.
새 기준은 `zlink_timer_*()` 와 `zlink_poller_add_timer()` 조합이다.
기존 timer API 는 폐기 대상으로 보고, core 구현과 bindings, 샘플, 테스트도
같은 기준으로 함께 옮긴다.
이 전환에서 기존 timer API 와의 호환 경로는 두지 않는다.


## 기본 원칙

### 1. timer 는 공통 기능이다

timer 는 `Spot` 전용 기능이 아니다.
특정 서비스에 묶지 않고 공통 handle 로 둔다.

즉 공개 이름은 아래처럼 간다.

- `zlink_timer_new()`
- `zlink_timer_destroy()`
- `zlink_timer_start()`
- `zlink_timer_stop()`
- `zlink_timer_recv()`
- `zlink_timer_handler()`
- `zlink_poller_add_timer()`
- `zlink_poller_remove_timer()`

기존 `zlink_timers_*()` 는 더 이상 기준 API 가 아니다.
새 timer 관련 설명과 예시는 모두 아래 공개 이름을 기준으로 쓴다.


### 2. 기본 수신 모델은 pull 이다

timer 도 socket 과 같은 방향으로 본다.

- 기본은 `recv`
- `poller` 를 붙이면 `poller` 로 readiness 를 받음
- callback 등록은 선택 기능

즉 callback 이 기본이 아니라, pull 모델이 기본이다.


### 3. callback 과 pull 모델을 섞지 않는다

timer 도 기존 receive 정책과 같은 규칙을 쓴다.

- `timer.recv()` 를 쓰는 중에는 `timer.handler()` 등록 금지
- `poller` 에 등록된 timer 는 `timer.handler()` 등록 금지
- `timer.handler()` 가 등록된 동안에는 `timer.recv()` 금지
- `timer.handler()` 가 등록된 동안에는 `poller` 등록 금지

충돌 시 `EBUSY` 를 사용한다.

이 규칙이 필요한 이유는 한 타이머 만료가
callback 과 recv 양쪽으로 동시에 보이는 일을 막기 위해서다.


### 4. timer event 는 `poller` 의 event source 다

timer 는 `poller` 안에서 socket, fd 와 같은 event source 로 동작해야 한다.

즉 `poller.wait()` 는 socket readiness 와 timer expiration 을 같은 결과 배열로 반환한다.


## 공개 타입

### poller event source 종류

```c
typedef enum zlink_poller_source_kind_t
{
    ZLINK_POLLER_SOURCE_SOCKET = 1,
    ZLINK_POLLER_SOURCE_FD = 2,
    ZLINK_POLLER_SOURCE_TIMER = 3
} zlink_poller_source_kind_t;
```

설명:

- `SOCKET` 은 기존 socket handle 이다
- `FD` 는 기존 file descriptor 등록이다
- `TIMER` 는 공통 timer handle 이다


### poller event 구조

```c
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

규칙:

- `source_kind = ZLINK_POLLER_SOURCE_SOCKET` 이면 `socket` 만 유효하다
- `source_kind = ZLINK_POLLER_SOURCE_FD` 이면 `fd` 만 유효하다
- `source_kind = ZLINK_POLLER_SOURCE_TIMER` 이면 `timer` 만 유효하다
- 사용하지 않는 필드는 `NULL` 또는 `0` 이어야 한다


### timer callback 타입

```c
typedef void (*zlink_timer_handler_fn)(
    void *timer,
    uint64_t fire_count,
    void *userdata);
```

설명:

- `fire_count` 는 이 timer 가 몇 번째로 발화했는지 알려주는 증가값이다
- one-shot 이면 첫 발화에서 `1`
- 반복 타이머면 `1`, `2`, `3` 순서로 증가한다


## 공개 API

### timer 생명주기

```c
void *zlink_timer_new(void);
int zlink_timer_destroy(void **timer_p);
```

규칙:

- `zlink_timer_new()` 는 정지 상태 timer handle 을 만든다
- `zlink_timer_destroy()` 는 timer 를 파괴한다
- `poller` 에 등록된 timer 를 destroy 하려 하면 `EBUSY` 로 실패시킨다

이 규칙을 두는 이유는 `poller` 가 이미 그 timer 를 event source 로 참조하고 있을 수 있기 때문이다.


### timer 제어

```c
int zlink_timer_start(void *timer,
                      uint64_t interval_ns,
                      uint64_t repeat_count);

int zlink_timer_stop(void *timer);
```

규칙:

- `interval_ns` 는 nanosecond 단위 반복 간격이다
- `repeat_count = 0` 이면 무한 반복이다
- `repeat_count = 1` 이면 one-shot 이다
- `repeat_count > 1` 이면 지정 횟수만큼 반복한 뒤 멈춘다
- `interval_ns = 0` 은 `EINVAL` 이다

설명:

- `0 = 무한 반복` 으로 두는 이유는 음수 sentinel 없이도 의미가 분명하기 때문이다
- one-shot 과 반복 timer 를 타입으로 나누지 않고, 하나의 timer 로 함께 처리한다


### timer pull 수신

```c
int zlink_timer_recv(void *timer,
                     uint64_t *fire_count_out,
                     int flags);
```

규칙:

- 성공 시 `fire_count_out` 에 발화 순번을 넣는다
- `ZLINK_DONTWAIT` 이고 아직 만료가 없으면 `EAGAIN`
- callback 모드가 켜져 있으면 `EBUSY`
- `poller` 에 등록된 상태에서 직접 `recv` 하면 `EBUSY`


### timer callback 수신

```c
int zlink_timer_handler(void *timer,
                        zlink_timer_handler_fn handler,
                        void *userdata);
```

규칙:

- callback 등록에 성공하면 timer 는 callback 전용 모드가 된다
- callback 모드에서는 `timer.recv()` 와 `poller` 등록을 허용하지 않는다
- 이미 `recv` 나 `poller` 모델을 쓰는 중이면 `EBUSY`


### poller 등록

```c
int zlink_poller_add_timer(void *poller,
                           void *timer,
                           void *user_data);

int zlink_poller_remove_timer(void *poller,
                              void *timer);
```

규칙:

- timer 는 `POLLIN` 과 비슷한 readable 개념 하나만 가진다
- 그래서 timer 등록은 별도 `events` 인자를 받지 않는다
- timer 가 발화 가능 상태가 되면 `poller.wait()` 는
  `source_kind = ZLINK_POLLER_SOURCE_TIMER` 로 event 를 돌려준다
- callback 모드 timer 를 poller 에 등록하려 하면 `EBUSY`
- 이미 다른 poller 에 등록된 timer 를 다시 등록하려 하면 `EBUSY`


## poller 와 timer 의 관계

### poller event 의미

timer event 의 `events` 값은 아래처럼 고정한다.

- `ZLINK_POLLIN`

이유는 timer 도 결국
"지금 읽을 수 있다"
는 readiness 신호 하나만 가지기 때문이다.


### poller wait 이후 처리

`poller.wait()` 가 timer event 를 돌려준 뒤,
사용자는 해당 timer 에 대해 `zlink_timer_recv()` 를 호출해 실제 발화 정보를 꺼낸다.

즉 `poller` 는 readiness notification,
`timer_recv()` 는 실제 event 소비를 담당한다.


## 수신 모델 충돌 규칙

timer 도 socket 과 같은 규칙을 가진다.

### 허용되는 조합

- `timer_recv()` 단독
- `poller + timer_recv()`
- `timer_handler()` 단독

### 허용하지 않는 조합

- `timer_handler()` + `timer_recv()`
- `timer_handler()` + `poller`
- `poller` 등록 중 direct callback 등록

오류:

- 충돌은 `EBUSY`


## ownership 과 수명

- timer handle 은 사용자가 destroy 할 때까지 유효하다
- `poller` 에 등록된 timer 는 `remove` 하기 전까지 poller 가 참조한다
- `zlink_poller_event_t` 안의 `timer` 포인터는 해당 wait 결과를 처리하는 동안 유효하다
- `timer_destroy()` 는 등록된 poller 참조가 남아 있으면 실패해야 한다


## 기존 `zlink_timers_*()` 와의 관계

이 스펙은 기존 `zlink_timers_*()` 를 더 이상 기준 모델로 보지 않는다.

기존 API 는 다음 이유로 현재 방향과 잘 맞지 않는다.

- callback 중심 구조다
- `poller` 와 직접 결합되지 않는다
- socket 과 timer 를 같은 event loop 로 다루기 어렵다

따라서 새로운 공개 기준은
공통 `zlink_timer_*()` 와 `zlink_poller_add_timer()` 조합이다.


## 오류 처리

권장 `errno`:

- `EINVAL`: 인자가 잘못됨
- `EFAULT`: handle 이 잘못됨
- `EAGAIN`: non-blocking recv 에서 아직 만료 없음
- `EBUSY`: recv/callback/poller 모델 충돌
- `ENOENT`: poller 에서 제거할 등록이 없음


## 예시

### 1. poller 기반 loop

```c
void *poller = zlink_poller_new();
void *timer = zlink_timer_new();

zlink_timer_start(timer, 1000000000ULL, 0);
zlink_poller_add_timer(poller, timer, NULL);

for (;;) {
    zlink_poller_event_t ev;
    if (zlink_poller_wait(poller, &ev, -1) <= 0)
        continue;

    if (ev.source_kind == ZLINK_POLLER_SOURCE_TIMER) {
        uint64_t fire_count = 0;
        if (zlink_timer_recv(ev.timer, &fire_count, 0) == 0) {
            /* handle timer tick */
        }
    }
}
```

### 2. callback 기반 timer

```c
static void on_timer(void *timer, uint64_t fire_count, void *userdata)
{
    LIBZLINK_UNUSED(timer);
    LIBZLINK_UNUSED(fire_count);
    LIBZLINK_UNUSED(userdata);
}

void *timer = zlink_timer_new();
zlink_timer_handler(timer, &on_timer, NULL);
zlink_timer_start(timer, 500000000ULL, 0);
```


## 회귀 테스트 기준

이 스펙 기준 구현은 아래를 자동 테스트로 고정해야 한다.

- one-shot timer 가 정확히 1회 발화한다
- `repeat_count = 0` 이면 반복 발화한다
- `repeat_count = N` 이면 정확히 N회 발화 후 멈춘다
- `timer_recv(ZLINK_DONTWAIT)` 는 만료 전 `EAGAIN`
- `poller + timer_recv()` 조합이 동작한다
- callback 등록 후 `timer_recv()` 는 `EBUSY`
- callback 등록 후 `poller_add_timer()` 는 `EBUSY`
- poller 등록 후 `timer_handler()` 는 `EBUSY`
- 등록된 timer 를 remove 없이 destroy 하려 하면 `EBUSY`
- socket event 와 timer event 를 같은 poller loop 에서 함께 처리할 수 있다
