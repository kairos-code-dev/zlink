[스펙 목차](../README.ko.md)

# Draft -- Request Progress-Owned Completion

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `request` 계열 API의 reply completion을 어떤 스레드에서 실행할지 다시
정리한다.

핵심 목표는 아래와 같다.

- `request(..., handler, ...)` 표면은 유지한다.
- 공개 `reply_recv()` API는 추가하지 않는다.
- reply completion callback을 I/O thread가 아니라 **호출자가 소유한 progress
  thread**에서 실행할 수 있게 한다.
- `recv + poller` 중심 루프를 쓰는 애플리케이션이 request/reply도 같은 루프 안에서
  처리할 수 있게 한다.
- 바인딩 라이브러리, 특히 coroutine 바인딩이 불필요한 스레드 hop 없이 completion을
  이어 받을 수 있게 한다.

이 문서에서 말하는 progress thread는 아래 중 하나를 뜻한다.

- 같은 handle에 대해 `recv()`를 호출하는 스레드
- 같은 handle을 포함한 poller loop를 돌리며 내부 progress를 함께 처리하는 스레드

즉 이 초안의 방향은 "callback 표면은 유지하되, callback 실행 위치를 owner loop로
옮긴다"에 가깝다.

## 2. 배경

현재 request sender 쪽 공개 표면은 callback completion 모델이다.

- `zlink_dealer_request(...)`
- `zlink_router_request(...)`
- `zlink_spot_request_* (...)`

이 모델은 표면 자체는 단순하지만, 실행 스레드가 I/O thread로 고정되면 아래 문제가
생긴다.

- 애플리케이션이 `recv + poller` 루프로 로직을 단일 스레드에서 처리하고 싶어도,
  reply completion만 별도 I/O thread에서 튀어나온다.
- 같은 handle의 일반 수신 메시지와 request completion이 서로 다른 스레드에서
  관찰되어, 사용자 코드가 락이나 별도 queue를 추가로 가져가야 한다.
- binding이 coroutine이나 promise 모델을 만들 때도, I/O thread callback을 다시
  사용자 loop thread로 hop 시켜야 할 수 있다.

이 문제는 특히 아래 사용 모델에서 크게 보인다.

1. 서버 또는 agent가 하나의 owner loop를 갖는다.
2. ordinary message는 `poller + recv`로 같은 loop에서 처리한다.
3. 같은 handle로 `request()`도 보낸다.
4. reply completion이 owner loop가 아니라 I/O thread에서 바로 호출된다.

이 경우 request/reply만 다른 동기화 규칙을 갖게 된다.

## 2.1 이 문서에서 쓰는 말

이 초안은 아래 표현을 반복해서 사용한다.

- **accepted request**:
  `request()` 호출이 즉시 실패하지 않고 submit이 받아들여져, 이후 reply 또는 실패
  completion을 정확히 한 번 받아야 하는 상태에 들어간 요청
- **owner progress thread**:
  같은 handle에 대해 `recv()`, poller progress, 또는 binding progress pump를 실제로
  돌려 completion을 전진시키는 스레드
- **completion queue**:
  정상 reply, timeout, terminate, local failure 같은 request 완료 항목을 user
  callback 대신 먼저 적재하는 내부 대기열

이 초안에서 "callback 실행 스레드"라고 하면 특별한 언급이 없는 한
`owner progress thread`를 뜻한다.

이 초안은 같은 handle에 대해 여러 사용자 스레드가 동시에 progress owner가 되는
모델은 기본 사용 모델로 다루지 않는다. owner는 하나의 event loop 또는 하나의
조정 지점으로 보는 것이 전제다.

## 3. 해결하려는 문제와 해결하지 않는 문제

### 3.1 해결하려는 문제

이 초안은 아래 문제를 풀려고 한다.

- request completion callback의 실행 스레드를 호출자가 예측하기 어렵다.
- same-handle 로직을 한 스레드에서 처리하고 싶은데 request completion 때문에
  스레드 경합이 생긴다.
- coroutine binding이 completion을 다시 scheduler thread로 넘겨야 한다.

### 3.2 해결하지 않는 문제

이 초안은 아래 문제를 직접 풀지 않는다.

- `send_ready_handler`와 `ZLINK_POLLOUT`의 의미 재정의
- data-plane receive callback 전반의 정리
- 같은 handle을 여러 사용자 스레드가 동시에 소유하는 모델

특히 `send_ready`는 이 문서의 범위가 아니다. `send_ready`는 기존처럼
`ZLINK_POLLOUT`과 같은 readiness 축으로 다루고, owner thread가 필요하면
callback 대신 poller 경로를 쓰는 방향을 전제로 둔다.

## 4. 설계 원칙

### 4.1 공개 표면은 유지하고 실행 위치만 바꾼다

이 초안은 request sender 쪽 API를 callback 기반으로 유지한다.

즉 아래 방향을 기준으로 둔다.

- `request(..., handler, userdata)` 시그니처는 유지한다.
- 공개 `reply_recv()`는 추가하지 않는다.
- 대신 내부적으로는 completion queue를 두고, progress point에서 callback을
  실행한다.

### 4.2 progress 없이는 completion도 없다

이 초안에서 가장 중요한 규칙은 아래다.

- accepted request는 **반드시 정확히 한 번** 완료된다.
- 그러나 그 완료 callback은 handle의 progress가 진행될 때 실행된다.

즉 request sender가 completion을 받으려면, 같은 handle에 대해 아래 중 하나가
계속 돌아야 한다.

- `recv()` 호출
- poller wait와 내부 progress drain
- binding이 제공하는 동일 의미의 progress pump

이 문서는 이를 `progress-owned completion`이라고 부른다.

### 4.3 ordinary receive와 async completion을 같은 owner loop에서 본다

ordinary inbound message와 request completion은 성격은 다르지만, owner loop
입장에서는 둘 다 "이 handle에서 처리해야 할 일"이다.

따라서 공개 계약은 아래 방향을 따른다.

- ordinary receive는 기존 recv queue를 통해 꺼낸다.
- request completion은 별도 completion queue를 통해 owner loop가 본다.
- 둘 다 같은 progress thread에서 소비된다.

즉 사용자는 "data는 recv에서, request reply는 callback에서"라는 표면 차이는 보되,
실행 스레드는 하나로 맞출 수 있어야 한다.

### 4.4 timeout, terminate, local error도 같은 규칙을 따른다

정상 reply만 owner loop로 보내고, timeout이나 terminate는 별도 scheduler
thread에서 바로 callback을 치면 same-thread 보장이 깨진다.

따라서 아래 completion도 모두 같은 queue 규칙을 따라야 한다.

- 정상 reply
- timeout
- close/terminate로 인한 실패 완료
- local route failure나 protocol failure로 확정된 실패 완료

## 5. 새 모델 요약

이 초안이 목표로 하는 request completion 모델은 아래와 같다.

1. 호출자가 `request(..., handler, userdata)`를 호출한다.
2. core는 pending request를 등록한다.
3. reply 또는 failure completion이 생기면 user callback을 바로 실행하지 않는다.
4. 대신 per-handle completion queue에 완료 항목을 적재한다.
5. handle의 progress point가 queue를 drain한다.
6. drain 중에 user callback을 실행한다.

핵심은 "callback이 없어지는 것"이 아니라 "callback 실행 주체가 바뀌는 것"이다.

## 6. progress point 정의

이 초안은 progress point를 아래처럼 정의한다.

### 6.1 recv 기반 progress

`recv()` 계열 호출은 ordinary inbound message를 기다리기 전에, 또는 기다리는 동안,
같은 handle의 request completion queue도 함께 진전시켜야 한다.

이 규칙은 아래 의미를 가진다.

- reply가 이미 도착해 completion queue에 들어가 있다면, `recv()`는 그 completion을
  먼저 또는 함께 처리할 수 있다.
- ordinary message가 없더라도 completion queue 때문에 `recv()`가 깨어날 수 있어야
  한다.

### 6.2 poller 기반 progress

poller loop를 쓰는 경우에도 owner thread가 request completion을 놓치지 않아야 한다.

이 초안은 아래 둘 중 하나를 허용하는 방향으로 둔다.

- poller wait 이전/이후에 hidden completion queue를 확인하는 내부 progress step을 둔다
- 또는 completion queue를 poller가 관찰 가능한 내부 wakeup source와 연결한다

공개 계약의 핵심은 구현 방식이 아니라 결과다.

- owner poller loop가 돌고 있으면 request completion이 정체되지 않아야 한다.

### 6.3 binding progress pump

binding이 core 위에 별도 event loop를 만들었다면, 그 binding이 제공하는 progress
함수도 같은 의미를 가져야 한다.

예를 들면 아래와 같은 wrapper가 여기에 해당한다.

- coroutine scheduler가 주기적으로 호출하는 progress pump
- runtime loop와 연동된 binding 전용 wait 함수

## 7. callback 실행 규칙 초안

### 7.1 기본 보장

이 초안에서 request completion callback은 아래를 보장한다.

- 같은 handle의 progress를 수행한 스레드에서 실행된다.
- 같은 accepted request에 대해 정확히 한 번만 실행된다.
- normal reply, timeout, terminate, local failure 모두 같은 실행 규칙을 따른다.
- callback에 전달되는 `parts`의 소유권과 lifetime 규칙은 기존
  `zlink_reply_handler_fn` 계약을 유지한다. 즉 성공 reply의 `parts`는 callback으로
  이전되고, timeout/terminate 같은 실패 completion에서는 `parts == NULL`일 수
  있다.

### 7.2 reentrant 규칙

callback이 이제 I/O thread가 아니라 `recv()` 또는 progress pump 호출 스택 안에서
실행되므로, 재진입 규칙을 분명히 해야 한다.

이 초안은 보수적인 규칙을 기본안으로 둔다.

- 같은 handle에 대한 recursive `recv()`는 금지한다.
- 같은 handle에 대한 mode change는 callback 안에서 금지하거나 `EBUSY`로 막는다.
- 같은 handle에 대한 close는 callback epilogue 이후로 미루거나 `EBUSY`로 막는다.

즉 앞 절의 close owner-thread 규칙은 callback 바깥의 일반 close 경로를 말한다.
callback 안에서의 close는 owner thread라고 해도 별도 재진입 제한을 그대로 받는다.

반면 아래 동작은 허용 가능하다.

- callback 안에서 같은 handle의 `send`, `publish`, `reply` 재시도
- callback 안에서 사용자 큐에 작업을 적재

### 7.3 callback 순서

한 handle에서 ordinary inbound message와 request completion이 동시에 pending일 수
있다.

이 초안은 아래 방향을 기본안으로 둔다.

- owner progress step은 먼저 completion queue를 drain한다.
- 그 다음 ordinary recv를 진행한다.

이 순서를 두는 이유는 request sender가 이미 accepted된 async operation completion을
불필요하게 오래 지연하지 않게 하기 위해서다.

다만 starvation을 피하려면 구현에는 budget이 필요할 수 있다.

- completion만 무한히 몰릴 때 ordinary receive가 굶지 않아야 한다.
- ordinary receive만 몰릴 때 timeout completion이 과도하게 늦어지지 않아야 한다.

즉 공개 계약은 "같은 owner loop에서 처리된다"에 초점을 두고, 세부 drain budget은
구현 선택으로 남길 수 있다.

## 8. completion source별 계약

### 8.1 정상 reply

정상 reply는 즉시 callback 하지 않고 completion queue에 적재한다.

사용자 callback이 받는 인자는 기존 request completion 계약과 같은 의미를 유지한다.

- `result`
- reply `parts`
- `userdata`

특히 reply payload `parts`의 소유권은 기존과 같이 callback으로 이전된다고 본다.
즉 이 초안은 callback 실행 thread만 바꾸고, payload ownership 계약은 바꾸지
않는다.

### 8.2 timeout

timeout scheduler는 더 이상 user callback을 직접 실행하지 않는다.

대신 아래 순서를 따른다.

1. pending request를 timeout 상태로 확정한다.
2. timeout completion record를 queue에 넣는다.
3. owner progress가 이를 drain하면서 callback을 실행한다.

### 8.3 terminate / close

handle close나 context terminate로 pending request를 완료시켜야 할 때도 같은 원칙을
따른다.

- direct callback으로 바로 완료시키지 않는다.
- pending request는 `TERMINATED` completion으로 정리한다.
- 이 completion도 owner progress thread에서 실행한다.

현재 정식 spec의 `ZLINK_REQUEST_TERMINATED`는 아직 예약값으로 적혀 있다. 이 초안이
구현될 때는 그 예약값이 실제 공개 completion bucket으로 승격되어야 한다.

여기서 `request`는 plain `send`와 같은 close 의미를 따르지 않는다는 점을 먼저
분명히 해야 한다.

- plain `send`는 fire-and-forget submit이다.
- accepted된 `send`는 close 시점에 send queue에 남아 있으면 `LINGER` 규칙에 따라
  flush되거나 폐기될 수 있다.
- 반면 accepted된 `request`는 async operation이다.
- 따라서 close 시 payload가 실제 wire로 나갔는지와 별개로, requester에게는
  completion이 정확히 한 번 와야 한다.

즉 이 초안은 아래를 구분한다.

- **request payload**: 내부 전송 경로라는 점에서는 send queue와 같은 flush/discard
  영향을 받을 수 있다.
- **request operation**: 공개 계약 차원에서는 close 시 silent discard 될 수 없고,
  반드시 `TERMINATED` completion으로 정리돼야 한다.

이 초안은 close 규칙을 아래처럼 고정하는 방향을 기본안으로 둔다.

1. `close()`가 accepted되면 handle은 먼저 closing 상태로 전이한다.
2. closing 상태가 된 뒤 새 API 진입은 `ESHUTDOWN`으로 막는다.
3. pending request는 lock 안에서 pending map에서 제거하고, 각 request를
   `TERMINATED` completion record로 바꾼다.
4. late reply나 late timeout은 이미 제거된 pending request를 다시 완료시키지
   못해야 한다.

그리고 pending request가 남아 있는 close는 아래처럼 제한한다.

- pending request가 없으면 close는 일반 close 규칙대로 진행할 수 있다.
- pending request가 있으면 owner progress thread만 close를 accepted할 수 있다.
- owner가 아닌 스레드가 pending request가 있는 handle을 닫으려 하면 `EBUSY`로
  실패한다.

owner progress thread가 close를 accepted한 경우 close epilogue는 아래 순서를 따른다.

1. pending request를 `TERMINATED` completion으로 snapshot 한다.
2. 같은 owner thread에서 그 completion queue를 마지막으로 drain한다.
3. callback이 정확히 한 번씩 실행된 뒤 destroy를 마무리한다.

이 규칙을 두는 이유는 아래와 같다.

- close caller thread가 임의로 callback을 치지 않게 한다.
- same-thread callback 보장을 유지한다.
- pending request를 silent drop 하지 않게 한다.
- late reply / timeout / close completion의 중복 완료를 막기 쉽다.

### 8.4 local immediate failure

API 호출 자체가 즉시 실패한 경우는 기존 규칙을 유지한다.

- request submit이 accepted되지 않았으면 completion callback은 오지 않는다.

이 초안이 queue로 옮기는 대상은 "accepted된 뒤 나중에 완료되는 작업"에 한정한다.

## 9. 공개 API 방향

### 9.1 유지되는 표면

이 초안은 아래 공개 표면을 유지한다.

```c
typedef void (*zlink_reply_handler_fn)(
  zlink_request_result_t result,
  zlink_msg_t *parts,
  size_t count,
  void *userdata);
```

그리고 request API도 기존 모양을 유지한다.

- `zlink_dealer_request*`
- `zlink_router_request*`
- `zlink_spot_request*`

### 9.2 추가하지 않는 표면

이 초안은 아래 공개 표면을 추가하지 않는다.

- `zlink_*_reply_recv(...)`
- `zlink_*_request_poll(...)`

이 초안의 판단은 아래와 같다.

- 공개 `reply_recv`를 추가하면 표면이 늘어난다.
- binding이 두 모델을 모두 감싸야 해서 오히려 복잡해질 수 있다.
- owner-thread 보장은 공개 API 추가 없이도 내부 completion queue로 달성 가능하다.

### 9.3 기본 동작 전환

이 초안은 이 모델을 opt-in이 아니라 **기본 동작**으로 두는 방향을 기준으로 둔다.

즉 request completion callback의 실행 thread 계약은 아래처럼 바뀐다.

- 기존: I/O thread에서 callback이 올 수 있다.
- 새 계약: owner progress thread에서 callback이 온다.

이 전환은 관찰 가능한 동작 변경이므로 아래 반영이 함께 필요하다.

- `core` 정식 spec 수정
- 바인딩 spec 수정
- thread-safety / polling 문서 수정
- 릴리즈 노트 또는 migration note 추가

핵심은 "callback 표면은 유지하지만 실행 위치는 기본적으로 owner thread"라는 점이다.

## 10. 바인딩 라이브러리 영향

### 10.1 일반 바인딩

일반 binding은 표면 자체는 크게 바뀌지 않는다.

- request 함수 시그니처 유지
- completion callback 시그니처 유지
- 성공 reply payload ownership 규칙 유지

다만 binding이 completion 스레드에 대해 문서화한 내용은 바뀔 수 있다.

### 10.2 coroutine 바인딩

coroutine binding에는 오히려 이 모델이 잘 맞는다.

아래 같은 패턴이 쉬워진다.

1. binding이 request를 보낸다.
2. awaiter를 pending map에 등록한다.
3. owner loop의 progress step이 completion queue를 drain한다.
4. 같은 owner thread에서 coroutine을 resume한다.

이 모델의 장점은 아래와 같다.

- I/O thread에서 scheduler thread로 다시 hop 하지 않아도 된다.
- ordinary recv와 async completion이 같은 executor에서 이어진다.
- "single-thread event loop + coroutine" 구조를 더 자연스럽게 만들 수 있다.

### 10.3 운영 규칙

binding은 아래 규칙을 사용자에게 분명히 알려야 한다.

- request completion은 progress가 있을 때만 전달된다.
- owner loop가 멈추면 ordinary receive와 completion이 함께 멈춘다.
- request-only 사용 코드도 progress pump 없이는 완료를 받지 못한다.

즉 coroutine binding에서도 "await만 걸면 배경 thread가 알아서 완료시킨다"는 계약은
기본이 아니다.

## 11. 운영상 장점과 주의점

### 11.1 장점

- 같은 handle의 ordinary receive와 request completion을 하나의 owner thread에서
  처리할 수 있다.
- 불필요한 락, cross-thread queue, scheduler hop을 줄일 수 있다.
- coroutine binding과 event-loop binding에 잘 맞는다.

### 11.2 주의점

- progress가 없으면 completion도 없다.
- request-only handle은 별도 progress pump가 필요하다.
- callback inside progress stack이므로 재진입 규칙을 엄격히 관리해야 한다.
- 기존 "callback은 I/O thread" 가정과 충돌하는 코드와 문서를 함께 정리해야 한다.

## 12. 비목표

이 초안은 아래를 목표로 하지 않는다.

- `send_ready_handler`까지 같은 규칙으로 owner thread callback으로 바꾸기
- request reply를 공개 recv API로 노출하기
- multi-thread shared ownership을 기본 사용 모델로 장려하기

특히 `send_ready`는 기존처럼 `ZLINK_POLLOUT`과 같은 readiness 축으로 두고,
single-thread owner loop가 필요하면 callback 대신 poller 경로를 쓰는 방향을
유지한다.

## 13. 정식 spec 분할 반영 계획

구현이 확정되면 이 초안의 내용을 한 문서에 그대로 올리지 않고, 성격에 맞게
나누어 반영해야 한다.

### 13.1 socket 공통 문서

반영 대상:

- `doc/spec/core/socket/README.ko.md`
- 필요하면 대응 영문 문서 `doc/spec/core/socket/README.md`

여기에는 공통 계약만 넣는다.

- `zlink_reply_handler_fn`의 의미
- request completion이 data-plane receive와 다른 축이라는 점
- request completion callback의 실행 thread가 "I/O thread"가 아니라
  "progress owner thread"가 될 수 있다는 새 규칙
- accepted request는 progress가 있을 때 완료된다는 규칙
- 같은 handle에서 ordinary recv와 request completion이 같은 owner loop에서
  관찰된다는 설명
- callback inside progress stack에서의 재진입 금지 규칙 요약

즉 socket 공통 문서에는 개별 소켓 타입 설명이 아니라, request completion의
공통 실행 모델만 넣는다.

### 13.2 DEALER 정식 spec

반영 대상:

- `doc/spec/core/socket/dealer.ko.md`
- 필요하면 대응 영문 문서 `doc/spec/core/socket/dealer.md`

여기에는 `zlink_dealer_request()` 전용 설명을 넣는다.

- request submit accepted 뒤 completion callback이 owner progress에 귀속된다는 점
- same-handle `recv()` 또는 poller progress가 completion 전달을 전진시킨다는 점
- request-only 사용 코드라면 별도 progress pump가 필요하다는 점
- timeout completion도 같은 경로로 전달된다는 점

기존 문서의 "reply arrives or timeout expires" 설명은 유지하되, "언제 어떤 스레드에서
실행되는가"를 이 초안에 맞게 더 분명히 적어야 한다.

### 13.3 ROUTER 정식 spec

반영 대상:

- `doc/spec/core/socket/router.ko.md`
- 필요하면 대응 영문 문서 `doc/spec/core/socket/router.md`

여기에는 아래 구분을 더 선명하게 반영한다.

- `zlink_router_recv()`는 data-plane routed receive
- `zlink_router_request()`의 reply completion은 async operation completion
- 두 표면은 의미가 다르지만 owner thread는 같을 수 있다는 점
- `zlink_router_recv()` 경로가 request completion progress point가 될 수 있다는 점

즉 ROUTER 문서에는 "recv 전용 inbound"와 "request completion"의 역할 차이와,
둘이 같은 owner loop에서 만나는 규칙을 함께 적는다.

### 13.4 SPOT 정식 spec

반영 대상:

- `doc/spec/core/service/spot.ko.md`
- 필요하면 대응 영문 문서 `doc/spec/core/service/spot.md`

SPOT 문서에는 socket 계층과 다른 service-aware 맥락을 적어야 한다.

- `zlink_spot_request_channel()` completion 규칙
- `zlink_router_request_spot()` completion 규칙
- `zlink_spot_recv()`가 routed receive plane을 읽는 함수이면서, 같은 owner loop가
  request completion도 진전시킬 수 있다는 점
- SPOT request completion도 ordinary routed recv와 동일 owner thread에서
  소비될 수 있다는 점
- local delivery failure, timeout, terminate completion도 같은 queue 규칙을
  따른다는 점

SPOT은 service-aware routed receive, subscribe receive, dispatch event가 함께
있으므로, 이 문서에서는 "어떤 progress point가 request completion을 전진시키는가"를
더 조심해서 써야 한다.

### 13.5 polling 정식 spec

반영 대상:

- `doc/spec/core/polling.ko.md`
- 필요하면 대응 영문 문서 `doc/spec/core/polling.md`

polling 문서에는 공개 poller API가 request completion과 어떤 관계를 갖는지 적어야
한다.

- owner poller loop가 돌고 있으면 request completion이 정체되지 않아야 한다는 규칙
- 이를 위해 poller wait 전후의 내부 progress step 또는 내부 wakeup source가
  필요하다는 점
- 이 동작이 ordinary `POLLIN`/`POLLOUT` 의미를 바꾸는 것은 아니라는 점

즉 polling 문서는 request completion 자체를 새 poller event로 노출하는 문서가
아니라, owner poller loop가 completion progress point가 된다는 관계를 설명하는
문서가 된다.

### 13.6 errno / result 문서

반영 대상:

- `doc/spec/core/errno-map.ko.md`
- 필요하면 대응 영문 문서 `doc/spec/core/errno-map.md`

여기에는 result 값 자체보다 completion 전달 시점과 source를 함께 정리해야 한다.

- `zlink_request_result_t`가 정상 reply, timeout, terminate, protocol error를
  어떤 public bucket으로 내보내는지
- 이 결과들이 모두 owner progress를 통해 callback으로 전달된다는 점
- `ETERM` 같은 종료 completion이 예약값이 아니라 실제 공개 completion이 되면
  그 시점부터 문서와 구현을 함께 올려야 한다는 점

### 13.7 바인딩 spec

반영 대상:

- `doc/spec/bindings/README.md`
- 각 언어별 바인딩 spec 문서

바인딩 문서에는 아래 내용을 적어야 한다.

- request completion callback 또는 future/promise/coroutine resume가 어느 thread에서
  일어나는지
- 그 thread가 background I/O thread가 아니라 owner progress thread일 수 있다는 점
- progress가 멈추면 await도 완료되지 않는다는 점

특히 coroutine binding은 이 초안의 수혜를 크게 보므로, scheduler hop을 줄일 수
있는 방향을 문서에 분명히 적는 것이 좋다.

## 14. 구현 반영 순서 초안

이 초안은 구현이 끝난 뒤 아래 순서로 문서에 반영하는 것을 권장한다.

1. `core/include/zlink.h`와 테스트를 먼저 확정한다.
2. socket 공통 문서에 request completion 실행 모델을 반영한다.
3. `DEALER`, `ROUTER`, `SPOT` 개별 문서에 타입별 예외와 progress point를 적는다.
4. polling 문서에 owner poller loop와 completion progress 관계를 적는다.
5. errno/result 문서에 timeout/terminate/public bucket 의미를 맞춘다.
6. 마지막으로 바인딩 spec에서 callback thread / coroutine resume thread 설명을
   맞춘다.

이 순서를 두는 이유는, 공통 계약과 타입별 계약을 먼저 고정해야 바인딩 문서가
중간 상태를 잘못 고정하지 않기 때문이다.

## 15. 회귀 테스트 항목 초안

이 초안이 구현되면 아래 테스트는 회귀 테스트로 고정하는 것이 좋다.

### 15.1 기본 completion 전달

- `DEALER request`가 정상 reply를 받고 callback이 정확히 한 번 실행되는지
- `ROUTER request`가 정상 reply를 받고 callback이 정확히 한 번 실행되는지
- `SPOT channel request`가 정상 reply를 받고 callback이 정확히 한 번 실행되는지
- `ROUTER -> SPOT request`가 정상 reply를 받고 callback이 정확히 한 번 실행되는지

### 15.2 실행 스레드 보장

- request completion callback이 I/O background thread가 아니라 owner progress
  thread에서 실행되는지
- 같은 handle의 ordinary recv와 request completion callback이 같은 스레드에서
  관찰되는지
- coroutine binding에서 await resume가 owner loop thread에서 일어나는지
- callback 안에서 같은 handle의 `close()`가 owner thread라고 해서 곧바로 accepted
  되지 않고, 재진입 제한대로 defer 또는 `EBUSY` 처리되는지

### 15.3 recv progress 경로

- same-handle `recv()` 호출만으로 pending reply completion이 진전되는지
- ordinary inbound message가 없어도 reply completion만으로 `recv()` 대기 경로가
  깨어날 수 있는지
- completion queue가 먼저 pending일 때 callback이 실행된 뒤 ordinary recv가
  이어지는지
- `ZLINK_DONTWAIT` 경로에서는 ordinary message와 completion이 모두 없을 때
  기존처럼 즉시 `EAGAIN`으로 돌아오는지

### 15.4 poller progress 경로

- owner poller loop만 돌고 있어도 request completion이 정체되지 않는지
- poller wait 직전 reply가 도착한 경우 lost wakeup 없이 completion이 전달되는지
- poller wait 중 reply가 도착한 경우 owner loop가 깨어나 completion을 drain하는지
- poller 경로가 ordinary `POLLIN`/`POLLOUT` 의미를 깨뜨리지 않는지

### 15.5 timeout completion

- timeout이 난 request가 callback으로 정확히 한 번 완료되는지
- timeout completion도 owner progress thread에서 실행되는지
- timeout 직전 reply 도착과 timeout 경합에서 중복 callback이 없는지
- timeout 뒤 late reply가 와도 callback이 다시 실행되지 않는지

### 15.6 terminate / close completion

- context terminate 시 pending request가 terminate completion으로 정리되는지
- owner progress thread가 close를 호출하면 pending request가 중복 없이 한 번만
  `TERMINATED` completion으로 정리되는지
- owner가 아닌 스레드가 pending request가 남은 handle을 닫으려 하면 `EBUSY`로
  실패하는지
- close/terminate completion도 same-thread 규칙을 지키는지
- close epilogue와 late reply 경합에서 double completion이 없는지
- close epilogue가 마지막 completion drain 뒤 destroy를 마무리하는지

### 15.7 local failure / protocol failure

- local route failure가 direct callback이 아니라 completion queue를 통해 전달되는지
- malformed reply envelope이 `PROTOCOL_ERROR`로 한 번만 완료되는지
- internal failure가 `INTERNAL_ERROR` bucket으로 정리되는지
- 성공 reply의 `parts` ownership이 기존 계약과 동일하게 callback으로 이전되는지

### 15.8 재진입 규칙

- callback 안에서 같은 handle의 recursive `recv()`가 금지되는지
- callback 안에서 같은 handle의 mode change가 금지되거나 `EBUSY`로 막히는지
- callback 안에서 같은 handle의 close가 지연 처리되거나 `EBUSY`로 막히는지
- callback 안에서 같은 handle의 `send`, `publish`, `reply`는 허용되는지

### 15.9 공정성

- completion만 많이 몰릴 때 ordinary recv가 영구 starvation 되지 않는지
- ordinary recv만 많이 몰릴 때 timeout completion이 과도하게 지연되지 않는지
- 여러 pending request가 있을 때 callback 순서가 문서 규칙과 테스트 기대에 맞는지

### 15.10 progress 의존성

- request-only 사용 코드가 progress pump 없이 completion을 받지 못하는지
- progress pump를 다시 돌리면 누적된 completion이 drain되는지
- binding 문서에 적힌 progress dependency와 실제 동작이 일치하는지

### 15.11 바인딩 회귀 테스트

- C binding의 callback thread 문서와 실제 동작이 일치하는지
- C++ binding wrapper가 owner-thread completion 모델을 깨지 않는지
- Python, Rust, Go, Node 등 future/promise/coroutine 래핑이 owner progress
  thread 규칙을 보존하는지

## 16. 열린 질문

구현 전 아래 항목은 추가 검토가 필요하다.

1. context terminate 경로에서도 owner-thread drain 규칙을 close와 같은 수준으로
   강제할지
2. poller 기반 progress를 어떤 내부 wakeup 방식으로 연결할지
3. completion 우선 drain과 ordinary recv 사이의 fairness budget을 어떻게 둘지
4. migration note에서 기존 I/O-thread callback 가정을 어떤 예시로 깨 주는 것이
   가장 이해하기 쉬운지

## 17. 요약

이 초안의 핵심은 아래 한 문장으로 정리할 수 있다.

- request completion callback을 없애지 말고, **owner progress thread에서
  실행되도록 귀속시키자**

이 방향을 따르면 공개 API를 크게 늘리지 않고도, `recv + poller` 중심 애플리케이션과
coroutine binding이 request/reply를 더 예측 가능하게 다룰 수 있다.
