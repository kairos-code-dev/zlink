[스펙 목차](../README.ko.md)

# Draft -- SPOT Dispatch-Owned Channel Reply Stream

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `Spot`의 channel request reply를 `Spot`의 dispatch 실행 문맥 안으로
가져오는 방향을 정의한다.

이번 초안의 목표는 아래와 같다.

- `Spot`이 timer, routed receive, subscription receive, channel reply completion을
  **하나의 spot dispatch stream** 안에서 처리하게 한다.
- framework의 `ZLinkSpot`가 raw dealer receive를 직접 노출하지 않고도 channel
  reply를 안전하게 처리할 수 있게 한다.
- 다른 channel로 동시에 request를 여러 개 보내도, reply callback 실행은 같은
  `Spot` 기준으로 순차 처리되게 한다.
- channel request가 실제로는 attach된 `DEALER` 하나에 귀속된다는 기존 전송 의미는
  유지한다.
- callback 기반 request completion 표면이 남더라도, completion delivery owner는
  `Spot` dispatch stream으로 옮긴다.

이 문서의 핵심은 "reply transport owner"와 "reply callback delivery owner"를
분리하는 것이다.

- 전송 owner는 여전히 선택된 attached `DEALER`다.
- callback delivery owner는 request를 시작한 `Spot`이다.

즉 reply는 dealer 경로로 돌아오지만, 최종 callback 실행은 `Spot` dispatch stream이
맡는다.

## 2. 배경

현재 구현에서 `Spot`의 dispatch event는 아래 세 종류만 가진다.

- subscribe readable
- routed readable
- timer readable

반면 `zlink_spot_request_channel()` 계열의 reply completion은 이 dispatch plane에
들어오지 않는다. channel request는 제출 시 선택된 attached `DEALER` 또는
service router socket의 request/reply state에 귀속되고, reply completion도 그
socket completion 경로에서 처리된다.

이 구조는 low-level transport 관점에서는 자연스럽지만, framework 관점에서는 아래
문제를 만든다.

- `ZLinkSpot`는 routed, subscribe, timer는 `Spot` dispatch callback 하나로
  모을 수 있는데, channel reply만 다른 progress 경로를 따로 돌려야 한다.
- 같은 `ZLinkSpot` state를 만지는 callback이 둘 이상의 실행 경로에서 나올 수
  있다.
- binding이 별도 progress pump를 붙이지 않으면 channel reply completion이
  정체될 수 있다.
- framework가 public surface에서 raw dealer receive를 숨기고 싶어도, 내부적으로는
  dealer completion을 따로 추적해야 한다.

즉 지금 구조는 "channel request transport는 attach dealer에 귀속"이라는 규칙은
분명하지만, "그 reply를 어떤 실행 문맥에서 user callback으로 전달하는가"는
framework 친화적으로 닫혀 있지 않다.

## 3. 해결하려는 문제와 해결하지 않는 문제

### 3.1 해결하려는 문제

이 초안은 아래 문제를 풀려고 한다.

- `Spot` 기반 framework가 channel reply를 같은 spot execution context에서
  순차 처리하고 싶다.
- `Spot` dispatch callback 하나만으로 `Spot` 관련 work를 모으고 싶다.
- request completion을 위해 binding이 channel별 socket progress pump를 따로 두지
  않게 하고 싶다.
- framework public surface에서 raw dealer receive를 감추고 싶다.

### 3.2 해결하지 않는 문제

이 초안은 아래 문제를 직접 풀지 않는다.

- channel request의 load balancing 의미 재설계
- 특정 서버 직접 지정 channel API 추가
- `SpotNode.router`를 channel reply transport로 바꾸는 일
- raw `DEALER` 소켓 일반 receive 모델 재설계
- strict global timestamp order 보장

특히 이 초안은 "reply를 `SpotNode.router` 경로로 돌린다"는 설계가 아니다.
channel request/reply는 계속 선택된 attached `DEALER` 경로에 귀속된다.

## 4. 이 문서에서 쓰는 말

- **spot dispatch stream**
  `Spot` 하나를 기준으로 timer, routed receive, subscription receive,
  channel reply completion을 순차 처리하는 실행 축
- **transport owner**
  실제 request를 내보내고 network reply를 받는 attached `DEALER`
- **delivery owner**
  최종 user callback을 어떤 실행 문맥에서 호출할지 결정하는 owner
- **channel reply completion**
  network reply, timeout, terminate, local failure, protocol error처럼
  request를 정확히 한 번 완료시키는 항목
- **spot execution context**
  framework 또는 binding이 `Spot`별로 소유하는 단일 실행 문맥

이 초안에서는 channel reply의 transport owner와 delivery owner가 다를 수 있다고
본다.

## 5. 목표 동작

### 5.1 high-level 모델

목표 모델은 아래와 같다.

```text
+---------------------------+
| Spot dispatch stream      |
|---------------------------|
| subscribe readable        |
| routed readable           |
| channel reply readable    |
| timer readable            |
+---------------------------+
            |
            v
+---------------------------+
| Spot execution context    |
|---------------------------|
| framework handlers        |
| request callbacks         |
| task continuations        |
+---------------------------+

+---------------------------+
| Attached dealer transport |
|---------------------------|
| send request              |
| receive network reply     |
| decode completion         |
| bridge to dealer queue    |
+---------------------------+
```

핵심 규칙은 아래와 같다.

- channel request는 제출 시 하나의 attached `DEALER`를 선택한다.
- network reply는 그 `DEALER` 경로로만 돌아온다.
- reply가 pending request와 매칭되면, 그 completion은 바로 user callback을 치지
  않고 request를 시작한 `Spot` 안의 **그 dealer source queue**로 옮겨진다.
- `Spot`은 `CHANNEL_REPLY_READABLE` dispatch item을 통해 어느 dealer source가
  준비됐는지 본다.
- framework 또는 binding은 같은 `Spot` dispatch stream 안에서 completion을
  drain하고, 등록된 callback 또는 promise completion을 실행한다.

### 5.2 framework 관점의 의미

이 모델이 성립하면 `ZLinkSpot`는 아래처럼 동작할 수 있다.

- low-level `Spot` dispatch event callback이 온다.
- framework는 event 종류에 따라 timer, routed recv, subscribe recv,
  channel reply completion drain을 수행한다.
- 각각에서 꺼낸 payload 또는 completion을 `ZLinkSpot`에 등록된 handler로
  전달한다.
- 이 과정은 같은 spot execution context 안에서 한 번에 하나씩 실행된다.

즉 framework 사용자는 "reply도 결국 `ZLinkSpot`의 dispatch 처리 중 하나"라고
생각할 수 있다.

## 6. 설계 원칙

### 6.1 raw dealer receive를 public `Spot` 표면으로 올리지 않는다

이 초안은 `Spot` public contract에 raw dealer receive surface를 추가하지 않는다.

이유는 아래와 같다.

- channel request reply는 low-level transport에서는 dealer receive지만, public
  의미는 ordinary inbound message가 아니라 request completion이다.
- raw dealer message를 framework가 직접 다루게 하면 pending 매칭, timeout 취소,
  protocol decode를 framework가 떠안게 된다.
- framework는 "어떤 dealer에서 어떤 frame이 왔는가"보다 "어떤 `Spot` request가
  어떤 결과로 끝났는가"를 더 알고 싶다.

따라서 dispatch stream에 올릴 대상은 raw dealer readable이 아니라
**channel reply completion readable**이다.

### 6.2 transport affinity는 유지한다

channel request/reply의 transport affinity는 유지한다.

- request 제출 시 선택된 attached `DEALER`가 transport owner다.
- late reply, timeout, cancel 경합 규칙도 그 dealer pending state 기준으로
  관리한다.
- reply를 다시 `channel_name`으로 재탐색하지 않는다.

즉 이 초안은 transport 모델을 바꾸지 않는다. callback delivery owner만 바꾼다.

### 6.3 completion은 user callback 전에 한 번 더 spot queue를 거친다

channel reply completion은 dealer completion에서 바로 user callback 하지 않는다.

반드시 아래 단계를 거친다.

1. attached dealer pending request와 network reply를 매칭한다.
2. timeout 또는 close completion이면 그 결과를 확정한다.
3. completion payload를 decode한다.
4. user callback을 직접 호출하지 않고, originating `Spot` 안의 해당 dealer source
   queue에 넣는다.
5. 그 source를 가리키는 dispatch pending item을 세운다.
6. `Spot` dispatch stream이 그 source queue를 drain하면서 최종 callback을 실행한다.

이 추가 hop은 동기화 규칙을 단순하게 만들기 위해 필요하다.

## 7. 제안하는 core 동작

### 7.1 새 dispatch event 추가

`Spot` dispatch event에 새 종류를 추가한다.

```c
typedef enum zlink_spot_dispatch_event_t {
  ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
  ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE = 2,
  ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE = 3,
  ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4
} zlink_spot_dispatch_event_t;
```

이 event의 의미는 아래처럼 고정한다.

- request를 시작한 그 `Spot`에 대해,
- user callback을 실행할 준비가 끝난 channel reply completion이 하나 이상 있다.

이 event는 raw dealer frame 존재를 뜻하지 않는다.

### 7.2 dispatch runtime은 source-aware pending queue를 가져야 한다

`subject`를 callback에 넣으려면, dispatch 내부도 event-only bitmask로는 충분하지
않다.

이번 초안은 dispatch runtime이 아래 단위를 pending item으로 관리하는 쪽을
기본안으로 둔다.

```c
typedef struct zlink_spot_dispatch_key_t {
  zlink_spot_dispatch_event_t event;
  zlink_spot_dispatch_subject_kind_t subject_kind;
  void *subject;
} zlink_spot_dispatch_key_t;
```

핵심 규칙은 아래와 같다.

- pending item의 식별자는 `(event, subject_kind, subject)`다.
- 같은 key는 pending queue 안에 동시에 두 번 들어가지 않는다.
- source가 이미 pending이거나 callback 실행 중일 때 추가 work가 생기면, 그 source는
  한 번 더 rearm 대상으로만 표시한다.
- callback이 끝난 뒤에도 그 source에 아직 work가 남아 있으면 같은 key를 다시
  queue 뒤에 넣는다.
- 서로 다른 dealer source가 동시에 ready면 서로 다른 pending item으로 각각
  callback 된다.

즉 이 초안의 dispatch는 "event 종류 몇 개가 켜졌는가"보다
"어떤 source에서 어떤 work가 준비됐는가"를 직접 운반하는 모델이다.

이 규칙이 있어야 여러 attached dealer가 동시에 ready여도 framework가 callback의
`subject`만 보고 안정적으로 해당 source를 drain할 수 있다.

### 7.3 dispatch callback은 event와 source instance를 함께 받는다

이 초안은 `Spot` dispatch callback이 event만 받는 형태로는 충분하지 않다고 본다.

이유는 아래와 같다.

- attached channel dealer는 여러 개일 수 있다.
- spot-owned timer도 여러 개일 수 있다.
- `CHANNEL_REPLY_READABLE`, `TIMER_READABLE` 같은 event만으로는 어떤 source를
  drain해야 하는지 바로 알기 어렵다.

따라서 callback payload는 아래처럼 확장하는 쪽을 기본안으로 둔다.

```c
typedef enum zlink_spot_dispatch_subject_kind_t {
  ZLINK_SPOT_DISPATCH_SUBJECT_SPOT = 1,
  ZLINK_SPOT_DISPATCH_SUBJECT_TIMER = 2,
  ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3
} zlink_spot_dispatch_subject_kind_t;

typedef struct zlink_spot_dispatch_info_t {
  zlink_spot_dispatch_event_t event;
  zlink_spot_dispatch_subject_kind_t subject_kind;
  void *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_);
```

이 구조의 의미는 아래처럼 고정한다.

- `event`:
  어떤 종류의 work가 준비됐는가
- `subject_kind`:
  `subject`를 어떤 타입으로 해석해야 하는가
- `subject`:
  실제 drain 대상 인스턴스

예를 들면 아래처럼 쓸 수 있다.

- `ROUTED_READABLE` + `SUBJECT_SPOT`:
  `spot_` 또는 `subject`를 기준으로 routed recv drain
- `TIMER_READABLE` + `SUBJECT_TIMER`:
  해당 timer handle로 `zlink_timer_recv(...)` drain
- `CHANNEL_REPLY_READABLE` + `SUBJECT_CHANNEL_DEALER`:
  해당 attached dealer source에 귀속된 channel reply completion drain

즉 callback은 단순 notification이 아니라, "무엇을 지금 drain해야 하는가"를
직접 알려주는 형태가 된다.

### 7.4 channel name은 dispatch info에 싣지 않고 attached dealer metadata에서 읽는다

`channel_name`을 `zlink_spot_dispatch_info_t`에 직접 넣는 방식도 가능하지만, 이번
초안은 그보다 **source metadata 조회**를 기본안으로 둔다.

이유는 아래와 같다.

- 모든 dispatch source가 channel name을 가지는 것은 아니다.
  - `Spot` 자체
  - timer
- channel reply source는 attached dealer이므로, channel 정보는 dealer metadata에
  붙어 있는 편이 더 자연스럽다.
- event payload를 작게 유지할 수 있다.

따라서 attached channel dealer에는 read-only channel metadata를 둘 수 있다.
다만 이 metadata는 app이 임의로 setter로 바꾸는 값이 아니다.

- discovery attach인 경우:
  attach된 `Discovery.service_name` 또는 `channel_name`에서 유도한다.
- manual attach인 경우:
  `zlink_spot_node_attach_channel_dealer_manual(node, channel_name, dealer)`의
  `channel_name`이 고정 metadata가 된다.

즉 channel name의 source of truth는 socket 자체가 아니라 attach 관계다.
socket은 그 결과를 read-only metadata로 보유한다.

공개 조회 API는 generic socket getter보다 더 좁은 형태를 기본안으로 둔다.

```c
ZLINK_EXPORT int zlink_spot_channel_dealer_get_channel_name (
  void *dealer_,
  char *channel_name_buf_,
  size_t channel_name_capacity_,
  size_t *channel_name_len_out_);
```

이 함수의 의미는 아래처럼 둔다.

- attached channel dealer면 그 socket에 귀속된 channel name을 돌려준다.
- channel name이 없는 ordinary socket이면 `ENOENT`로 실패한다.
- setter API는 제공하지 않는다.

framework는 `subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER`일 때만
필요하면 이 getter를 호출하면 된다.

### 7.5 spot state는 channel reply를 source-aware queue로 들고 있어야 한다

`Spot` request/reply state에는 channel reply delivery를 위한 queue가 필요하다.

이번 초안은 channel reply를 `Spot` 하나의 공용 completion queue로 합치지 않는다.

대신 아래처럼 source-aware queue를 둔다.

- `Spot` direct request completion:
  기존 `Spot` request completion queue
- channel request completion:
  attached dealer source별 queue

예를 들면 내부 상태는 아래처럼 갈 수 있다.

- `spot_request_reply_state_t::completion`
  `Spot` direct request용
- `spot_request_reply_state_t::channel_reply_sources[dealer_handle]`
  attached dealer별 channel reply completion queue

이렇게 나누는 이유는 아래와 같다.

- dispatch callback의 `subject`가 실제 drain 대상을 가리켜야 한다.
- 여러 dealer source가 동시에 ready일 때 source별 callback을 안정적으로 만들 수
  있다.
- framework가 callback에서 받은 source만 처리하고 다음 source로 넘어가는 구조를
  단순하게 유지할 수 있다.

### 7.6 attached dealer completion과 spot completion을 연결하는 bridge가 필요하다

현재 channel request는 selected dealer socket의 request/reply state를 사용한다.
이 점은 유지하되, user callback은 바로 넘기지 않고 내부 bridge callback으로
바꾼다.

bridge는 아래 정보를 가진다.

- originating `Spot` weak reference 또는 owner key
- 원래 user callback 포인터
- 원래 user callback userdata
- request submit metadata

dealer completion이 발생하면 bridge는 아래를 수행한다.

- completion을 **그 dealer source queue** 형식으로 재포장한다.
- originating `Spot` state가 살아 있으면 `channel_reply_sources[dealer]`에
  적재한다.
- `(CHANNEL_REPLY_READABLE, CHANNEL_DEALER, dealer)` pending item을 세운다.
- `Spot` state가 이미 종료 중이면 completion을 조용히 폐기하거나 `ETERM`
  규칙에 맞게 정리한다.

이 bridge는 transport affinity를 깨지 않으면서 callback delivery owner만
`Spot`으로 옮기는 핵심 장치다.

### 7.7 callback에서 cast 후 바로 호출할 drain 대상은 raw recv가 아니라 source progress다

timer와 routed/subscription은 `recv`로 drain하는 모델이 자연스럽다. 하지만
channel reply는 ordinary data-plane receive가 아니라 request completion plane이다.

따라서 callback에서 `subject`를 dealer handle로 캐스팅하더라도, 그 다음 호출은 raw
`zlink_recv()`가 아니라 source-aware completion drain helper가 되어야 한다.

기본안은 아래와 같다.

```c
ZLINK_EXPORT int zlink_spot_channel_reply_progress_from (
  void *spot_,
  void *dealer_);
```

이 함수의 의미는 아래처럼 둔다.

- `spot_`에 귀속된 `dealer_` source queue를 drain한다.
- drain 중 해당 source에 적재된 request completion callback 또는 binding promise
  completion을 실행한다.
- `dealer_`가 그 `Spot`에 attach된 channel dealer가 아니면 `EINVAL` 또는 `ENOENT`
  로 실패한다.

즉 callback의 source instance는 "raw socket처럼 직접 recv하라"는 뜻이 아니라,
"이 source에 대응하는 drain 함수를 호출하라"는 뜻이다.

### 7.8 attached dealer completion progress는 `Spot` progress가 책임진다

이 기능의 핵심 요구 중 하나는 "binding이 attached dealer별 progress pump를 따로
돌리지 않아도 된다"는 점이다.

따라서 `Spot` progress는 아래 일을 함께 해야 한다.

- `Spot` direct request completion queue drain
- attached channel dealer들의 request completion signal 감시
- signal이 오면 dealer completion을 bridge 단계까지 진전시키기

즉 `zlink_spot_request_progress_internal(spot_)`는 기존 spot-owned completion만
drain하는 helper가 아니라, **이 `Spot`에 귀속된 attached dealer completion을
spot dispatch stream으로 끌어오는 progress point**가 되어야 한다.

필요하면 기존 `zlink_spot_request_channel_progress_internal(...)`는 아래 둘 중
하나로 정리한다.

- 더 좁은 내부 최적화 helper로 남긴다.
- `zlink_spot_request_progress_internal(...)` 의미에 흡수하고 점진적으로 사용을
  줄인다.

### 7.9 dispatch runtime도 attached dealer completion signal을 깨울 수 있어야 한다

`Spot` dispatch event를 runtime task로 구현하는 경우, wakeup source는 routed,
subscribe, timer만으로 끝나지 않는다.

새 모델에서는 아래 source도 고려해야 한다.

- attached dealer request completion signal socket

즉 dispatch runtime은 "spot receive queue가 readable인가"만 보는 모델이 아니라,
"이 `Spot`이 처리해야 할 work가 생겼는가"를 보는 모델로 넓혀야 한다.

## 8. 소유권과 사용 제한

### 8.1 attached channel dealer는 spot runtime 전용으로 본다

이 기능이 안전하게 동작하려면 attached channel dealer는 사실상 `SpotNode`
runtime이 소유하는 socket으로 다뤄야 한다.

아래 혼용은 기본적으로 지원 대상으로 두지 않는다.

- 같은 attached dealer를 app이 raw `zlink_recv()`로 직접 읽기
- 같은 attached dealer를 app이 별도 poller에 등록해 일반 socket처럼 다루기
- 같은 attached dealer로 app이 `zlink_dealer_request()`를 직접 섞어 쓰기

이런 혼용을 허용하면 `Spot` progress와 외부 사용자 progress가 같은 completion을
경합할 수 있다.

따라서 이 초안은 attached dealer에 대해 아래 규칙을 둔다.

- attach된 이후 그 socket은 `SpotNode` service transport로 간주한다.
- ordinary app-visible dealer semantics는 attach 이전 socket과 동일하지 않다.
- attach된 service transport는 `Spot` dispatch model 안에서 progress된다.

framework나 binding이 callback에서 source를 구분하기 위해 handle identity를 쓰는
것은 허용하지만, 그 handle을 ordinary public dealer처럼 사용하는 것은 별도 문제다.

즉 아래 둘은 구분해야 한다.

- source 식별:
  callback이 어느 attached dealer인지 알아내기 위해 handle을 본다.
- raw transport 사용:
  그 handle에 대해 ordinary recv/request/send를 직접 호출한다.

이번 초안은 첫 번째는 허용하지만, 두 번째는 기본 모델로 두지 않는다.

### 8.2 callback delivery owner와 transport owner를 문서로 분리한다

attached dealer의 일반 owner와 `Spot` delivery owner를 같은 말로 섞지 않는다.

문서에는 아래를 분명히 적어야 한다.

- transport owner:
  어떤 socket이 request를 내보내고 network reply를 받는가
- delivery owner:
  어떤 `Spot` execution context가 user callback을 실행하는가

이 구분이 있어야 "reply는 dealer로 오지만 callback은 spot에서 실행된다"는 모델이
혼란 없이 설명된다.

## 9. 순서와 동기화 규칙

### 9.1 핵심 보장

이 초안이 보장하려는 것은 **상호배제와 순차 실행**이다.

같은 `Spot`에 대해 아래 work는 동시에 user code를 실행하지 않는다.

- routed receive handler
- subscription handler
- timer handler
- channel reply callback

즉 같은 `Spot` mutable state는 spot execution context 안에서만 접근하면 된다.

### 9.2 보장하지 않는 것

이 초안은 아래를 보장하지 않는다.

- 서로 다른 plane 간의 strict wall-clock global order
- 여러 channel reply와 routed message 사이의 절대 timestamp order
- 장시간 handler가 없는 이상적인 fairness

dispatch event는 readiness/coalescing 모델이므로, 완전한 event log 순서를 뜻하지
않는다.

### 9.3 권장 dispatch 우선순위

새 event가 들어오면 dispatch 우선순위는 아래처럼 고정하는 쪽을 기본안으로 둔다.

1. subscribe readable
2. routed readable
3. channel reply readable
4. timer readable

이 순서를 제안하는 이유는 아래와 같다.

- 기존 `subscribe -> routed -> timer` 우선순위를 크게 깨지 않는다.
- channel reply를 timer 뒤로 미루지 않아 request latency가 과도하게 늘지 않는다.
- ordinary inbound routed work를 자기 자신이 보낸 outbound completion보다 먼저
  다루는 현재 성격을 유지할 수 있다.

다만 이 우선순위는 구현 단계에서 다시 점검할 수 있다. 중요한 것은 **문서에 고정된
우선순위가 있어야 한다**는 점이다.

### 9.4 plane 내부 순서

같은 plane 내부에서는 아래 순서를 따른다.

- routed receive:
  기존 recv queue enqueue 순서
- subscription receive:
  subscription queue enqueue 순서
- channel reply completion:
  같은 dealer source queue 안에서는 enqueue 순서
- timer:
  기존 timer fired count 규칙

즉 reply도 queue에 들어온 순서대로 callback 된다.

## 10. completion 규칙

### 10.1 정확히 한 번 완료

accepted channel request는 아래 결과 중 하나로 정확히 한 번 완료된다.

- 정상 reply
- timeout
- terminate
- local failure
- protocol error

이 completion은 dealer pending state에서 먼저 확정되고, 그 뒤 해당 dealer source
queue로 bridge된다.

### 10.2 late reply

timeout 또는 close로 이미 완료된 request에 late reply가 오면, 그 reply는
user-visible completion을 다시 만들지 않는다.

즉 기존 request/reply의 single-completion 규칙은 유지된다.

### 10.3 close와 shutdown

`Spot` 또는 owning `SpotNode`가 종료 중이면 아래 규칙을 따른다.

- 아직 final delivery되지 않은 `Spot` direct completion과 dealer source queue
  completion은 `ETERM` 또는 종료 규칙에 맞게 정리한다.
- attached dealer에서 뒤늦게 올라온 completion bridge는 dead `Spot` owner를 다시
  깨우지 않는다.
- close epilogue와 late completion 경합에서도 double completion이 없어야 한다.

## 11. framework 구현 지침

### 11.1 `ZLinkSpot` public surface는 packet handler 중심으로 둔다

framework 문서는 여전히 아래 방향을 따라야 한다.

- app은 raw recv를 직접 하지 않는다.
- mapped packet handler 또는 request handler만 등록한다.
- timer도 timer callback 등록으로 쓴다.
- channel request reply도 `Task`, callback, 또는 framework-level response handler로
  본다.

즉 framework 문서에는 low-level attached dealer가 드러나지 않아야 한다.

### 11.2 framework 내부 dispatch 루프

framework 내부에서는 low-level dispatch event를 받으면 아래를 수행할 수 있다.

- `SUBSCRIBE_READABLE`:
  subscription event를 drain하고 mapped subscription handler 호출
- `ROUTED_READABLE`:
  routed packet을 drain하고 packet/request handler 호출
- `CHANNEL_REPLY_READABLE`:
  callback이 준 `subject` dealer에 대해
  `zlink_spot_channel_reply_progress_from(spot, subject)`를 호출하고,
  그 source queue의 pending request callback 또는 `Task` completion 실행
- `TIMER_READABLE`:
  timer fire를 drain하고 timer handler 호출

이 동작은 모두 같은 spot executor에서 실행해야 한다.

### 11.3 async binding 규칙

`Task` 또는 coroutine binding은 아래 규칙을 따르는 쪽이 맞다.

- low-level completion callback이 직접 arbitrary thread에서 promise를 완료하지
  않는다.
- spot executor 안에서 promise를 완료한다.
- 사용자가 명시적으로 executor를 벗어나지 않는 한, continuation도 같은 spot
  execution context를 유지하게 한다.

이렇게 해야 framework 사용자는 "request 후 continuation도 spot state와 같은
동기화 규칙을 따른다"고 이해할 수 있다.

## 12. 구현 단계 제안

### 12.1 1단계

- `SpotDispatchEvent`에 `CHANNEL_REPLY_READABLE` 추가
- attached dealer completion을 spot-owned queue로 bridge하는 내부 구조 추가
- `zlink_spot_request_progress_internal(...)`가 attached dealer bridge progress를
  함께 처리하게 수정

### 12.2 2단계

- spot dispatch runtime이 attached dealer completion signal을 wakeup source로 삼게
  수정
- dispatch callback serialization 테스트에 channel reply completion 추가

### 12.3 3단계

- binding progress pump를 `Spot` 중심으로 단순화
- `.NET` `ZLinkSpot` 또는 대응 binding runtime이 channel reply를 같은 spot
  executor에서 처리하게 정리
- framework draft 문서를 user-facing handler model 기준으로 반영

## 13. 테스트 요구사항

### 13.1 core contract 테스트

아래 테스트가 필요하다.

- `Spot` dispatch callback 안에서 timer, routed, subscribe, channel reply가
  동시에 들어와도 user callback 실행은 한 번에 하나인지
- 같은 `Spot`이 여러 channel로 request를 보내고 reply가 섞여 와도 completion이
  각 dealer source queue에서 정확히 한 번씩 처리되는지
- timeout과 reply 경합에서 double completion이 없는지
- close 중 late reply가 와도 callback이 다시 실행되지 않는지
- malformed reply가 `PROTOCOL_ERROR` completion으로 한 번만 올라오는지

### 13.2 ownership 테스트

아래 제약도 확인해야 한다.

- attach된 dealer를 외부 recv path와 섞어 쓰면 어떻게 실패하는지
- `Spot` progress만으로 channel reply completion이 전진하는지
- attached dealer별 별도 progress pump 없이도 completion이 delivery되는지

### 13.3 binding 테스트

binding 수준에서는 아래를 본다.

- `Spot.RequestChannelAsync(...)`가 별도 dealer progress pump 없이 완료되는지
- dispatch callback, timer callback, routed handler, request continuation이 같은
  synchronization context 또는 executor에서 실행되는지
- 여러 reply completion이 들어와도 callback이 순차 실행되는지

### 13.4 회귀 테스트

이번 변경은 새 기능 테스트만으로 충분하지 않다. 기존 `Spot`, attached dealer,
request/reply, timer 동작이 그대로 유지되는지도 함께 봐야 한다.

아래 회귀 테스트가 필요하다.

- 기존 `SUBSCRIBE_READABLE`, `ROUTED_READABLE`, `TIMER_READABLE`만 쓰는 `Spot`
  dispatch callback 코드가 channel reply 기능 추가 후에도 같은 순서 규칙으로
  동작하는지
- channel reply가 전혀 없는 `Spot` 사용 모델에서 dispatch callback 횟수나 기존
  recv drain 동작이 바뀌지 않는지
- `Spot` direct request/reply 경로가 기존처럼 정확히 한 번 완료되는지
- `router -> spot` request/reply 경로가 기존처럼 정확히 한 번 완료되는지
- attached dealer가 하나뿐인 기존 channel request/reply 사용 모델에서 결과 코드,
  timeout, late reply 규칙이 바뀌지 않는지
- attached dealer가 없는 channel request가 기존과 같은 실패 규칙을 유지하는지
- 같은 channel에 대한 attach 중복 금지, 잘못된 socket 타입 거부, 잘못된
  `channel_name` 거부 같은 attach 검증 규칙이 바뀌지 않는지
- timer만 active인 `Spot`에서 timer dispatch와 `zlink_timer_recv(...)` 동작이
  그대로 유지되는지
- routed recv만 active인 `Spot`에서 `subject_kind == SPOT` 경로가 기존 recv 모델을
  깨지 않는지
- subscribe recv만 active인 `Spot`에서 기존 topic delivery와 drain 규칙이 바뀌지
  않는지
- close 중 dispatch pending item이 남아 있어도 기존 shutdown 경로와 충돌하지
  않는지
- attached dealer monitor, discovery attach, service attachment cache 갱신이 기존
  topology 관리 동작을 깨지 않는지
- poller 또는 progress loop가 없는 경우 completion이 정체되는 기존 progress-owned
  completion 의미가 유지되는지
- `.NET` binding에서 channel reply 기능을 켜도 기존 `OnDispatchEvent`,
  `OnRoutedReceive`, `Timer.OnFire`, `RequestChannelAsync(...)` public surface가
  깨지지 않는지

## 14. 문서 반영 대상

구현이 끝나면 아래 문서에 나누어 반영한다.

- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- 필요하면 polling 또는 request/reply 관련 정식 spec
- framework binding draft와 sample 문서

정식 문서에는 아래가 특히 분명하게 적혀야 한다.

- `Spot` dispatch event에 channel reply completion이 포함되는지
- channel request reply transport owner와 delivery owner가 어떻게 다른지
- framework 사용자가 raw dealer를 알 필요가 없는 이유

## 15. 최종 판단

이번 기능은 단순 binding 보완으로 닫기 어렵다.

핵심 이유는 아래 두 가지다.

- 현재 channel request reply는 attached dealer socket completion plane에 귀속돼 있다.
- 현재 `Spot` dispatch event plane은 그 completion을 직접 표현하지 못한다.

따라서 "reply까지 포함한 단일 `Spot` dispatch stream"을 진짜로 만들려면, core가
attached dealer completion을 `Spot` delivery owner로 bridge하는 기능을 가져야
한다.

이 초안은 그 변경을 "raw dealer readable 노출"이 아니라
"channel reply completion readable 추가"로 푸는 방향을 기본안으로 둔다.
