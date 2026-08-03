# Framework 공통 internals — 계획 문서 묶음

[Framework 공통 내부 구조](../../framework/common/internals/README.ko.md)를 실제 구현으로
옮기기 위한 문서들이다. internals가 "네 runtime이 무엇을 같게 해야 하는가"를 정하고,
여기서는 "지금 무엇이 다르고, 각 언어가 무엇을 어떤 순서로 고칠 것인가"를 다룬다.

## 읽는 순서

| 순서 | 문서 | 언제 보는가 |
|---|---|---|
| 1 | [구현 갭 목록](framework-internals-implementation-gaps.ko.md) | 무엇이 다른지 전체를 볼 때. **모든 판정의 정본** |
| 2 | 구현 계획 (아래) | 무엇을 어떤 순서로 할지 정할 때 |
| — | [spec 확장 제안](framework-spec-extension-proposals.ko.md) | spec을 왜 그렇게 고쳤는지 근거가 필요할 때 |

## 이 작업의 배경

기존 internals 9개 문서는 머리말에 "**목표 구조**를 설명한다. 현재 구현과의 차이는
execution ledger가 소유한다"고 적혀 있었다. 구현이 아니라 목표를 적은 문서이며 구현과
다를 수 있음을 스스로 명시했다. 언어별 `{lang}/internals/`도 문서 집합이 서로 달랐고 최소
한 건은 코드와 불일치했다.

그래서 **네 언어 구현 실측과 정식 spec 대조를 근거로** 12개 문서로 재작성했다. 판정
우선순위는 다음과 같다.

1. **spec** — 정식 spec이 정한 것은 그대로 따른다. 구현이 다르면 구현이 갭이다.
2. **구현 실측** — spec이 침묵하면 네 구현 중 실제로 나은 쪽을 고른다.
3. **POSD** — 위 둘이 갈리지 않을 때 모듈 구조 건강성으로 고른다.


## 구현 계획

**Core를 먼저 진행하고, 그 뒤에 언어별 framework 작업을 시작한다.**

> **순서의 근거를 정정한다.** 처음에는 "C++ ClientServer가 Core에 위임하므로 Core가
> 선행"이라고 적었으나 **틀렸다.** 정식 ClientServer 경로는 네 언어 모두 framework가
> 고르며, Core에 위임하는 것은 C++의 fallback 경로 하나다. **Core 수정으로 닫히는
> framework 갭은 없다.**
>
> 그래도 Core를 먼저 하는 이유는 다음이다.
>
> - Core 동작이 바뀌면 **bindings를 다시 배포해야** framework가 그 변경을 본다. 배포가
>   framework 작업보다 앞서야 소비자 참조가 꼬이지 않는다.
> - Core 변경은 framework 4언어와 독립적이라 병렬 대기 없이 진행할 수 있다.
> - `ZLINK_DEALER_OPT_WEIGHT`는 framework와 무관하게 공개된 Core 기능이다.

| 순서 | 대상 | 문서 | 가장 무거운 것 |
|---|---|---|---|
| **0** | **Core** | [dealer 가중 선택 순서](dealer-weighted-selection-order.ko.md) | 구현·spec **완료**. current source는 `11.2.0`이며 bindings package·consumer provenance가 남았다 |
| 1 | C++ | [cpp](cpp-implementation-plan.ko.md) | scheduler 재설계 + Core 위임 경로 정리 |
| 1 | .NET | [dotnet](dotnet-implementation-plan.ko.md) | scheduler 두 축 회계 + 관찰자 stream 재작성 |
| 1 | Java | [java](java-implementation-plan.ko.md) | **public error ABI 교체** + queue 한도 도입 |
| 1 | Node | [node](node-implementation-plan.ko.md) | queue 한도가 아예 없음 — 실행 구조 도입 |

네 언어는 서로 독립적이라 동시에 진행할 수 있다. **Core 배포(§7)가 끝난 뒤 시작한다** —
현재 candidate source의 Core version은 `VERSION`과 `core/CMakeLists.txt`가 소유하는 `11.2.0`이다.
그러나 모든 binding package와 clean consumer가 같은 candidate provenance를 확인한 상태는 아니므로,
이 gate가 닫히기 전에는 framework 작업 완료를 판정하지 않는다.

**W2-RM은 어느 언어에서도 선행 과제가 없다.** 다른 묶음을 기다리는 동안 먼저 진행할 수 있다.

## 작업 묶음

언어별 문서는 아래 묶음을 공유한다. 같은 코드를 고치는 항목을 따로 진행하면 같은 자리를
여러 번 다시 설계하게 된다.

| 묶음 | 내용 |
|---|---|
| W1 scheduler | 제출 → 두 축 예약 → lane 선택 → claim → 점유 상한 → handler → 회계 반납 → public 오류 |
| W2-RM selector | RouteMesh 선택 절차·tiebreak. **선행 과제 없음 — 즉시 진행** |
| W2-CS selector | ClientServer 선택 절차. per-server 연결 구조는 유지 |
| W3 relay 통지 | wire command 정의가 선행 |
| W4 유휴 정리 | exact interface + Location Store CAS + admission seal + timer 구조 |
| W5 관찰자 | source 모델 + 합치기 + terminal 보관 + 유실 counter + 전달 envelope |
| W6 수신 공정성 | topology 전 경로 batch 상한과 회전 cursor |
| W7 오류 ABI | 공통 13-kind 정합, 무음 드롭 제거 |

## 시작 전에 정해야 하는 것

아래가 정해지기 전에는 해당 묶음을 시작할 수 없다. 전부
[구현 갭 목록](framework-internals-implementation-gaps.ko.md)의 선행 과제 절에 있다.

| 판정 | 막고 있는 묶음 |
|---|---|
| relay 통지 wire command | W3 전체 |
| `IdleEvicted` enum 수치와 timeout 설정을 네 exact interface에 | W4 |
| 관찰자 전달 envelope를 네 exact interface에 | W5 |
| 실행 대기열 기본 한도와 작업당 고정 비용 | W1 |
| 상한 값 6종 — 측정 후 판정 | W1 · W6 |

Core 쪽 수정은 [Core DEALER 가중 선택 순서 수정](dealer-weighted-selection-order.ko.md)이
소유한다.
