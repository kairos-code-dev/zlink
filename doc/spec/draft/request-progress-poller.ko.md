# Request progress poller 초안

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 바인딩에서 request/reply callback 완료를 효율적으로 진행하기 위한
설계안이다. 정식 spec 문서와 공개 헤더에 반영되기 전까지 응용은 이 동작에
의존하면 안 된다.

## 배경

request/reply callback API는 요청을 제출한 뒤, 나중에 reply completion을
callback으로 전달한다. core 내부에는 completion queue가 있고, 이 queue를
진행시키기 위해 request progress 함수가 필요하다.

현재 여러 바인딩은 이 진행을 언어 런타임 쪽에서 반복 호출한다.

| 바인딩 | 현재 방식 |
|--------|-----------|
| Node | `setInterval(1ms)` 로 progress 함수 호출 |
| Go | goroutine + `time.NewTicker(1ms)` 로 progress 함수 호출 |
| Rust | pending 중 worker thread가 `yield_now()` 하며 progress 함수 호출 |
| Python/.NET | 별도 progress pump로 progress 함수 호출 |

이 방식은 동작하지만, pending request가 있는 동안 불필요한 wakeup을 만든다.
특히 Node와 Go의 1ms timer 방식은 request가 거의 완료되지 않는 구간에서도
주기적으로 깨어나므로 CPU 사용량과 tail latency 해석을 흐릴 수 있다.

## 문제

현재 바인딩이 알아야 하는 구현 지식이 많다.

1. socket request와 spot request의 progress 함수가 다르다.
2. request completion queue를 진행하려면 내부 progress 함수를 호출해야 한다.
3. poller에 socket이나 spot을 등록하면 core 내부 completion signal도 함께
   관찰할 수 있다는 사실을 바인딩이 알아야 한다.
4. handle close, request 완료, progress worker 종료가 동시에 일어날 수 있어
   바인딩마다 같은 lifecycle 처리를 반복하게 된다.

이 지식은 core request/reply 구현 상세에 가깝다. 바인딩이 이 구조를 직접
알수록 정보가 누출되고, 바인딩마다 다른 형태의 polling loop가 생긴다.

## 목표

이 초안의 목표는 다음과 같다.

1. request completion이 있을 때만 worker가 깨어나도록 한다.
2. 바인딩이 1ms timer를 직접 운용하지 않게 한다.
3. socket, spot, router-to-spot request completion 차이를 core 쪽에서 숨긴다.
4. 바인딩은 "request progress 대상 등록, 대기, drain"만 호출하게 한다.
5. 기존 request/reply callback API의 의미와 callback 호출 순서를 바꾸지 않는다.

## 비목표

이 초안은 다음을 목표로 하지 않는다.

1. request/reply 공개 API 자체를 바꾸지 않는다.
2. callback을 core thread에서 언어 runtime callback으로 직접 호출하지 않는다.
3. 모든 바인딩에 같은 worker 구현을 강제하지 않는다.
4. request timeout 의미를 바꾸지 않는다.
5. message ownership 규칙을 바꾸지 않는다.

## 설계 원칙

이 API는 일반 응용 개발자용 API가 아니라 바인딩 runtime용 API다.
따라서 이름과 문서에서 용도를 분명히 제한해야 한다.

POSD 관점의 판단 기준은 다음과 같다.

| 원칙 | 적용 |
|------|------|
| 깊은 모듈 | 바인딩은 단순한 wait/drain API만 보고, completion signal socket 구조는 core가 숨긴다 |
| 정보 은닉 | request completion queue, signal socket, poller subject kind를 바인딩에 노출하지 않는다 |
| 복잡성을 아래로 | handle 종류별 등록과 drain 차이를 core가 흡수한다 |
| 오류를 정의로 없애라 | pending이 없는 대상의 drain은 성공한 no-op으로 정의한다 |

## 선택지

### 선택지 A: 바인딩별 worker 개선

각 바인딩이 기존 C API와 internal progress 함수를 조합해서 blocking wait 기반
worker를 직접 구현한다.

장점:
- C API를 추가하지 않는다.
- Node만 빠르게 고칠 수 있다.

단점:
- 바인딩마다 completion signal 지식을 반복한다.
- core 내부 구조가 바인딩 코드에 계속 드러난다.
- Go, Rust, Python, .NET이 서로 다른 구현을 유지한다.

### 선택지 B: request progress poller API 추가

core가 바인딩 runtime용 progress poller API를 제공한다. 바인딩은 대상 handle을
등록하고, worker thread에서 wait한 뒤, 깨어난 대상만 drain한다.

장점:
- 바인딩 인터페이스가 단순해진다.
- completion signal 구조가 core 안에 숨는다.
- 여러 바인딩이 같은 의미를 공유할 수 있다.
- Node의 `setInterval(1ms)`와 Go의 1ms ticker를 같은 방향으로 제거할 수 있다.

단점:
- C API가 늘어난다.
- 이 API가 일반 응용 API처럼 오해될 수 있다.

선택: **선택지 B**를 권장한다. 이 기능은 바인딩 runtime이 반복해서 구현해야
하는 내부 지식이므로, core가 얕은 내부 구조를 숨기는 편이 더 낫다.

## API 범위

이 API는 `zlink_binding_*` 또는 `zlink_request_progress_*` 이름 중 하나를
사용해야 한다. 일반 응용 API와 구분하려면 `zlink_binding_*` 접두사가 더
명확하다.

이 초안에서는 의미가 드러나는 이름을 위해 `zlink_request_progress_*`를 사용한다.
정식 반영 전에는 이름을 다시 검토해야 한다.

## 타입

```c
typedef enum zlink_request_progress_subject_type_t {
    ZLINK_REQUEST_PROGRESS_SUBJECT_SOCKET = 1,
    ZLINK_REQUEST_PROGRESS_SUBJECT_SPOT = 2
} zlink_request_progress_subject_type_t;

typedef struct zlink_request_progress_event_t {
    void *subject;
    zlink_request_progress_subject_type_t subject_type;
    void *user_data;
} zlink_request_progress_event_t;
```

`subject`는 등록한 socket 또는 spot handle이다.
`subject_type`은 바인딩이 어떤 drain 함수를 호출해야 하는지 구분하는 값이다.
`user_data`는 등록 시 바인딩이 넘긴 값이며, core는 해석하지 않는다.

## 함수

### Poller 생성과 해제

```c
ZLINK_EXPORT void *zlink_request_progress_poller_new(void);

ZLINK_EXPORT zlink_close_result_t zlink_request_progress_poller_destroy(
    void *poller_);
```

`zlink_request_progress_poller_new()`는 request progress 대상을 등록할 수 있는
poller를 만든다. 실패하면 `NULL`을 반환하고 `zlink_errno()`에 원인을 남긴다.

`zlink_request_progress_poller_destroy()`는 poller를 닫는다. poller에 등록된
대상이 남아 있어도 모두 제거한다. 등록된 request 자체를 취소하지는 않는다.

### 대상 등록

```c
ZLINK_EXPORT zlink_config_result_t zlink_request_progress_poller_add_socket(
    void *poller_,
    void *socket_,
    void *user_data_);

ZLINK_EXPORT zlink_config_result_t zlink_request_progress_poller_add_spot(
    void *poller_,
    void *spot_,
    void *user_data_);
```

등록은 같은 `poller_` 안에서 같은 subject에 대해 idempotent하게 동작한다.
이미 등록된 subject를 다시 등록하면 `user_data_`를 새 값으로 교체하고 성공한다.
이 정의는 바인딩이 pending count 변경 중 중복 등록 여부를 별도로 기억하지
않아도 되게 하기 위함이다.

지원하지 않는 handle이면 `ZLINK_CONFIG_INVALID_HANDLE`을 반환한다.
request completion signal 준비에 실패하면 errno 기반 config result를 반환한다.

### 대상 제거

```c
ZLINK_EXPORT zlink_config_result_t zlink_request_progress_poller_remove(
    void *poller_,
    void *subject_);
```

등록되지 않은 subject 제거는 성공한 no-op이다. 바인딩에서는 request 완료와
handle close가 경합할 수 있으므로, 제거 실패를 정상 종료 경로의 오류로 만들지
않는다.

### 대기

```c
ZLINK_EXPORT zlink_recv_result_t zlink_request_progress_poller_wait(
    void *poller_,
    zlink_request_progress_event_t *events_,
    size_t max_events_,
    int timeout_ms_,
    size_t *event_count_out_);
```

`zlink_request_progress_poller_wait()`는 등록된 subject 중 request completion
progress가 필요한 대상이 생길 때까지 기다린다.

반환 규칙:
- 이벤트가 있으면 `ZLINK_RECV_OK`를 반환하고 `event_count_out_`에 개수를 쓴다.
- timeout이면 `ZLINK_RECV_NO_DATA`를 반환한다.
- poller가 닫히거나 interrupt되면 적절한 recv result와 errno를 반환한다.

`max_events_`는 1 이상이어야 한다. `events_`와 `event_count_out_`은 `NULL`이면
안 된다.

### Drain

```c
ZLINK_EXPORT zlink_config_result_t zlink_request_progress_drain_socket(
    void *socket_);

ZLINK_EXPORT zlink_config_result_t zlink_request_progress_drain_spot(
    void *spot_);
```

drain 함수는 해당 subject의 pending completion queue를 가능한 만큼 진행한다.
pending completion이 없으면 성공한 no-op이다.

이 함수는 기존 internal 함수의 공개 후보 성격을 가진다. 이름을 공개 API로
올릴지, 바인딩 전용 namespace로 둘지는 정식 반영 전에 결정해야 한다.

## Node 바인딩 사용 모델

```text
+-------------------+      +----------------------+
| JS request submit |----->| native request call  |
+-------------------+      +----------------------+
          |                           |
          v                           v
+-------------------+      +----------------------+
| pending count +1  |----->| progress poller add  |
+-------------------+      +----------------------+
                                      |
                                      v
                           +----------------------+
                           | worker wait blocking |
                           +----------------------+
                                      |
                                      v
                           +----------------------+
                           | drain target handle  |
                           +----------------------+
                                      |
                                      v
                           +----------------------+
                           | TSFN delivers reply  |
                           +----------------------+
```

위 다이어그램에서 worker는 Node event loop thread가 아니다.
worker는 blocking wait를 수행하고, reply callback 전달은 기존처럼 Node의
thread-safe callback 경로를 사용한다.

## Lifecycle 규칙

### Pending count

바인딩은 subject별 pending count를 가진다.

1. request submit 성공 후 count를 증가시킨다.
2. count가 0에서 1로 바뀌면 progress poller에 subject를 등록한다.
3. callback 또는 promise 완료 시 count를 감소시킨다.
4. count가 1에서 0으로 바뀌면 progress poller에서 subject를 제거한다.

submit이 실패하면 count를 증가시키지 않는다. submit 성공 후 callback 등록이
실패한 경우에는 request 결과가 유실될 수 있으므로, 바인딩 구현에서 submit 전에
callback state를 먼저 준비해야 한다.

### Close와 경합

handle close는 pending request를 종료시키거나, core가 termination completion을
queue에 넣을 수 있다. 따라서 바인딩은 close 직후에도 worker가 한 번 더 깨어날
수 있음을 허용해야 한다.

`zlink_request_progress_poller_remove()`가 등록되지 않은 subject에 대해 성공한
no-op인 이유도 이 경합을 단순화하기 위함이다.

### Worker 종료

worker는 등록된 subject가 없고 pending count가 모두 0이면 idle 상태가 된다.
구현은 다음 중 하나를 선택할 수 있다.

1. worker를 유지하고 조건 변수로 대기한다.
2. 일정 idle timeout 뒤 worker를 종료한다.
3. process 종료까지 shared worker를 유지한다.

Node 바인딩에서는 shared worker 1개와 idle 대기가 가장 단순하다.

## 오류 처리

| 상황 | 결과 |
|------|------|
| `poller_ == NULL` | invalid handle |
| `subject_ == NULL` | invalid handle |
| 이미 등록된 subject add | 성공, `user_data` 교체 |
| 등록되지 않은 subject remove | 성공 no-op |
| pending completion 없음 | drain 성공 no-op |
| wait timeout | `ZLINK_RECV_NO_DATA` |
| wait 중 poller destroy | interrupted 또는 terminated 계열 recv result |

정확한 result enum 매핑은 기존 `zlink_config_result_t`,
`zlink_recv_result_t`, errno map과 맞춰야 한다.

## 공개 범위 주의 사항

이 API는 일반 응용이 직접 호출할 필요가 거의 없다. 일반 응용은 기존
request/reply API를 사용하고, 바인딩 runtime이 progress 처리를 맡아야 한다.

따라서 정식 반영 시 다음 중 하나를 선택해야 한다.

1. 공개 C API로 추가하되 문서에 "binding runtime support" 용도임을 명시한다.
2. `core/include/zlink/binding.h` 같은 별도 헤더로 분리한다.
3. 기존처럼 internal 심볼로 두고, 각 바인딩이 C API 없이 자체 worker를 구현한다.

POSD 기준으로는 2번이 가장 명확하다. 일반 사용자 API와 바인딩 runtime API를
섞지 않으면서도, 바인딩이 internal 구현에 의존하지 않게 할 수 있기 때문이다.

## 기존 API와의 관계

이 초안은 아래 함수의 의미를 대체하거나 정리한다.

```c
int zlink_socket_request_progress_internal(void *socket_);
int zlink_spot_request_progress_internal(void *spot_);
int zlink_spot_channel_reply_progress_from(void *spot_, void *dealer_);
```

`zlink_spot_channel_reply_progress_from()`은 channel reply bridge처럼 특정 source를
drain해야 하는 경로가 있어 별도 검토가 필요하다. 정식 API에서 이 함수를
흡수하려면 subject type을 더 세분화해야 할 수 있다.

예:

```c
typedef enum zlink_request_progress_subject_type_t {
    ZLINK_REQUEST_PROGRESS_SUBJECT_SOCKET = 1,
    ZLINK_REQUEST_PROGRESS_SUBJECT_SPOT = 2,
    ZLINK_REQUEST_PROGRESS_SUBJECT_SPOT_CHANNEL = 3
} zlink_request_progress_subject_type_t;
```

다만 subject type이 늘어날수록 API가 얕아질 수 있다. 가능하면 core가
spot channel source까지 내부에서 숨기고, 바인딩은 spot subject 하나만 등록하는
방향이 낫다.

## 성능 기대

주요 성능 개선은 C API 추가 자체가 아니라 timer polling 제거에서 나온다.

기대 효과:
- pending request 중 1ms 주기 wakeup 제거
- idle 상태 CPU 사용량 감소
- request 수가 적은 workload에서 불필요한 event loop 간섭 감소
- 바인딩별 progress loop 차이 축소

큰 request 처리량에서는 callback 실행, message allocation, transport 비용이 더
클 수 있으므로, 처리량 개선보다 CPU 사용량과 tail latency 안정성 개선이 더
중요한 지표다.

## 검증 계획

정식 구현 전후로 아래 항목을 확인해야 한다.

1. Node request/reply promise 테스트가 timer 없이 완료되는지 확인한다.
2. Node callback request 테스트가 timer 없이 완료되는지 확인한다.
3. request timeout이 기존과 같은 result로 전달되는지 확인한다.
4. request 중 socket/spot close가 termination completion으로 정리되는지 확인한다.
5. pending request가 없는 상태에서 worker가 CPU를 쓰지 않는지 확인한다.
6. Go의 1ms ticker 제거 가능성을 별도 브랜치에서 확인한다.
7. Rust worker의 yield loop를 blocking wait로 바꿀 수 있는지 확인한다.

## 미해결 질문

1. 이 API를 공개 C API로 둘지, 바인딩 전용 헤더로 분리할지 결정해야 한다.
2. `zlink_spot_channel_reply_progress_from()`을 새 progress poller가 흡수할 수
   있는지 확인해야 한다.
3. poller wait의 종료 result를 `interrupted`로 볼지 `terminated`로 볼지 정해야 한다.
4. subject 중복 등록 시 `user_data` 교체가 충분한지, refCount를 core가 가질지
   결정해야 한다.
5. request progress poller가 기존 service poller 구현을 재사용할지, 더 작은
   전용 구현을 가질지 결정해야 한다.
