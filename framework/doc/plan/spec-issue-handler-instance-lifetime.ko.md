# spec 이슈 — Handler 인스턴스 수명 계약

> 대상: framework 공통 spec 담당
> 제기: `dotnet/guide/03-concepts.ko.md` 정비 중 발견
> 기준 revision: working tree (2026-07-29)
> 관련 spec: [06-framework-api §8.1](../framework/common/spec/06-framework-api.ko.md)
> 상태: 완료

## 1. 요약

공통 spec은 handler를 **어디서 찾고 어떤 key로 dispatch하는지**는 정의하지만,
**handler 인스턴스를 언제 만들고 얼마나 살려 두는지**는 정의하지 않는다. 그 결과
.NET 구현에서 dispatch 경로마다 수명이 갈라져 있고, 문서는 이 차이를 설명하지
못한다. `PerActor` User Spot에서는 이 미정의가 실제 경합 위험으로 이어진다.

## 2. 현재 spec이 말하는 것과 말하지 않는 것

`06-framework-api.ko.md` §8.1은 **filter**에 대해서만 수명을 규정한다.

> 각 filter는 해당 dispatch의 DI scope에서 resolve하며 handler와 같은 scoped
> dependency를 사용한다. Root singleton으로 한 번 resolve해 여러 dispatch에서
> 공유하지 않는다.

여기서 "handler와 같은 scoped dependency"는 handler에도 dispatch scope가 있다는 것을
전제하지만, handler 자체의 인스턴스 수명은 어느 절에서도 정의하지 않는다. §8.1은
적용 범위도 ChannelName send/request dispatch로 한정하고 있어, Spot·Actor dispatch에는
이 문장을 그대로 적용할 수 없다.

## 3. 변경 전 .NET 구현

| dispatch 경로 | handler 인스턴스 수명 | 근거 |
| --- | --- | --- |
| ChannelName send/request | dispatch scope | §8.1과 일치 |
| Entry Spot actor packet | dispatch마다 새 scope | `ZLinkEntrySpotHandlerExecutor.cs:20-21`, `44-45` |
| Entry Spot direct handler | Entry Spot scope에서 resolve | `ZLinkEntrySpotActivation.cs:63` |
| User Spot direct·subscription·timer | Spot scope에서 resolve | `ZLinkSpotActivation.cs:67` |
| User Spot actor packet | Spot scope에서 resolve | `ZLinkSpotActivation.cs:103`이 같은 invoker를 전달 |
| Instance Spot direct handler | Spot scope에서 resolve | `ZLinkSpotActivation.cs:67` |

현재 `ZLinkScopedHandlerInstanceOwner.Resolve(Type)`은 handler의 Application DI
등록 여부에 따라 다른 결과를 만든다.

1. `Services.GetService(handlerType)`으로 먼저 조회한다. Application이 DI에 등록했다면
   그 등록의 lifetime을 따른다.
2. 등록이 없으면 `ActivatorUtilities.CreateInstance`로 만들고 `_fallbackInstances`에
   보관해 이후 dispatch에서 재사용한다.
3. Owner는 activation과 함께 dispose된다(`ZLinkSpotActivationExecution.cs:79`).

따라서 “Spot·Actor handler는 activation마다 정확히 하나”라고 설명할 수 없다. 같은
handler type도 Application DI 등록에 따라 host 전체에서 공유되거나 dispatch마다 새로
만들어질 수 있다. 이 차이는 현재 public contract에 없다.

Execution mode는 이 동작에 관여하지 않는다. `ZLinkSpotActivationConfiguration.cs:66`의
`AttachSpot`은 `ExecutionMode`를 보지 않고 항상 activation owner로 invoker를 만든다.
`ExecutionMode`는 직렬화 gate와 relocation 계획에만 사용한다.

## 4. 왜 문제인가

1. **같은 개념이 경로마다 다르게 동작한다.** Entry Spot의 actor packet만 dispatch
   scope이고 나머지 Spot·Actor 경로는 Spot scope와 DI 등록 lifetime이 함께 결정한다. Spec에 근거가 없으므로
   어느 쪽이 의도인지 판정할 수 없고, 다른 언어 구현이 무엇을 따라야 하는지도
   정해지지 않는다.
2. **`PerActor`에서 경합할 수 있다.** Scoped handler와 Framework가 만든 handler는
   member Actor들 사이에서 공유된다. `PerActor`는 서로 다른 Actor lane을 동시에
   실행하므로 mutable handler field가 있으면 data race가 발생한다.
3. **captive dependency가 문서화되어 있지 않다.** 생성자 주입 의존성이 activation당
   한 번 resolve되므로 spot 수명만큼 붙잡힌다. Application이 per-message scoped
   서비스를 생성자로 받으면 조용히 잘못 동작한다. → §4.1 참고. 이 항목은 guide에서
   먼저 처리했다.
4. **guide가 이 차이를 설명할 근거가 없다.** 현재 `03-concepts` §7.1은 channel
   handler 수명만 적고 Spot handler 수명은 비워 두었다.

### 4.1 DI scope도 객체별로 분리해야 한다

현재 `ZLinkSpotActivationFactory.cs:57`·`:130`은 Spot activation마다 DI scope를
하나 만든다. Spot 본체와 Spot handler에는 이 범위가 맞다. 그러나 Actor handler까지
같은 scope를 사용하면 서로 다른 Actor가 scoped dependency를 공유한다.

Application이 지켜야 할 기본 사용 규칙은
[`dotnet/guide/06-spot.ko.md` §4.1](../framework/dotnet/guide/06-spot.ko.md)에
서술했다. 요지는 두 가지다.

- ORM context 같은 per-message 자원은 Spot에서 직접 다루지 않고 channel handler가
  있는 서비스에 요청한다.
- Spot 안에서 써야 하면 `IServiceScopeFactory`로 그 호출에만 사는 scope를 연다.

Spec은 Spot scope와 Actor scope를 분리한다. Actor가 Spot에 들어오거나 target에서
복원될 때 Actor activation scope를 만들고, leave·destroy·relocation source cleanup에서
정리한다. Dispatch마다 child scope를 만드는 방식은 사용하지 않는다.

## 5. 제안하는 방향

### 5.1 Handler가 처리하는 객체의 activation에 수명을 맞춘다

Execution mode나 handler별 option으로 lifetime을 선택하지 않는다. Handler가 직접
처리하는 객체를 기준으로 세 규칙을 사용한다.

| 경로 | 수명 | 근거 |
| --- | --- | --- |
| Channel handler, filter | dispatch scope | 서비스 경계다. 외부 호출과 scoped 의존성이 일반적이다 |
| Spot packet·request·subscription·timer handler | 해당 Spot activation 수명 | Spot turn의 동작을 처리한다 |
| Actor send·request handler | 해당 Actor activation 수명 | Actor별 turn과 relocation 경계에 맞춘다 |

Actor handler를 hosting Spot scope에 두지 않는다. `PerActor` User Spot과 Entry Spot은
여러 Actor turn을 동시에 실행할 수 있기 때문이다. Actor마다 scope를 나누면 서로 다른
Actor가 handler instance와 scoped dependency를 공유하지 않는다.

`SpotWide`에서도 같은 Actor 수명 규칙을 사용한다. 현재는 Spot gate가 Actor handler를
직렬화하지만 execution mode가 바뀌어도 handler lifetime과 dependency 의미가 바뀌지
않아야 한다.

Framework는 handler type을 Application DI에서 직접 resolve하지 않는다. Handler
instance는 Framework가 해당 activation에서 정확히 한 번 만들고 재사용한다. 생성자
dependency만 해당 activation의 DI scope에서 resolve한다. Handler type의 DI 등록
lifetime으로 이 규칙을 바꿀 수 없다.

C++는 Actor handler를 별도 class가 아니라 Spot member function으로 표현한다. 이
경우 새 handler object를 만들지 않고 Actor별 state와 실행 resource만 Actor activation이
소유한다. Spot member에는 Actor별 mutable state를 저장하지 않는다.

### 5.2 캐싱에 따라오는 제약을 함께 규정한다

수명만 정하고 끝내면 captive dependency 문제가 남는다. 다음을 같은 절에 명시한다.

- Spot handler의 생성자 dependency는 Spot activation scope에서 resolve한다.
- Actor handler의 생성자 dependency는 Actor activation scope에서 resolve한다.
- Handler instance field에 복구해야 하는 application state를 두지 않는다. 해당
  상태는 Spot 또는 Actor가 소유한다.
- Spot relocation은 source Spot handler를 정리하고 target Spot activation에서 다시
  만든다. Actor relocation과 cross-node Join은 source Actor handler를 정리하고 target
  Actor activation에서 다시 만든다.
- Same-node Join은 Actor activation을 유지하므로 handler와 scope도 유지한다.
- Handler instance와 dependency는 relocation payload에 포함하지 않는다.
- Activation이 끝나면 Framework가 handler와 activation scope를 정확히 한 번 정리한다.
- 종료를 시작하면 새 dispatch를 막고, 이미 받아들였거나 실행 중인 dispatch가 끝난 뒤
  handler와 dependency scope를 정리한다.
- Handler가 현재 dispatch 안에서 종료 operation을 시작해도 자기 dispatch가 끝나기를
  기다리는 순환 대기가 발생하지 않는다.

### 5.3 `06-framework-api` §8.1의 적용 범위를 명확히 한다

"root singleton으로 여러 dispatch에서 공유하지 않는다"는 문장이 Channel handler·filter
범위임을 밝히고, Spot·Actor handler는 5.1의 별도 규칙을 따른다고 연결한다. 지금은 이
문장이 전체 handler에 적용되는 것처럼 읽힌다.

### 5.4 Entry Spot actor packet 경로를 정렬한다

Entry Spot 자체의 direct handler는 Entry Spot activation 수명을 사용한다. Entry Spot에
속한 Actor handler는 Entry Spot과 공유하지 않고 Actor activation 수명을 사용한다.
따라서 현재 dispatch scope 구현은 Actor activation scope로 교체한다.

## 6. 확정한 판단

1. Channel handler와 filter는 dispatch scope를 사용한다.
2. Spot handler는 Spot activation, Actor handler는 Actor activation 수명을 사용한다.
3. Handler lifetime을 registration option으로 노출하지 않는다.
4. Handler type의 Application DI lifetime은 Framework handler lifetime을 바꾸지 않는다.
5. Entry Spot actor packet도 Actor activation 수명으로 정렬한다.

## 7. 적용 결과

- `06-framework-api.ko.md`에 handler 수명 절을 추가하고 §8.1 적용 범위를 명확히 했다.
- `11-spot-model.ko.md`와 `14-actor-model.ko.md`에서 수명 계약을 연결했다.
- 다섯 언어 exact interface와 구현을 같은 의미로 맞췄다.
- 다음 contract test를 추가했다.
  - 별도 handler class를 사용하는 언어는 같은 activation의 같은 handler type을 반복
    dispatch에서 같은 instance로 실행한다.
  - 서로 다른 Actor는 mutable handler state나 scoped dependency를 공유하지 않는다.
  - C++ Actor handler는 Spot member function 표현을 유지하면서 Actor별 state를 Actor
    activation에 둔다.
  - Same-node Join은 Actor handler를 유지한다.
  - Cross-node Join·relocation은 target에서 새 handler를 만들고 source를 한 번 정리한다.
  - Handler type의 DI singleton·transient 등록은 Framework lifetime을 바꾸지 않는다.
- `.NET`과 C++ guide의 handler 수명과 상태 배치 규칙을 현재 구현에 맞췄다.
- 종료 전에 수락된 dispatch를 먼저 끝내고 정리하는 lifecycle barrier와 handler
  내부 종료 호출의 순환 대기 방지 규칙을 네 runtime에 반영했다.
- 독립 Codex high review에서 발견한 P1 네 건과 P2 한 건을 수정했으며, 재검토에서
  추가 P1·P2가 없음을 확인했다.
- 구체적인 test 결과와 문서 검증 수치는
  [execution ledger](v11.0/route-mesh-11.0.0-execution-ledger.ko.md)의
  `2026-07-29 Handler instance 수명 계약 보강` 절에 기록했다.
