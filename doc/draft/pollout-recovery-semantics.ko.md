[스펙 목차](../README.ko.md)

# Draft -- POLLOUT Recovery Semantics

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 raw socket family의 writable readiness 의미를 다시 정리한다.

핵심 목표는 아래와 같다.

- `zlink_send_ready_handler()`는 유지한다.
- `ZLINK_POLLOUT`은 지금보다 쓰기 쉬운 표면으로 다시 정의한다.
- `BACKPRESSURED` 이후 송신 재시도 시점을 poller에서도 자연스럽게 다룰 수 있게
  한다.
- callback 기반 writable 알림과 poller 기반 writable 알림의 의미를 가능한 한
  맞춘다.

## 2. 배경

현재 writable readiness를 다루는 공개 표면은 두 가지다.

- `zlink_send_ready_handler()`
- data-plane poller의 `ZLINK_POLLOUT`

실사용에서는 아래 패턴이 자주 나온다.

1. 호출자가 `send`, `publish`, `request` 같은 submit 함수를 호출한다.
2. 결과가 `ZLINK_SUBMIT_BACKPRESSURED`로 실패한다.
3. 호출자는 이 handle이 다시 보낼 수 있는 시점을 기다린다.
4. 회복 알림이 오면 송신을 재시도한다.

문제는 `ZLINK_POLLOUT`이 이 패턴에 바로 맞지 않으면 사용 코드가 불편해진다는
점이다.

- `BACKPRESSURED`가 난 뒤에만 writable 관심을 등록해야 하는 경우가 있다.
- 회복되면 다시 관심을 빼는 패턴을 호출자가 직접 관리해야 한다.
- registration 시점과 회복 시점 사이의 경쟁 상태를 호출자가 걱정하게 된다.
- 결국 poller를 쓰는 쪽이 callback보다 더 low-level 동기화 부담을 지게 된다.

이 문서는 이 문제를 "POLLOUT의 공개 의미를 회복 관점으로 더 또렷하게 고정한다"는
방식으로 해결하려고 한다.

## 3. 설계 원칙

### 3.1 writable readiness는 send 성공 보장이 아니다

`send_ready_handler`나 `POLLOUT`은 모두 "지금 send가 반드시 성공한다"는 보장을
뜻하지 않는다.

이 표면이 주는 의미는 더 약하다.

- busy loop 없이 송신을 다시 시도할 가치가 있는 시점이 왔다.

즉 readiness 알림 뒤 첫 재시도가 다시 `BACKPRESSURED`로 실패할 수도 있다.

### 3.2 callback과 poller는 같은 readiness 의미를 공유해야 한다

사용자가 callback을 쓰든 poller를 쓰든, writable readiness의 공개 의미는 가능하면
같아야 한다.

이 초안은 아래 방향을 기준으로 둔다.

- `zlink_send_ready_handler()`와 `ZLINK_POLLOUT`은 같은 개념을 다른 표면으로
  제공한다.
- 둘 다 "backpressure 회복 이후 재시도 시점 관찰"에 쓰인다.
- 차이는 실행 방식뿐이다.

### 3.3 poller 경로가 callback보다 더 불편해서는 안 된다

poller는 서버 루프의 기본 도구다. 따라서 poller 경로가 callback보다 더 낮은 수준의
동기화 부담을 호출자에게 떠넘기면 안 된다.

이 문서는 특히 아래 상황을 중요하게 본다.

- `BACKPRESSURED` 직후 `POLLOUT` 관심 등록
- 등록 직전 또는 직후에 회복이 이미 일어난 경우
- 한 번 회복된 뒤 다시 막히는 경우

이 경계에서 사용자가 알림을 놓치지 않도록 공개 의미를 정리해야 한다.

## 4. 새 의미 요약

이 초안은 `ZLINK_POLLOUT`을 아래처럼 정의한다.

- `ZLINK_POLLOUT`은 단순 "항상 writable일 수 있음"을 뜻하는 일반 신호가 아니다.
- `ZLINK_POLLOUT`은 이 handle이 backpressure 상태에서 벗어나 송신을 다시 시도할
  가치가 있는 상태임을 뜻한다.
- poller에 `ZLINK_POLLOUT` 관심이 등록되어 있으면, 현재 handle이 이미 재시도
  가능한 상태일 때도 readable과 같은 방식으로 관찰 가능해야 한다.

즉 `ZLINK_POLLOUT`은 level-triggered writable을 그대로 노출하는 low-level 표면이
아니라, send recovery 관점의 writable readiness 표면으로 본다.

## 5. 공개 계약 초안

### 5.1 send_ready_handler

`zlink_send_ready_handler()`는 그대로 유지한다.

```c
ZLINK_EXPORT zlink_handler_result_t zlink_send_ready_handler (
  void *s_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_);
```

이 초안에서 이 함수의 공개 의미는 아래와 같다.

- 이 handle이 송신 재시도를 시도할 가치가 있는 writable 상태로 전이하면
  callback을 호출한다.
- 이 callback은 send 성공 보장을 뜻하지 않는다.
- callback은 backpressure recovery notification 표면이다.

### 5.2 ZLINK_POLLOUT

`ZLINK_POLLOUT`도 같은 의미를 공유한다.

- poller가 `ZLINK_POLLOUT`을 보고했다는 것은, 이 handle이 송신 재시도를 시도할
  가치가 있는 상태라는 뜻이다.
- 이는 "지금 바로 send가 반드시 성공한다"는 보장이 아니다.
- 이 알림을 받은 호출자는 send를 다시 시도할 수 있다.

### 5.3 callback과 poller의 의미 일치

공개 의미 차원에서 아래 해석을 고정한다.

- `send_ready_handler`가 불리는 조건과
- `ZLINK_POLLOUT`이 보고되는 조건은
  가능한 한 같은 readiness 상태를 가리킨다.

즉 callback과 poller가 서로 다른 상태 기계를 노출해서는 안 된다.

## 6. BACKPRESSURED 이후 권장 사용 흐름

이 초안이 권장하는 기본 흐름은 아래와 같다.

### 6.1 callback 사용자

1. `send` 계열 함수를 호출한다.
2. 결과가 `ZLINK_SUBMIT_BACKPRESSURED`면 `send_ready_handler`를 통해 회복을
   기다린다.
3. callback이 오면 send를 다시 시도한다.

### 6.2 poller 사용자

1. `send` 계열 함수를 호출한다.
2. 결과가 `ZLINK_SUBMIT_BACKPRESSURED`면 poller에서 `ZLINK_POLLOUT` 관심을
   유지하거나 등록한다.
3. `ZLINK_POLLOUT`이 보고되면 send를 다시 시도한다.
4. backlog가 모두 비워졌거나 더 이상 회복 알림이 필요 없으면 관심을 해제한다.

핵심은 두 흐름 모두 "backpressure recovery를 관찰한 뒤 재시도"라는 같은 모델을
쓴다는 점이다.

## 7. 경쟁 상태 처리 원칙

이 초안에서 가장 중요한 계약은 이 절이다.

### 7.1 등록 시점 경쟁 상태

호출자가 `BACKPRESSURED` 결과를 받은 뒤 `ZLINK_POLLOUT` 관심을 등록하는 동안,
내부 상태는 이미 회복됐을 수도 있다.

이 경우 poller 경로가 callback 경로보다 본질적으로 더 불리하면 안 된다.

따라서 공개 계약은 아래 방향을 따른다.

- `ZLINK_POLLOUT` 관심이 등록된 시점에 handle이 이미 recovery-eligible 상태면,
  poller는 그 상태를 관찰할 수 있어야 한다.
- 즉 호출자는 "등록 직전에 회복이 일어나면 알림을 영원히 놓칠 수 있다"는 전제를
  기본 사용 규칙으로 가져가면 안 된다.

이 문서는 이를 "lost wakeup을 피할 수 있는 writable readiness"라는 요구로 본다.

### 7.2 재시도 후 재차 backpressure

`ZLINK_POLLOUT` 또는 `send_ready_handler` 알림 뒤 재시도한 send가 다시
`BACKPRESSURED`가 될 수 있다.

이 경우 공개 의미는 아래와 같다.

- 이전 readiness 알림이 잘못된 것은 아니다.
- 단지 재시도 가능성이 있었지만, 호출 시점에는 다시 막혔다는 뜻이다.
- 호출자는 다시 recovery 알림을 기다릴 수 있다.

### 7.3 다중 스레드 사용

같은 handle의 writable 관심 등록/해제와 송신 재시도를 여러 스레드가 동시에
만지는 경우는 정책을 더 복잡하게 만든다.

이 초안은 기본 사용 모델을 아래처럼 둔다.

- 같은 handle의 writable recovery 관찰과 send 재시도는 하나의 소유 루프 또는
  하나의 조정 지점에서 관리하는 것을 권장한다.

즉 이 문서는 `POLLOUT`의 의미를 개선하지만, 다중 스레드 경쟁 자체를 없애는 계약을
새로 추가하지는 않는다.

## 8. POLLOUT 의미 변경 방향

### 8.1 변경 전 문제 인식

현재 `POLLOUT`이 아래처럼 읽히면 실사용이 불편해진다.

- writable일 수도 있다
- 저수준 transport 쓰기 가능 상태다

이 해석만으로는 backpressure recovery를 기다리는 호출자가 원하는 동작을 얻기
어렵다.

### 8.2 변경 후 목표 의미

이 초안이 목표로 하는 `POLLOUT` 의미는 아래와 같다.

- 이 handle의 송신 경로가 적어도 한 번은 다시 진행될 가능성이 생겼다.
- 따라서 backlog가 있으면 재시도할 가치가 있다.

즉 `POLLOUT`을 "transport writable"보다 "send recovery readiness"에 더 가까운
공개 표면으로 본다.

## 9. 소켓 타입별 적용 관점

이 문서는 모든 send-capable subject에 같은 의미 축을 적용한다.

대상은 아래와 같다.

- raw `PAIR`
- raw `PUB`
- raw `XPUB`
- raw `DEALER`
- raw `ROUTER`
- raw `STREAM`
- unified `spot`
- `spot node`

다만 실제 내부 구현 방식은 타입별로 다를 수 있다.

- 어떤 타입은 큐 공간 회복을 기준으로 readiness를 만들 수 있다.
- 어떤 타입은 peer path 회복을 포함해 readiness를 만들 수 있다.

하지만 공개 계약 차원에서는 아래 문장을 유지한다.

- `POLLOUT`과 `send_ready_handler`는 "send recovery readiness"를 알린다.

## 10. 구현 순서 기준

### 10.1 문서 의미 먼저 고정

먼저 socket 공통 spec과 polling 문서에서 `POLLOUT` 의미를 다시 쓴다.

- 단순 writable이 아니라 recovery readiness라는 점을 명시한다.
- 첫 재시도 성공 보장이 아님을 명시한다.
- callback과 poller의 의미가 같은 축이라는 점을 명시한다.

### 10.2 poller 관심 등록 경계 정리

그 다음 poller 등록 시점의 경쟁 상태에 대해 문서와 구현을 맞춘다.

- 등록 시점에 이미 recovery-eligible 상태면 poller가 이를 관찰할 수 있게 한다.
- callback 경로보다 poller 경로가 더 쉽게 wakeup을 잃지 않도록 한다.

### 10.3 타입별 테스트 정리

아래 테스트를 우선 추가하거나 강화한다.

- raw `PAIR`, `DEALER`, `PUB`, `ROUTER`, `STREAM`, `SPOT`에서 공통 recovery
  readiness 의미가 같은 축으로 동작하는지
- `BACKPRESSURED` 후 `POLLOUT`으로 회복을 관찰할 수 있는지
- `BACKPRESSURED` 후 `send_ready_handler`로 회복을 관찰할 수 있는지
- 두 표면이 같은 종류의 회복 상태를 보고하는지
- recovery 알림 후 즉시 재시도해도 다시 `BACKPRESSURED`가 될 수 있음을 허용하는지
- 등록 시점 경쟁 상태에서 wakeup을 놓치지 않는지

## 11. 헤더와 spec 반영 포인트

이 초안은 새로운 공개 함수 추가를 요구하지 않는다.

필요한 변경은 주로 의미론 정리다.

- `zlink_send_ready_handler()` 설명 수정
- `ZLINK_POLLOUT` 설명 수정
- socket 공통 spec의 send-ready 절 수정
- polling spec의 writable readiness 설명 수정
- 필요하면 errno-map에 recovery readiness 해석을 보조 설명으로 추가

## 12. 기대 효과

이 개정으로 기대하는 효과는 아래와 같다.

- poller 기반 서버 루프에서도 writable recovery를 더 자연스럽게 다룰 수 있다.
- callback 사용자와 poller 사용자가 같은 의미의 readiness를 공유하게 된다.
- `BACKPRESSURED` 이후 관심 등록/해제 패턴이 덜 어색해진다.
- `POLLOUT`이 있으나 실사용이 불편한 상태를 줄일 수 있다.

## 13. 남은 확인 사항

구현 전 마지막으로 확인해야 할 항목은 아래와 같다.

- 현재 poller 구현이 recovery-eligible 상태를 어떻게 캐시하고 보고하는지
- `SPOT`의 send-ready와 `POLLOUT`도 같은 의미 축으로 묶을지
- monitor/timer처럼 send가 없는 subject와 문서 구성을 어떻게 분리할지

## 14. 정식 spec 분해 계획

구현과 공개 헤더가 정리되면 이 초안 내용은 아래 문서들로 나누어 반영한다.

- `doc/spec/core/socket/README*.md`
  send-ready callback과 `POLLOUT` 의미
- `doc/spec/core/polling*.md`
  poller writable readiness 의미
- 필요하면 `doc/spec/core/errno-map*.md`
  `BACKPRESSURED`와 recovery readiness 보조 설명
