# spec 이슈 — Handler 인스턴스 수명과 filter 적용 범위

> 대상: framework 공통 spec 담당
> 제기: `dotnet/guide/03-concepts.ko.md` 정비 중 발견
> 확인 revision: `edb0461448` (`fix framework handler activation ownership`)
> 관련 spec: [06-framework-api §8.1](../framework/common/spec/06-framework-api.ko.md)

## 1. 이 문서가 다루는 두 가지

Framework가 handler를 실행할 때 결정해야 하는 두 계약을 다룬다. 둘 다 판단은
끝났고, 반영 상태가 서로 다르다.

| 주제 | 정한 것 | 상태 | 절 |
| --- | --- | --- | --- |
| Handler 인스턴스 수명 | Handler instance를 언제 만들고 언제 정리하는가 | 공통 spec·다섯 언어 구현·contract test 반영 완료 | [2](#2-handler-수명--무엇이-문제였나)~[4](#4-handler-수명--반영-결과) |
| Filter 적용 범위 | Handler filter가 어느 dispatch 경로에서 실행되는가 | 설계 결정과 반영안 검토 완료, spec·구현에는 아직 반영하지 않음 | [5](#5-filter-범위--무엇이-문제였나)~[8](#8-filter-범위--남은-작업) |

두 주제에서 공통으로 쓰는 말을 먼저 정리한다.

- **dispatch** — 도착한 message 하나를 handler 하나가 처리하는 한 번의 실행이다.
- **activation** — Spot이나 Actor 하나가 이 node에서 살아 있는 기간이다. 만들어질
  때 시작하고 닫히거나 다른 node로 옮겨갈 때 끝난다.
- **DI scope** — `.NET` 의존성 주입 컨테이너가 `Scoped` 서비스 인스턴스를 하나로
  유지하는 범위다. Framework가 이 범위를 dispatch마다 여는지 activation마다 여는지가
  이 문서의 쟁점이다.

## 2. Handler 수명 — 무엇이 문제였나

### 2.1 spec이 정의하지 않은 부분

공통 spec은 handler를 **어디서 찾고 어떤 key로 고르는지**는 정의한다. 그러나 고른
handler의 **인스턴스를 언제 만들고 얼마나 살려 두는지**는 어느 절에도 없었다.

`06-framework-api.ko.md` §8.1이 수명을 언급하는 유일한 자리인데, 대상이 filter다.

> 각 filter는 해당 dispatch의 DI scope에서 resolve하며 handler와 같은 scoped
> dependency를 사용한다. Root singleton으로 한 번 resolve해 여러 dispatch에서
> 공유하지 않는다.

"handler와 같은 scoped dependency"라는 표현은 handler에도 dispatch 단위 scope가
있다는 것을 전제한다. 그러나 §8.1은 적용 범위를 ChannelName send·request dispatch로
한정하고 있어, Spot·Actor handler에는 이 문장을 그대로 적용할 수 없었다.

### 2.2 그래서 `.NET`에서 벌어진 일

같은 "handler"인데 경로마다 수명이 달랐다.

| dispatch 경로 | 변경 전 handler 수명 | 근거 |
| --- | --- | --- |
| ChannelName send·request | dispatch마다 새로 만든다 | §8.1과 일치 |
| Entry Spot actor packet | dispatch마다 새 scope를 연다 | `ZLinkEntrySpotHandlerExecutor.cs:20-21`, `44-45` |
| Entry Spot direct handler | Entry Spot activation scope에서 가져온다 | `ZLinkEntrySpotActivation.cs:63` |
| User Spot direct·subscription·timer | Spot activation scope에서 가져온다 | `ZLinkSpotActivation.cs:67` |
| User Spot actor packet | Spot activation scope에서 가져온다 | `ZLinkSpotActivation.cs:103`이 같은 invoker를 전달한다 |
| Instance Spot direct handler | Spot activation scope에서 가져온다 | `ZLinkSpotActivation.cs:67` |

수명이 갈리는 이유는 `ZLinkScopedHandlerInstanceOwner.Resolve(Type)`이 세 단계로
동작하기 때문이다.

1. `Services.GetService(handlerType)`으로 먼저 조회한다. Application이 그 type을 DI에
   등록해 두었다면 등록한 lifetime을 그대로 따른다.
2. 등록이 없으면 `ActivatorUtilities.CreateInstance`로 만들어 `_fallbackInstances`에
   보관하고 이후 dispatch에서 재사용한다.
3. 이 보관소는 activation이 끝날 때 함께 정리된다(`ZLinkSpotActivationExecution.cs:79`).

따라서 "Spot·Actor handler는 activation마다 정확히 하나"라고 설명할 수 없었다. 같은
handler type이라도 application이 DI에 singleton으로 등록했으면 host 전체에서 하나가
공유되고, transient로 등록했으면 dispatch마다 새로 만들어진다. 이 차이는 공개 계약에
없던 동작이다.

Execution mode는 이 동작에 관여하지 않았다. `ZLinkSpotActivationConfiguration.cs:66`의
`AttachSpot`은 `ExecutionMode`를 보지 않고 항상 activation 단위로 invoker를 만든다.
`ExecutionMode`는 실행 순서를 직렬화하는 gate와 relocation 계획에만 쓴다.

### 2.3 무엇이 실제로 위험한가

1. **같은 개념이 경로마다 다르게 동작한다.** Entry Spot의 actor packet만 dispatch
   단위이고 나머지 Spot·Actor 경로는 activation scope와 application DI 등록이 함께
   결정한다. Spec에 근거가 없으므로 어느 쪽이 의도인지 판정할 수 없고, 다른 언어
   구현이 무엇을 따라야 하는지도 정해지지 않는다.
2. **`PerActor`에서 경합할 수 있다.** 한 Spot에 속한 여러 Actor가 같은 handler
   instance를 공유하는데, `PerActor`는 서로 다른 Actor를 동시에 실행한다. Handler에
   변경 가능한 field가 있으면 두 Actor가 같은 값을 동시에 건드린다.
3. **의존성이 예상보다 오래 살아 있다.** 생성자로 주입한 의존성이 activation당 한 번만
   만들어지므로 Spot이 닫힐 때까지 유지된다. Application이 요청마다 새로 만들어져야 하는
   서비스를 생성자로 받으면 오류 없이 잘못 동작한다([2.4](#24-di-scope도-객체별로-나눠야-한다) 참고).
4. **guide가 이 차이를 설명할 근거가 없었다.** `03-concepts`는 channel handler 수명만
   적고 Spot handler 수명은 비워 두었다.

### 2.4 DI scope도 객체별로 나눠야 한다

`ZLinkSpotActivationFactory.cs:57`·`:130`은 Spot activation마다 DI scope를 하나 만든다.
Spot 본체와 Spot handler에는 이 범위가 맞다. 그러나 Actor handler까지 같은 scope를
사용하면 서로 다른 Actor가 같은 `Scoped` 서비스 인스턴스를 공유한다.

Application이 지켜야 할 사용 규칙은
[`dotnet/guide/06-spot.ko.md`](../framework/dotnet/guide/06-spot.ko.md)에 먼저 서술했다.
요지는 둘이다.

- `DbContext`처럼 요청 단위로 써야 하는 자원은 Spot에서 직접 다루지 않고 channel
  handler가 있는 서비스에 요청한다.
- Spot 안에서 써야 하면 `IServiceScopeFactory`로 그 호출 동안만 사는 scope를 연다.

## 3. Handler 수명 — 무엇으로 정했나

### 3.1 세 대안 중 하나를 골랐다

| 대안 | 장점 | 문제 |
| --- | --- | --- |
| 모든 dispatch마다 새로 만든다 | 호출 사이에 상태가 남지 않는다 | Spot·Actor activation과 handler 수명이 어긋나고 생성·정리 비용이 매번 든다 |
| Spot activation scope에서 공유한다 | 구현이 단순하다 | `PerActor`와 Entry Spot에서 여러 Actor가 handler와 의존성을 공유한다 |
| **handler가 처리하는 객체의 activation이 소유한다** | 실행·relocation·정리 경계와 handler 수명이 일치한다 | Framework가 Spot scope와 Actor scope를 각각 관리해야 한다 |

세 번째를 선택했다. Handler를 그 handler가 다루는 객체와 같은 주기로 묶으면
execution mode에 따라 수명 의미가 바뀌지 않고, 서로 다른 Actor가 변경 가능한
의존성을 공유하지 않는다.

### 3.2 수명 규칙

Execution mode나 handler별 option으로 수명을 고르지 않는다. Handler가 직접 처리하는
객체를 기준으로 세 규칙만 사용한다.

| 경로 | 수명 | 근거 |
| --- | --- | --- |
| Channel handler와 filter | dispatch마다 새로 만들고 끝나면 정리한다 | 서비스 경계다. 외부 호출과 요청 단위 의존성이 일반적이다 |
| Spot packet·request·subscription·timer handler | 그 Spot activation과 같다 | Spot turn의 동작을 처리한다 |
| Actor send·request handler | 그 Actor activation과 같다 | Actor별 실행 순서와 relocation 경계에 맞춘다 |

Actor handler를 Spot scope에 두지 않는다. `PerActor` User Spot과 Entry Spot은 여러
Actor를 동시에 실행할 수 있기 때문이다. Actor마다 scope를 나누면 서로 다른 Actor가
handler instance와 의존성을 공유하지 않는다.

`SpotWide`에서도 같은 Actor 수명 규칙을 쓴다. 지금은 Spot gate가 Actor handler 실행을
직렬화하지만, execution mode를 바꿔도 handler 수명과 의존성 의미는 달라지지 않아야
한다.

Framework는 handler type을 application DI에서 직접 가져오지 않는다. Handler instance는
Framework가 해당 activation에서 정확히 한 번 만들어 재사용하고, 생성자 의존성만 그
activation의 DI scope에서 가져온다. Handler type을 application이 어떤 lifetime으로
등록했든 이 규칙은 바뀌지 않는다.

C++는 Actor handler를 별도 class가 아니라 Spot member function으로 표현한다. 이때는
새 handler 객체를 만들지 않고 Actor별 상태와 실행 자원만 Actor activation이 소유한다.
Spot member에는 Actor별 변경 상태를 저장하지 않는다.

### 3.3 수명과 함께 규정한 제약

수명만 정하고 끝내면 의존성이 오래 붙잡히는 문제가 남는다. 다음을 같은 절에 함께
명시한다.

- Spot handler의 생성자 의존성은 Spot activation scope에서 가져온다.
- Actor handler의 생성자 의존성은 Actor activation scope에서 가져온다.
- 복구해야 하는 application 상태를 handler field에 두지 않는다. 그 상태는 Spot 또는
  Actor가 소유한다.
- Spot relocation은 source의 Spot handler를 정리하고 target activation에서 다시 만든다.
  Actor relocation과 다른 node로의 Join도 같다.
- 같은 node 안에서의 Join은 Actor activation을 유지하므로 handler와 scope도 유지한다.
- Handler instance와 의존성은 relocation payload에 넣지 않는다.
- Activation이 끝나면 Framework가 handler와 scope를 정확히 한 번 정리한다.
- 종료를 시작하면 새 dispatch를 막고, 이미 받아들였거나 실행 중인 dispatch가 끝난
  뒤에 handler와 scope를 정리한다.
- Handler가 자기 dispatch 안에서 종료를 요청해도 자기 dispatch의 완료를 기다리는
  순환 대기가 생기지 않는다.

### 3.4 Entry Spot actor packet을 같은 규칙으로 맞춘다

Entry Spot 자신의 direct handler는 Entry Spot activation 수명을 쓴다. Entry Spot에
머무는 Actor의 handler는 Entry Spot과 공유하지 않고 그 Actor의 activation 수명을 쓴다.
[2.2](#22-그래서-net에서-벌어진-일)에서 이 경로만 dispatch마다 새 scope를 열고 있었으므로
Actor activation scope로 바꾼다.

### 3.5 §8.1의 적용 범위를 밝힌다

§8.1의 "root singleton으로 여러 dispatch에서 공유하지 않는다"가 channel handler와
filter에만 해당한다는 것을 문장에 드러내고, Spot·Actor handler는
[3.2](#32-수명-규칙)의 규칙을 따른다고 연결한다. 지금 문장은 모든 handler에
적용되는 것처럼 읽힌다.

## 4. Handler 수명 — 반영 결과

- `06-framework-api.ko.md`에 handler 수명 절을 추가하고 §8.1 적용 범위를 밝혔다.
- `11-spot-model.ko.md`와 `14-actor-model.ko.md`에서 수명 계약을 연결했다.
- 다섯 언어 exact interface와 구현을 같은 의미로 맞췄다.
- `.NET`과 C++ guide의 handler 수명·상태 배치 규칙을 구현에 맞췄다.
- 종료 전에 이미 수락한 dispatch를 먼저 끝내고 정리하는 순서, 그리고 handler가 자기
  dispatch 안에서 종료를 호출해도 순환 대기가 생기지 않는 규칙을 네 runtime에 반영했다.
- 독립 Codex high review에서 나온 P1 네 건과 P2 한 건을 수정했고, 재검토에서 추가
  발견이 없음을 확인했다.
- 추가한 contract test는 다음을 확인한다.
  - 별도 handler class를 쓰는 언어에서, 같은 activation의 같은 handler type은 반복
    dispatch에서 같은 instance로 실행된다.
  - 서로 다른 Actor는 handler 상태나 의존성을 공유하지 않는다.
  - C++ Actor handler는 Spot member function 표현을 유지하면서 Actor별 상태를 Actor
    activation에 둔다.
  - 같은 node 안 Join은 Actor handler를 유지한다.
  - 다른 node로의 Join과 relocation은 target에서 새 handler를 만들고 source를 한 번
    정리한다.
  - Handler type을 DI에 singleton이나 transient로 등록해도 Framework 수명은 바뀌지 않는다.
- test 결과와 문서 검증 수치는
  [execution ledger](v11.0/route-mesh-11.0.0-execution-ledger.ko.md)의
  `2026-07-29 Handler instance 수명 계약 보강` 절에 있다.

## 5. Filter 범위 — 무엇이 문제였나

Handler filter는 handler 앞에 공통 처리를 끼워 넣는 공개 확장 지점이다. 로그, 검증,
권한 확인처럼 여러 handler에 같은 코드가 반복될 일을 한곳에 모으는 데 쓴다.

`06-framework-api.ko.md` §8.1은 filter를 **ChannelName의 send·request dispatch에만**
적용하고 Node direct, Spot, Actor, STREAM session, Logical Multicast, classic fanout에는
적용하지 않는다고 규정한다.

이 경계에는 근거가 적혀 있지 않다. 그리고 제외 목록 안에 성격이 서로 다른 경로가
섞여 있다. classic fanout 구독 handler는 channel send handler와 구조가 같다 — 독립
class이고, dispatch마다 새로 만들어지고, 응답이 없다. 반면 Spot handler는 activation
수명을 쓰고 그 Spot의 실행 순서 안에서 돈다. 두 부류를 같은 이유로 제외할 수 없다.

또한 관제·진단 명령이 오가는 Node direct 경로야말로 감사 로그와 권한 확인이 필요한
자리인데, 현재 규정은 그곳에 공통 처리를 둘 방법을 주지 않는다.

## 6. Filter 범위 — 무엇으로 정했나

### 6.1 Framework root에 등록하는 handler 전부에 적용한다

**filter는 Framework root에 등록하는 process-level handler 전부에서 실행한다.**

| 구분 | 경로 | filter |
| --- | --- | --- |
| Framework root에 등록한다 | RouteMesh channel의 send·request | 실행한다 |
| Framework root에 등록한다 | ClientServer channel의 send·request | 실행한다 |
| Framework root에 등록한다 | classic fanout 구독 | 실행한다 |
| Framework root에 등록한다 | Node direct route handler | 실행한다 |
| 객체가 소유한다 | Spot handler, Actor handler | 실행하지 않는다 |
| 객체가 소유한다 | Spot이 등록하는 Logical Multicast 구독(`Context.Handlers.AddSubscribe`) | 실행하지 않는다 |
| 객체가 소유한다 | STREAM session handler | 실행하지 않는다 |

객체가 소유하는 handler를 제외하는 이유는 filter를 Framework root에 등록한 handler의
공통 정책으로 한정하기 위해서다. 기술적으로는 그쪽에도 filter를 실행할 수 있다.
그러나 root에 한 번 등록한 filter가 Spot이나 Actor 내부의 처리 규칙까지 조용히
바꾸면 어디까지 적용되는지 예측하기 어려워진다. 객체가 소유하는 handler는 자기
activation 수명과 실행 순서를 그대로 쓰고 이 filter 계약에 포함하지 않는다.

### 6.2 dispatch 종류를 filter context에 드러낸다

지금 filter가 받는 정보에는 이 message가 send인지 request인지 fanout인지 구분하는
값이 없다. Filter가 `MeshName`·`ChannelName`이 비었는지 조합해 추측하거나 context의
실제 class type을 검사하게 하면 Framework 내부 표현이 application에 드러난다.

Filter가 경로를 직접 고를 수 있도록 dispatch 종류를 공개 값으로 제공한다. 아래는
공통 동작을 설명하는 `.NET` 예시이며, 다른 언어에 같은 signature를 요구하지 않는다.
정확한 선언은 언어별 exact interface가 소유한다.

```csharp
public enum ZLinkHandlerDispatchKind
{
    NodeDirectSend = 0,
    NodeDirectRequest = 1,
    ChannelSend = 2,
    ChannelRequest = 3,
    ClassicFanout = 4
}

public interface IZLinkHandlerFilterContext : IZLinkMessageContext
{
    // 이 dispatch가 어느 경로로 들어왔는지 알려 준다. filter가 경로별 정책을
    // 고르는 유일한 공개 기준이다.
    ZLinkHandlerDispatchKind DispatchKind { get; }
}

public interface IZLinkHandlerFilter
{
    ValueTask InvokeAsync(
        IZLinkHandlerFilterContext context,  // MeshName·ChannelName·packet·metadata는 그대로 제공한다.
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken);
}
```

이 값은 filter가 받는 context에만 둔다. Filter가 실행되지 않는 Spot·Actor handler의
공통 message context에는 추가하지 않는다. Transport socket 종류, endpoint와 내부
dispatch 표는 계속 공개하지 않는다.

`ChannelSend`와 `ChannelRequest`는 RouteMesh channel과 ClientServer channel을 모두
포함한다. topology 종류마다 값을 늘리지 않는다. 둘을 구분해야 하는 filter는
`MeshName`을 본다.

| dispatch 종류 | `MeshName` |
| --- | --- |
| RouteMesh channel, Node direct | 해당 MeshName을 제공한다 |
| ClientServer channel | 제공하지 않는다 |
| classic fanout | 제공하지 않는다. MeshNode membership을 쓰지 않기 때문이다 |

등록된 MeshName이 아닌 내부 구분 문자열(예: `.NET` 구현이 현재 넣는 `"fanout"`)을
공개 context에 넣지 않는다.

`DispatchKind`를 공통 message context에 두고 Spot·Actor·Logical Multicast 값을 함께
넣는 대안도 검토했다. 이 대안은 filter가 보지도 않는 경로를 filter 분류 계약에
포함시키고, Actor handler는 handler type만으로 이미 send와 request를 구분한다.
그래서 filter 전용 context에만 두는 쪽을 골랐다. Logical Multicast와 classic fanout이
같은 context class를 재사용하는 구현이라도 classic fanout 경로에서만 filter context를
만들면 공개 의미가 섞이지 않는다.

### 6.3 실행 순서와 수명

Filter는 root에 등록한 순서대로 handler 앞에서 실행한다. 첫 filter가 `next`를
호출하면 두 번째 filter가 실행되고, 마지막 filter가 `next`를 호출하면 handler가
실행된다. `next`가 끝나면 등록의 반대 순서로 각 filter의 나머지 코드가 이어진다.

```text
Filter A 앞부분
  -> Filter B 앞부분
       -> Handler
     Filter B 뒷부분
Filter A 뒷부분
```

Handler 하나를 실행하는 dispatch마다 scope를 새로 연다. Framework는 그 scope에서
handler와 각 filter의 instance를 한 번씩 만들고 같은 `Scoped` 서비스 인스턴스를
제공한다. Application이 handler나 filter type에 지정한 DI lifetime은 이 규칙을 바꾸지
않는다.

정상 완료, [6.4](#64-next를-호출하지-않으면-어떻게-되나)의 중단, 예외, cancellation 중
어느 결과로 끝나도 Framework가 만든 instance와 scope는 정확히 한 번 정리한다.
Framework가 `InvokeAsync`에 전달한 cancellation token은 같은 dispatch의 filter와
handler에 함께 전달한다. Cancellation은 이미
시작한 다른 fanout handler의 dispatch를 취소하지 않고, 이미 끝난 dispatch를 되돌리지도
않는다.

### 6.4 `next`를 호출하지 않으면 어떻게 되나

Filter는 `next`를 최대 한 번 호출한다. 호출하지 않으면 그 dispatch는 handler를
실행하지 않고 끝난다. 호출한 쪽에서 관찰하는 결과는 경로마다 다르다.

| dispatch 종류 | 호출한 쪽이 보는 결과 |
| --- | --- |
| Node direct send, Channel send | 그 dispatch만 끝난다. 송신자는 이미 전송이 접수됐다는 결과를 받았으므로 추가로 전달되는 것이 없다. |
| Classic fanout | 그 구독 handler 하나만 끝난다. 같은 message를 받은 다른 구독 handler는 그대로 실행된다. 발행자에게는 아무것도 전달되지 않는다. |
| Node direct request, Channel request | Framework가 `Rejected` 오류 reply를 보낸다. 값이 없다는 이유로 `null`을 정상 업무 reply로 직렬화하지 않는다. |

Filter가 업무 reply를 대신 만들어 돌려주는 기능은 제공하지 않는다. Java·Node.js·C++의
filter는 `next`와 filter 자신이 값을 반환하는 형태를 쓸 수 있지만, 그 반환값은 handler가
만든 결과를 밖으로 전달하는 통로일 뿐이다. `next`를 부르지 않고 임의의 값을 반환해
request reply를 대체하는 동작은 보장하지 않는다. Framework는 `next` 호출 여부를 기록해
request 중단을 `Rejected`로 확정한다.

Filter나 handler가 예외로 끝나면 그 dispatch의 기존 오류 규칙을 그대로 적용한다.
`next`를 호출하지 않은 것은 예외가 아니며 자동 재시도를 시작하지 않는다.

Filter가 `next`를 두 번 이상 호출하면 Framework가 두 번째 호출을 거부한다. Handler를
다시 실행하거나 새 dispatch를 만들지 않는다. 오류 type 이름은 언어별 exact interface가
정하되, 모든 언어에서 application 코드의 잘못으로 분류한다.

### 6.5 classic fanout의 실행 단위

Classic fanout message 하나가 이 node의 구독 handler 여러 개와 일치하면 handler마다
별도 dispatch를 만든다. 각 dispatch는 자기 DI scope, 자기 filter instance와 해당
handler instance를 쓴다. 한 handler에서 filter가 `next`를 호출하지 않거나 handler가
실패해도 다른 handler는 그대로 실행된다.

따라서 filter는 message 하나에 한 번이 아니라 **일치한 handler 수만큼** 실행된다.
무거운 filter를 등록하면 구독자가 많을수록 비용이 그만큼 늘어난다.

## 7. Filter 범위 — 반영 현황

### 7.1 공통 spec

[6](#6-filter-범위--무엇으로-정했나)의 결정을 `06-framework-api.ko.md` §8.1과 `.NET`
exact interface(`server/languages/dotnet/interfaces/03-configuration-topology.ko.md`)에
반영하기 전에 변경안을 검토했다. 정식 spec과 exact interface는 아직 바꾸지 않았다.
검토 결과 다음 네 가지를 함께 반영해야 한다.

**같은 계약을 반대로 말하는 문장이 다른 spec에 남아 있다.** §8.1만 고치면 두 문서가
서로 충돌한다.

| 문서 | 지금 문장 | 고칠 방향 |
| --- | --- | --- |
| `19-stream-session.ko.md` | "Handler filter pipeline은 ChannelName dispatch에만 적용한다. Node direct, Spot, Actor와 STREAM session dispatch에는 적용하지 않는다" | 이 문서가 소유할 내용은 STREAM session을 제외한다는 것 하나다. Node direct를 빼고 나머지 범위는 §8.1 링크에 맡긴다 |
| `04-message-model.ko.md` | "Handler filter는 handler와 같은 current `MessageContext`를 직접 받는다" | filter는 그 message 정보에 더해 [6.2](#62-dispatch-종류를-filter-context에-드러낸다)의 dispatch 분류를 함께 받는다로 고친다 |

**현재 실패 규칙을 반영안에서도 유지한다.** 현재 §8.1의 "Filter 또는 handler에서
발생한 예외는 같은 dispatch 실패 처리 규칙을 따른다"는 문장이 검토한 반영안에서
빠져 있었다. Fanout에서 다른 구독 handler를 취소하지 않는다는 것과 어느 결과로 끝나도
한 번 정리한다는 규칙만으로는 **filter가 던진 예외를 호출한 쪽이 무엇으로 받는지**
알 수 없다. 이 문장과 [6.4](#64-next를-호출하지-않으면-어떻게-되나)의 규칙을 공통
spec에 함께 넣는다.

**설명 없이 들어간 용어를 푼다.** 공통 spec 본문에 `short-circuit`과 `programmer
error`가 뜻을 밝히지 않은 채 쓰였다. 특히 `programmer error`는 재시도 대상이 아니라는
것이 계약의 일부인데 단어만으로는 드러나지 않는다. 오류 이름(`.NET`은
`ZLinkFrameworkErrorKind.InvalidOperation`)은 언어별 exact interface가 소유하므로, 공통
spec은 "handler를 다시 실행하지 않고 오류로 거부한다. 이 오류는 application 코드의
잘못이므로 재시도하지 않는다"처럼 결과로 쓴다. `short-circuit`도 "`next`를 호출하지 않고
끝난 경우"로 바꾼다.

**cancellation token 공유 규칙이 빠졌다.** [6.3](#63-실행-순서와-수명)의 "Framework가
`InvokeAsync`에 전달한 cancellation token을 같은 dispatch의 filter와 handler에 함께 전달한다"는
규칙을 §8.1에 넣어야 한다. 없으면 filter가 handler와 다른 token을 쓸 수 있다고 읽힌다.

### 7.2 `.NET`

filter를 실행하는 `ZLinkHandlerDispatcher`가 어느 경로에 연결되어 있는지 확인한
결과다.

| 경로 | 지금 filter 실행 | [6.1](#61-framework-root에-등록하는-handler-전부에-적용한다) 기준 | 필요한 작업 |
| --- | --- | --- | --- |
| RouteMesh channel send·request | 실행한다 | 적용 | filter 전용 context, request 중단 처리, 중복 `next` 거부 |
| ClientServer channel send·request | 실행한다 | 적용 | filter 전용 context, request 중단 처리, 중복 `next` 거부 |
| classic fanout 구독(`ZLinkFanoutPacketDispatcher`) | 실행한다 | 적용 | filter 전용 context, `MeshName`의 `"fanout"` 제거, 중복 `next` 거부 |
| Node direct route | **실행하지 않는다** — `ZLinkRouteHandlerInvoker`가 filter 없이 handler를 직접 호출한다 | 적용 | filter 연결과 handler scope 구현 |
| Spot·Actor·Logical Multicast 구독·STREAM session | 실행하지 않는다 | 제외 | 없음 |

[6.5](#65-classic-fanout의-실행-단위)의 실행 단위는 이미 그렇게 동작한다.
`ZLinkChannelPublishDispatchPipeline`이 일치한 handler마다 context를 만들고
`DispatchAsync`를 따로 호출하므로 각 호출이 별도 scope와 filter chain을 쓰고, handler
실패 처리도 반복문 안에서 끝나 다른 handler를 막지 않는다. 이 동작은 구현을 바꾸지
않고 contract test로 고정한다.

**공통 pipeline에 필요한 작업.** channel과 classic fanout에 함께 적용한다.

1. Filter가 받는 context를 `IZLinkHandlerFilterContext`로 바꾼다.
2. Dispatch 종류를 pipeline 진입 지점에서 정한다.
3. `next` 호출 여부와 횟수를 기록한다.
4. Request가 중단되면 `null` 대신 `Rejected` 오류 reply를 보낸다.
5. 두 번째 `next` 호출은 handler를 다시 실행하지 않고 거부한다.
6. classic fanout filter context의 `MeshName`을 비운다.

이 변경으로 **기존 .NET filter 구현은 깨진다.** `InvokeAsync`의 첫 인자가
`IZLinkMessageContext`에서 `IZLinkHandlerFilterContext`로 바뀐다. v11.0에서는 이전
context signature를 호환 overload나 adapter로 유지하지 않는다.

**Node direct에 추가로 필요한 작업.** 현재 `ZLinkRouteHandlerInvoker`는 application
service provider의 `GetRequiredService(...)`로 handler를 직접 가져온다. 그래서
application이 등록한 lifetime이 실제 handler 수명을 바꾼다 —
[3.2](#32-수명-규칙)가 금지한 동작이다.

1. Node direct dispatch마다 DI scope를 하나 연다.
2. Framework가 handler와 filter를 그 scope에서 한 번씩 만든다.
3. Handler와 filter type의 application DI lifetime은 Framework 수명을 바꾸지 않는다.
4. Handler와 filter를 정리한 뒤 scope를 정리한다.

### 7.3 다섯 언어

공통 spec 변경은 `.NET` 현황만으로 끝나지 않는다. Java·Kotlin, Node.js와 C++에서
다음을 확인하고 같은 의미로 맞춘다.

- Node direct와 classic fanout이 실제로 filter를 거치는지
- handler 하나마다 새 filter chain과 DI scope를 만드는지
- request 중단이 `Rejected`로 끝나는지
- fanout에서 한 handler의 실패가 다른 handler를 막지 않는지
- filter context가 dispatch 종류를 직접 제공하는지
- handler·filter instance와 의존성이 정확히 한 번 정리되는지

Java·Node.js·C++에서 filter가 값을 반환하는 형태는 유지해도 되지만, 그 값은 `next()`가
만든 handler 결과를 전달하는 데만 사용한다. 임의 업무 reply를 대체하는 기능으로
해석하지 않는다. C++ exact interface의 기존 reply 대체 문장도 제거한다. Kotlin은 Java
runtime과 같은 의미를 공유한다. 구현 형태가 달라도 위 동작은 같아야 한다.

이 변경은 기존 filter 구현을 깨뜨린다.

- C++ exact interface가 보장하던 "filter가 `next()` 결과 대신 새 `message_t`를 반환해
  request reply를 대체한다"는 계약을 제거한다.
- Java·Node.js·C++의 filter가 `next()`를 호출하지 않은 request는 반환값과 관계없이
  `Rejected`로 끝난다.
- 기존에 filter에서 업무 reply를 직접 만들던 application 코드는 handler 또는 별도
  application service로 옮겨야 한다.
- v11.0에서는 reply 대체 동작을 compatibility adapter로 유지하지 않는다.

## 8. Filter 범위 — 남은 작업

[6](#6-filter-범위--무엇으로-정했나)의 설계와 반영안 검토를 마쳤다. 공통 spec, 다섯
언어 exact interface와 구현은 아직 바꾸지 않았다. 아래를 모두 마친 뒤 이 주제를 완료로
표시한다.

1. `06-framework-api.ko.md`와 다섯 언어 exact interface에 적용 범위, filter 전용
   dispatch 종류, 실행 순서, 중단 결과와 fanout 실행 단위를 반영했는가?
2. `19-stream-session.ko.md`와 `04-message-model.ko.md`의 filter 문장을
   [7.1](#71-공통-spec)의 방향으로 고쳐 §8.1과 어긋나지 않게 했는가?
3. filter나 handler가 던진 예외를 호출한 쪽이 무엇으로 받는지 공통 spec에 남겼는가?
4. `short-circuit`과 `programmer error`를 결과가 드러나는 문장으로 바꿨는가?
5. `InvokeAsync`에 전달한 cancellation token을 같은 dispatch의 filter와 handler에 함께
   전달한다는 규칙을 §8.1에 넣었는가?
6. 기존 filter 구현이 바꿔야 하는 context 인자와 C++·Java·Node.js의 reply 대체 계약
   제거를 언어별 migration 안내에 breaking change로 적었는가?
7. `.NET` channel과 classic fanout에
   [7.2](#72-net)의 공통 pipeline 여섯 항목을 구현했는가?
8. `.NET` Node direct handler와 filter를 공통 dispatch scope에 연결했는가?
9. Java·Kotlin, Node.js와 C++의 Node direct·fanout 구현을 같은 계약으로 맞췄는가?
10. 각 언어 contract test가 다섯 dispatch 종류와 request `Rejected`를 확인하는가?
11. fanout에서 두 handler 중 하나만 중단되거나 실패해도 다른 handler가 실행되는지
    확인하는가?
12. handler·filter instance와 의존성의 생성·정리가 dispatch마다 정확히 한 번인지
    확인하는가?
13. 공통 spec, exact interface, guide, implementation gap과 execution ledger를 갱신했는가?

`dotnet/guide/05-channel-messaging.ko.md`의 filter 절은 현행 §8.1(ChannelName
send·request 전용)에 맞춰 두었다. 1번을 마친 뒤 이 문서의 결정으로 바꾼다.
