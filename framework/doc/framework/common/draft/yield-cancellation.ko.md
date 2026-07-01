# yield cancellation 공개 계약 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, 언어별 framework 구현에 바로 추가할
> API를 확정하지 않는다.

## 배경

공통 E2E Config 8의 `YD-E2`는 handler가 `yield`로 외부 작업을 기다리는 동안 취소가 발생하면
대기 중인 작업과 continuation이 정리되고, 같은 mailbox의 다음 작업이 정상 진행되는지 검증한다.

.NET framework는 handler의 `CancellationToken`을 `Yield<T>(token)`에 넘겨 이 동작을 검증한다.
C++ framework는 현재 public cancellation token을 제공하지 않고, public `yield()` 호출도 token 인자를
받지 않는다. `pending_operation_t::cancel()`은 public header에 있지만, 현재 call object의 `yield()`와
결합할 수 있는 사용자-facing 경로가 아니며 C++ spec도 이 타입을 일반 사용자가 직접 다루지 않는
내부 pending 상태 핸들로 설명한다.

따라서 C++ `YD-E2`를 내부 pending 상태 접근, handler-local timeout, 테스트 전용 adapter로 통과시키면
공통 E2E가 요구하는 공개 동작을 검증하지 못한다.

## 필요한 공개 의미

취소 계약을 받아들이려면 framework 공통 계약은 아래 의미를 먼저 정해야 한다.

- handler 안에서 시작한 `yield` 대기 작업을 사용자가 공개 API로 취소할 수 있어야 한다.
- 취소된 `yield`는 continuation을 정상 reply/send 경로로 재개하지 않아야 한다.
- 취소 결과는 언어별 public error 또는 정해진 cancellation result로 관찰되어야 한다.
- 취소 뒤 같은 Spot, actor, timer mailbox는 다음 작업을 처리할 수 있어야 한다.
- timeout, runtime shutdown, stream disconnect와 cancellation의 결과 구분이 문서화되어야 한다.

## 설계 후보

### 후보 A: 언어별 cancellation token을 `yield`에 전달한다

각 언어가 자기 런타임에 맞는 cancellation token을 공개하고, `yield` terminator가 그 token을 받는다.
.NET의 현재 방식과 가장 비슷하지만 C++에는 새 public token/source 타입이 필요하다.

### 후보 B: call object에 cancellable operation을 공개한다

사용자가 call object에서 취소 가능한 operation을 얻고, 그 operation을 취소하면 `yield` 대기가
취소된다. 이 방식은 C++의 기존 `pending_operation_t`와 가까워 보이지만, 현재 문서가 이 타입을
내부 상태 핸들로 설명하므로 그대로 노출 범위를 넓히면 공개 계약을 다시 정리해야 한다.

### 후보 C: call object에 cancellation policy를 설정한다

`timeout(...)`처럼 call object에 취소 policy를 설정하고, `yield()`는 그 policy를 따른다. 사용자가
token 객체를 직접 다루지 않아도 되지만, 외부 event로 취소하는 흐름을 어떻게 표현할지 별도 설계가
필요하다.

## 현재 판정

`YD-E2`는 공통 E2E 요구이므로 완료로 표시하지 않는다. 다만 C++에 새 public API를 바로 추가하지도
않는다. 먼저 이 draft를 기준으로 공통 framework 계약과 C++ spec/guide를 갱신할지 리뷰한 뒤,
확정된 공개 표면으로 C++ E2E를 구현한다.
