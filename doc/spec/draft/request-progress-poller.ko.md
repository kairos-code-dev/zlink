# Poller 기반 request progress 초안

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 바인딩 runtime이 request/reply callback 완료를 효율적으로 진행하기
위한 설계안이다. 정식 spec 문서와 공개 헤더에 반영되기 전까지 응용은 이
동작에 의존하면 안 된다.

## 배경

request/reply callback API는 요청을 제출한 뒤, 나중에 reply completion을
callback이나 future completion으로 전달한다. core 내부에는 request completion
queue가 있고, 이 queue를 진행시키려면 completion signal을 기다린 뒤 drain해야
한다.

현재 여러 바인딩은 이 진행을 언어 runtime 쪽에서 반복 호출한다.

| 바인딩 | 현재 방식 |
|--------|-----------|
| Java | pending request가 있는 동안 background pump가 progress 함수를 반복 호출 |
| .NET | `Task.Run` worker가 progress 함수를 호출하고 yield/backoff를 수행 |
| C++ | `async_result_t::wait()` / `wait_for()` / `get()` 중 progress 함수를 호출 |
| Node | timer 기반으로 progress 함수를 반복 호출 |
| Go | goroutine과 ticker 기반으로 progress 함수를 반복 호출 |
| Rust | worker가 yield 또는 짧은 대기를 반복하며 progress 함수를 호출 |

이 방식은 동작하지만, 바인딩마다 같은 core 내부 지식을 반복해서 구현한다.
특히 timer나 짧은 sleep 기반 구현은 request completion이 없는 구간에서도
주기적으로 깨어나므로 CPU 사용량과 tail latency 해석을 흐릴 수 있다.

## 문제

현재 바인딩이 알아야 하는 구현 지식이 많다.

1. socket request와 spot request의 completion queue가 다르다.
2. router-to-spot request와 spot channel reply에는 별도 progress 경로가 있다.
3. completion queue를 진행하려면 내부 progress 함수를 호출해야 한다.
4. completion signal을 어떤 socket이나 fd로 기다릴 수 있는지 바인딩이 알아야 한다.
5. handle close, request 완료, worker 종료가 동시에 일어날 수 있어 바인딩마다
   같은 lifecycle 처리를 반복한다.

이 지식은 core request/reply 구현 상세다. 바인딩이 이 구조를 직접 알수록
정보가 누출되고, 바인딩별 progress loop가 서로 달라진다.

## 목표

이 초안의 목표는 다음과 같다.

1. 새 request-progress poller 타입을 만들지 않는다.
2. 기존 `zlink_poller`를 request completion 대기와 drain의 단일 진입점으로
   정리한다.
3. 바인딩이 1ms timer, yield loop, 직접 progress 함수 호출을 운용하지 않게 한다.
4. socket, spot, router-to-spot, spot channel reply completion 차이를 core가
   숨긴다.
5. 기존 request/reply callback API의 의미와 callback 호출 순서를 바꾸지 않는다.
6. 일반 응용 API 표면 증가는 최소화한다.

## 비목표

이 초안은 다음을 목표로 하지 않는다.

1. request/reply 공개 API 자체를 바꾸지 않는다.
2. callback을 core thread에서 언어 runtime callback으로 직접 호출하지 않는다.
3. 모든 바인딩에 같은 worker 구현을 강제하지 않는다.
4. request timeout 의미를 바꾸지 않는다.
5. message ownership 규칙을 바꾸지 않는다.
6. 별도 `zlink_request_progress_poller_*` 함수 묶음을 추가하지 않는다.

## 설계 원칙

POSD 관점의 판단 기준은 다음과 같다.

| 원칙 | 적용 |
|------|------|
| 깊은 모듈 | 바인딩은 poller add/wait/remove만 사용하고, completion signal 구조는 core가 숨긴다 |
| 정보 은닉 | request completion queue, signal socket, channel source handle을 바인딩에 노출하지 않는다 |
| 복잡성을 아래로 | handle 종류별 drain 차이와 hidden subject 등록을 core가 흡수한다 |
| 오류를 정의로 없애라 | pending completion이 없는 drain은 성공한 no-op으로 정의한다 |

## 권장안

기존 `zlink_poller`에 request completion 전용 interest를 추가한다. 바인딩은
completion 대상 handle을 poller에 등록하고, worker에서 `zlink_poller_wait()`로
blocking wait한다. wait가 completion signal을 감지하면 core가 내부적으로
completion queue를 drain한다.

이 기능은 이미 poller가 해결하는 문제인 "대상을 등록하고 준비될 때까지
기다린다"와 같은 형태다. 별도 request progress poller API를 만들면 표면이
넓어지고 같은 생명주기 처리가 반복된다. 바인딩별 worker 개선만으로 처리하면
completion signal 지식이 Java, .NET, C++, Node, Go, Rust에 계속 흩어진다.

따라서 이 초안은 기존 poller를 확장하는 한 가지 방향만 다룬다.

## API 변경 범위

새 함수 묶음은 추가하지 않는다. 공개 표면 증가는 event flag 하나로 제한한다.

```c
typedef enum zlink_poller_event_flag_e {
    ZLINK_POLLIN = 1,
    ZLINK_POLLOUT = 2,
    ZLINK_POLLERR = 4,
    ZLINK_POLLPRI = 8,
    ZLINK_POLLITEMS_DFLT = 16,
    ZLINK_POLLCOMPLETION = 32
} zlink_poller_event_flag_e;
```

`ZLINK_POLLCOMPLETION`은 request/reply completion queue를 진행하기 위한
binding runtime용 interest다. 일반 응용은 보통 이 flag를 직접 사용할 필요가
없다.

기존 함수는 그대로 사용한다. completion-only worker는 subject 등록에는 `add`와
`remove`를 쓰고, worker 깨움용 control fd에는 `add_fd`와 `remove_fd`를 쓸 수
있다. `modify`는 기존 readiness poller용 함수로 남기고, completion-only
interest 변경 경로로 요구하지 않는다.

```c
ZLINK_EXPORT zlink_config_result_t zlink_poller_add(
    void *poller_,
    void *subject_,
    void *user_data_,
    short events_);

ZLINK_EXPORT zlink_config_result_t zlink_poller_remove(
    void *poller_,
    void *subject_);

ZLINK_EXPORT zlink_config_result_t zlink_poller_add_fd(
    void *poller_,
    zlink_fd_t fd_,
    void *user_data_,
    short events_);

ZLINK_EXPORT zlink_config_result_t zlink_poller_remove_fd(
    void *poller_,
    zlink_fd_t fd_);

ZLINK_EXPORT int zlink_poller_wait(
    void *poller_,
    zlink_poller_event_t *events_,
    int n_events_,
    long timeout_,
    zlink_config_result_t *error_out_);
```

위 시그니처에서 `subject_`는 ABI상 기존 `socket_` 인자와 같은 위치다. 정식
문서에서는 이름을 바꾸지 않더라도 의미는 socket, spot, spot node처럼 poller가
지원하는 handle로 설명해야 한다.

## Completion Interest 의미

`ZLINK_POLLCOMPLETION`으로 등록한 subject는 일반 receive readiness를 뜻하지
않는다. 이 interest는 "해당 subject의 request completion queue를 진행할 수
있을 때 worker를 깨운다"는 뜻이다.

지원 대상:

| subject | core가 숨기는 drain 대상 |
|---------|--------------------------|
| DEALER socket | socket request completion queue |
| ROUTER socket | socket request completion queue, router-to-spot completion queue |
| Spot | spot request completion queue, spot channel reply completion queue |

Spot node는 request/reply completion queue를 직접 소유하지 않는다면
`ZLINK_POLLCOMPLETION` 등록 대상이 아니다. 정식 구현에서 spot node가 completion
queue를 소유하게 되면 이 표에 추가한다.

completion-only 등록 규칙:

1. `events_`는 `ZLINK_POLLCOMPLETION` 단독 값이어야 한다.
2. core는 subject의 public receive readiness를 poller에 등록하지 않는다.
3. core는 subject의 completion signal만 hidden subject로 등록한다.
4. `user_data_`는 `NULL`을 권장한다. completion worker는 public event가 아니라
   pending count와 future 상태를 기준으로 종료를 판단한다.

이 규칙이 있어야 completion worker가 응용의 receive poller와 독립적으로 동작한다.
`ZLINK_POLLIN | ZLINK_POLLCOMPLETION` 같은 조합은 이 초안의 범위에 넣지 않는다.

## Wait와 Drain 규칙

`zlink_poller_wait()`는 completion signal을 감지하면 core 내부에서 해당
completion queue를 drain한다.

규칙:

1. drain은 가능한 만큼 진행한다.
2. pending completion이 없으면 성공한 no-op이다.
3. drain 중 언어 바인딩 callback shim이 호출될 수 있다.
4. drain은 기존 callback 호출 순서를 바꾸지 않는다.
5. subject가 닫히는 중이면 termination completion을 가능한 만큼 전달한다.
6. remove는 등록된 subject에 대해서만 호출한다. 바인딩은 pending count를 사용해
   0에서 1로 바뀔 때 add하고, 1에서 0으로 바뀔 때 remove한다.

completion-only 등록에서 `zlink_poller_wait()`가 completion을 drain했지만
public readiness event가 없다면 `0`을 반환할 수 있다. 바인딩 worker는 이 반환을
timeout과 같은 치명적 상태로 보지 말고 pending count나 future 상태를 다시
확인해야 한다.

completion worker는 `zlink_poller_wait()`에 최소 1개 이상의 event slot을 넘긴다.
completion-only 경로는 public event를 기대하지 않지만, 기존 poller 구현은 event
buffer가 있는 경로를 기준으로 동작하므로 빈 buffer에 의존하지 않는다.

## 일반 Readiness와의 관계

`ZLINK_POLLIN`과 `ZLINK_POLLOUT`은 기존 의미를 유지한다.

`ZLINK_POLLCOMPLETION`은 단독으로만 사용한다. completion worker는
`ZLINK_POLLCOMPLETION`만 사용하고, 응용 poller는 기존 readiness flag만 사용한다.
completion worker와 응용 poller를 분리하면
`zlink_poller_wait()`가 0을 반환하는 hidden drain 경로를 응용 코드가 오해할
가능성이 줄어든다.

## 바인딩 사용 모델

이 절은 `ZLINK_POLLCOMPLETION`이 각 바인딩에서 충분한지 판단하기 위한 기준을
정리한다. 공통 조건은 바인딩이 더 이상 `*_request_progress_internal` 계열
함수를 직접 호출하지 않아도 request completion이 완료되어야 한다는 점이다.

| 바인딩 | 현재 필요한 지식 | `ZLINK_POLLCOMPLETION` 적용 후 필요한 지식 |
|--------|------------------|--------------------------------------------|
| Java | socket/spot/channel progress 함수 구분, background pump 생명주기 | subject 등록, pending count, poller wait |
| .NET | socket/spot/channel progress 함수 구분, Task worker backoff | subject 등록, pending count, poller wait |
| C++ | async result별 progress 함수 또는 progress 누락, 1ms wait slice | subject 등록, poller wait, future 상태 확인 |
| Node | timer 기반 progress 호출 | subject 등록, shared worker wait |
| Go | ticker 기반 progress 호출 | subject 등록, goroutine wait |
| Rust | yield 기반 progress 호출 | subject 등록, worker wait |

`ZLINK_POLLCOMPLETION`이 충분하려면 core가 다음을 보장해야 한다.

1. DEALER/ROUTER socket을 등록하면 socket request completion이 drain된다.
2. ROUTER socket을 등록하면 router-to-spot request completion도 함께 drain된다.
3. Spot을 등록하면 일반 spot request completion과 routed spot request completion이 drain된다.
4. Spot을 등록하면 spot channel reply completion도 함께 drain된다.
5. drain 중 completion callback은 기존 callback 전달 경로를 그대로 사용한다.
6. close 중 발생한 termination completion도 가능한 만큼 drain된다.
7. completion이 없는 상태에서 wait는 CPU를 주기적으로 깨우지 않는다.
8. completion-only 등록은 receive readiness를 소비하거나 숨기지 않는다.

4번이 빠지면 Java, .NET, C++는 여전히 channel reply용 progress 함수를 따로
알아야 한다. 그러면 이 설계는 다른 바인딩에 충분하지 않다.

### Poller 소유권

shared worker를 쓰는 바인딩은 poller handle을 worker thread가 소유한다.
submit thread가 `zlink_poller_add()`나 `zlink_poller_remove()`를 wait 중인
poller에 직접 호출하지 않는다. 기존 poller가 add/remove와 wait의 동시 호출을
보장한다는 공개 계약이 없기 때문이다.

권장 구조:

1. submit thread는 pending count를 갱신하고 worker command queue에 add/remove
   요청을 넣는다.
2. worker thread는 command queue를 처리한 뒤 `zlink_poller_wait()`를 호출한다.
3. worker가 `zlink_poller_wait()` 안에 있을 때도 깨어날 수 있도록 control fd나
   pipe를 poller에 함께 등록한다.
4. 새 command가 들어오면 submit thread는 command queue에 요청을 넣고 control
   fd나 pipe에 신호를 쓴다.
5. worker가 control event를 받으면 신호를 drain하고 command queue를 처리한다.
6. poller handle에 대한 add/remove/wait 호출은 worker thread에서 직렬화한다.

이 규칙은 core API 표면을 늘리지 않고도 기존 poller를 안전하게 재사용하기 위한
조건이다. 정식 구현에서 core가 poller add/remove/wait 동시 호출을 보장한다면
이 제약은 완화할 수 있지만, 이 초안은 그런 보장을 요구하지 않는다.

등록된 completion subject가 하나도 없을 때는 worker가 runtime 조건 변수로 쉬어도
된다. 그러나 한 번 `zlink_poller_wait()`에 들어간 뒤에는 조건 변수만으로 깨울 수
없으므로, control fd나 pipe처럼 poller가 관찰할 수 있는 깨움 수단이 필요하다.

### Java와 .NET

Java와 .NET은 shared worker를 둘 수 있다.

1. request submit 전에 callback state를 준비한다.
2. request submit이 성공하면 subject별 pending count를 증가시킨다.
3. count가 0에서 1로 바뀌면 worker command queue에 add 요청을 넣는다.
4. worker는 add 요청을 처리해 shared poller에 subject를
   `ZLINK_POLLCOMPLETION`으로 등록한다.
5. worker는 `zlink_poller_wait()`를 blocking 호출한다.
6. wait가 깨어나면 core가 completion queue를 drain한다.
7. callback 또는 future completion에서 pending count를 감소시킨다.
8. count가 1에서 0으로 바뀌면 worker command queue에 remove 요청을 넣는다.
9. worker는 remove 요청을 처리해 poller에서 subject를 제거한다.

이 구조에서는 바인딩이 `zlink_socket_request_progress_internal()`이나
`zlink_spot_request_progress_internal()`을 직접 호출하지 않는다. channel reply도
Spot subject의 completion drain에 포함되어야 하므로
`zlink_spot_channel_reply_progress_from()`도 호출하지 않는다.

필요한 바인딩 변경은 다음과 같다.

1. worker 내부 progress 호출을 `zlink_poller_wait()`로 바꾼다.
2. subject별 pending count가 0에서 1로 바뀔 때만 add command를 보낸다.
3. completion callback 또는 future 완료에서 pending count를 감소시킨다.
4. pending count가 0이 되면 remove command를 보낸다.
5. wait가 0을 반환해도 timeout으로 단정하지 않고 완료 상태를 다시 확인한다.

이 변경으로 Java와 .NET은 backoff 정책을 직접 설계하지 않아도 된다.

### C++

C++의 `async_result_t`는 background worker 없이 `wait()`와 `wait_for()` 안에서
completion-only poller를 사용할 수 있다.

1. async result가 progress 대상 subject를 가진다.
2. `wait()`는 subject를 completion-only poller에 등록하고 무기한 wait한다.
3. `wait_for(timeout)`은 남은 timeout만큼 poller wait를 반복한다.
4. wait가 반환될 때마다 future 상태를 확인한다.
5. future가 ready가 되면 subject를 poller에서 제거한다.

이 방식은 기존 `progress_slice()` 1ms loop를 blocking wait로 바꾼다.

C++에서 충분하려면 async result가 "어떤 progress 함수를 호출할지"가 아니라
"어떤 subject를 completion-only poller에 등록할지"만 들고 있어야 한다. 따라서
현재 `std::function<void()> progress` 형태는 subject handle과 kind를 담는 작은
값 객체로 바꾸는 편이 낫다. 이 값 객체는 DEALER/ROUTER socket과 Spot만
구분하고, channel name이나 channel source는 보관하지 않는다.

또한 C++의 모든 async request 결과가 같은 subject 기반 대기 모델을 써야 한다.
spot request처럼 progress 함수가 이미 붙은 경로뿐 아니라 actor join, actor
leave, actor lookup 같은 actor 계열 async 결과도 completion subject를 가져야
한다. 특정 async result가 future만 들고 completion subject를 갖지 않으면, 그
경로는 여전히 호출자가 별도 진행 조건을 알아야 하므로 이 설계가 C++에 충분하지
않다.

### Node, Go, Rust

Node, Go, Rust도 같은 모델을 따른다.

1. pending request가 생기면 worker command queue에 add 요청을 넣는다.
2. worker는 completion-only poller에 subject를 등록한다.
3. worker는 timer나 ticker 대신 `zlink_poller_wait()`로 blocking wait한다.
4. callback 전달은 각 언어 runtime의 기존 안전한 callback 전달 경로를 사용한다.

이 바인딩들에서 충분하려면 worker가 event loop나 scheduler를 1ms 주기로 깨우지
않아도 된다. poller wait는 native worker thread나 goroutine에서 수행하고,
callback 전달만 각 runtime의 안전한 queue로 넘긴다.

## Spot Channel Reply 처리

이 초안에서 가장 중요한 설계 조건은 spot channel reply progress를 바인딩에
노출하지 않는 것이다.

현재 일부 바인딩은 별도 progress 함수나 channel source를 알아야 한다. 정식
구현에서는 `ZLINK_POLLCOMPLETION`으로 Spot을 등록하면 core가 아래 경로를 모두
흡수해야 한다.

1. 일반 spot request completion
2. routed spot request completion
3. spot channel reply completion
4. termination completion

바인딩이 dealer subject, channel name, source handle을 별도로 등록해야 한다면
이 설계는 실패한 것이다. 그런 지식은 core 내부에 남아야 한다.

core에는 이미 Spot progress가 direct completion과 channel reply source들을
스냅샷으로 모아 drain하는 내부 흐름이 있다. 따라서 Spot subject 하나로 흡수하는
방향은 가능하다. 다만 poller wait가 channel reply completion에도 깨어나려면
channel reply source의 completion enqueue가 Spot의 aggregate completion signal도
깨워야 한다.

정식 구현 기준:

1. Spot의 `ZLINK_POLLCOMPLETION` 등록은 Spot aggregate completion signal 하나를
   hidden subject로 등록한다.
2. 일반 spot reply, routed spot reply, spot channel reply enqueue는 모두 이
   aggregate signal을 깨운다.
3. wait가 깨어나면 core는 direct completion과 모든 channel reply source
   completion을 함께 drain한다.
4. channel reply source별 signal socket이나 dealer handle은 바인딩에 노출하지
   않는다.

## Core 구현 기준

기존 poller 구현에는 이미 hidden completion registration 개념이 있다. 이 초안은
그 구조를 별도 API로 빼지 않고 정식 계약으로 다듬는 방향이다.

구현 기준:

1. `ZLINK_POLLCOMPLETION` 등록은 public socket readiness registration을 만들지
   않는다.
2. `ZLINK_POLLCOMPLETION` 등록은 completion signal용 hidden registration만 만든다.
3. 기존 `ZLINK_POLLIN` / `ZLINK_POLLOUT` 등록 동작은 호환성을 위해 유지한다.
4. completion-only drain은 기존 request completion callback scope와 owner-thread
   규칙을 그대로 사용한다.
5. hidden completion event가 여러 개 준비되면 `zlink_poller_wait()` 한 번에서
   가능한 만큼 drain한다.
6. hidden drain이 callback을 실행했지만 public event가 없으면 `0`을 반환할 수
   있다.
7. `error_out_`은 hidden drain 성공, timeout, public event 반환 모두에서
   `ZLINK_CONFIG_OK`를 유지한다.

기존 일반 poller에 socket이나 spot을 `ZLINK_POLLIN`으로 등록했을 때 completion
signal을 함께 drain하는 내부 동작은 유지할 수 있다. 다만 새 바인딩 worker는
그 부수 효과에 의존하지 않고 `ZLINK_POLLCOMPLETION` 단독 등록을 사용한다.

## 제거할 Internal 함수

이 변경은 브레이킹 체인지로 진행한다. 아래 internal progress 함수는 공개
헤더와 export 목록에서 제거한다. 바인딩은 이 함수들을 직접 호출하지 않고
`ZLINK_POLLCOMPLETION` poller 경로로만 request completion을 진행한다.

```c
int zlink_socket_request_progress_internal(void *socket_);
int zlink_spot_request_progress_internal(void *spot_);
int zlink_spot_request_channel_progress_internal(void *spot_, const char *channel_name_);
int zlink_spot_channel_reply_progress_from(void *spot_, void *dealer_);
```

core 내부에서 같은 동작이 필요하면 C export가 아닌 내부 helper로 남긴다. 새
바인딩은 이 helper를 볼 수 없어야 한다.

## 오류 처리

| 상황 | 결과 |
|------|------|
| `poller_ == NULL` | invalid handle |
| `subject_ == NULL` | invalid handle |
| 지원하지 않는 subject에 `ZLINK_POLLCOMPLETION` 등록 | invalid handle 또는 invalid argument |
| 이미 등록된 subject add | 기존 poller 중복 등록 규칙을 따른다 |
| 등록되지 않은 subject remove | 기존 poller remove 오류 의미를 따른다 |
| pending completion 없음 | drain 성공 no-op |
| wait timeout | 기존 `zlink_poller_wait()` timeout 의미 유지 |
| wait 중 poller destroy | 기존 poller 종료 의미 유지 |

같은 subject를 같은 poller에 중복 등록하거나 등록되지 않은 subject를 제거하는
동작은 새 계약에서 바꾸지 않는다. 바인딩 worker는 pending count로 0에서 1로
바뀔 때만 add하고, 1에서 0으로 바뀔 때만 remove해서 기존 poller 규칙과
충돌하지 않게 한다.

## 성능 기대

주요 성능 개선은 새 flag 자체가 아니라 timer polling 제거에서 나온다.

기대 효과:

1. pending request 중 주기적 1ms wakeup 제거
2. idle 상태 CPU 사용량 감소
3. request 수가 적은 workload에서 불필요한 runtime 간섭 감소
4. 바인딩별 progress loop 차이 축소
5. C++ `wait_for()`의 1ms slice 기반 대기 제거

큰 request 처리량에서는 callback 실행, message allocation, transport 비용이 더
클 수 있으므로 처리량 개선보다 CPU 사용량과 tail latency 안정성 개선이 더
중요한 지표다.

## 검증 계획

정식 구현 전후로 아래 항목을 확인해야 한다.

1. Java async request 테스트가 직접 progress 함수 호출 없이 완료되는지 확인한다.
2. .NET async request 테스트가 `RequestProgressPump`의 backoff loop 없이
   완료되는지 확인한다.
3. C++ `async_result_t::wait()`와 `wait_for()`가 1ms progress slice 없이
   완료되는지 확인한다.
4. Node promise/callback request 테스트가 timer 없이 완료되는지 확인한다.
5. Go ticker 제거 후 request timeout이 기존 result로 전달되는지 확인한다.
6. Rust yield loop 제거 후 callback 순서가 유지되는지 확인한다.
7. request 중 socket/spot close가 termination completion으로 정리되는지 확인한다.
8. pending request가 없는 상태에서 worker가 CPU를 쓰지 않는지 확인한다.
9. spot channel reply completion이 별도 바인딩 progress 함수 없이 완료되는지
   확인한다.

## 확정 결정

1. spot channel reply completion은 Spot subject 하나로 흡수한다. core는 Spot
   aggregate completion signal을 두고, channel reply source completion도 이
   signal을 깨우게 한다.
2. `zlink_poller_add()`의 기존 인자는 정식 spec에서 subject handle로 설명한다.
   C 헤더의 인자 이름은 ABI가 아니므로 `socket_`에서 `subject_`로 바꿔도 된다.
   문서에서는 socket, spot처럼 poller가 지원하는 handle을 모두 subject라고 부른다.
3. event flag 이름은 `ZLINK_POLLCOMPLETION`으로 둔다. 이 이름은 `ZLINK_POLLIN` /
   `ZLINK_POLLOUT`과 같은 poller interest임을 유지하면서, request/reply
   completion queue를 기다리는 용도를 드러낸다. 세부 의미는 spec에서
   "request/reply completion 전용"으로 제한한다.
