# Framework 공개 계약 관리

[스펙 목차](README.ko.md) | [다음: ZLink Framework Overview](01-overview.ko.md)

## 1. 계약 소유권

framework 공개 계약은 두 층으로 나눈다.

- 이 디렉토리의 공통 스펙은 언어와 무관한 기능, 동작, 오류, 완료 조건을 소유한다.
- `languages/<lang>/` 아래의 언어별 스펙은 실제 public 타입, 메서드 시그니처,
  generic 제약, nullable 규칙과 언어별 비동기 표현을 소유한다.

공통 스펙은 framework 사용자가 관찰하는 기능, 호출 흐름, 완료 조건과 오류 의미를
정의한다. 언어별 스펙은 이 의미를 각 언어의 관례에 맞는 정확한 시그니처로 고정한다.
특정 host framework에만 필요한 통합, 언어 전용 타입과 우발적으로 노출된 내부 구현은
공통 기능으로 다루지 않는다.

정식 스펙은 모든 언어가 도달해야 하는 목표 계약이다. 현재 구현이 없거나 다른
시그니처를 제공하더라도 스펙을 구현에 맞춰 축소하지 않는다. 차이는
[언어별 구현 차이](90-implementation-gap.ko.md)에 기록하고, 이후 코드와 contract test를
정식 스펙에 맞춘다.

## 2. 정식 계약 고정

언어별 정식 스펙에 기록된 public contract는 일반 기능 구현, 버그 수정, 내부
리팩터링에서 변경하지 않는다. 다음 항목은 모두 공개 계약 변경이다.

- public 타입, 메서드, property, field, constructor를 추가하거나 제거하는 변경
- 이름, 인자 순서, 인자 타입, 반환 타입과 generic 제약을 바꾸는 변경
- nullable, optional, default value와 overload 조합을 바꾸는 변경
- interface 구현자가 새로 구현해야 하는 abstract member를 추가하는 변경
- timeout, cancellation, callback 순서, 예외와 오류 결과의 의미를 바꾸는 변경
- package, namespace, module export와 사용자가 import하는 경로를 바꾸는 변경

소스 호환성이 유지되는 추가처럼 보여도 public surface가 달라지면 계약 변경으로
분류한다. 특히 interface의 새 member와 callback 인자 추가는 기존 구현체를 깨뜨릴 수
있으므로 내부 리팩터링에 포함하지 않는다.

## 3. 계약 변경 절차

공개 계약을 추가하거나 바꾸면 다음 순서를 지킨다.

1. 공통 기능, 호출 흐름과 관찰 가능한 결과를 확인한다.
2. 공통 스펙에 언어와 무관한 목표 계약을 먼저 반영한다.
3. 언어별 스펙에 해당 언어의 관례를 따른 정확한 목표 인터페이스를 반영한다.
4. 언어별 스펙과 현재 public surface를 비교해 구현 차이를 기록한다.
5. 구현과 contract test를 정식 스펙에 맞춘다.
6. 모든 언어의 차이가 닫힌 뒤 공통 E2E와 배포 산출물의 public surface를 검증한다.

정식 스펙과 코드의 차이는 구현 차이다. 문서를 현재 코드에 맞춰 조용히 바꾸거나,
한 언어에만 임시 public API를 추가해 차이를 숨기지 않는다.

## 4. 언어별 표현 원칙

모든 언어는 공통 기능과 사용자가 관찰하는 결과를 같은 수준으로 제공해야 한다.
다만 public API의 문법과 타입은 각 언어의 표준 관례를 따른다.

- 한 언어의 비동기 결과와 취소 타입을 다른 언어에 이름과 모양까지 그대로 복제하지
  않는다.
- Java는 Java의 type system과 `CompletionStage` 계약을 사용한다.
- Kotlin은 Java runtime을 공유하더라도 Kotlin 전용 표면에서는 `suspend`, `Flow`,
  coroutine 취소 규칙을 우선한다.
- Node.js framework와 TypeScript browser connector는 `Promise`, 필요한 장기 작업의
  `AbortSignal`과 TypeScript의 optional 표현을 사용한다.
- TypeScript browser connector의 정확한 public interface는 `languages/typescript/`가 소유한다.
  Node.js framework 계약에는 browser client connector signature를 함께 기록하지 않는다.
- C++는 C++의 ownership, value type과 coroutine 규칙을 사용한다.

언어 관례 때문에 시그니처가 달라도 기능, 완료 조건, timeout과 오류의 관찰 결과가
공통 스펙과 같으면 같은 계약을 투영한 것으로 본다. `CancellationToken` 타입은
`.NET` framework의 언어별 계약에만 둔다. 다른 언어가 취소를 제공할 때는
`AbortSignal`, coroutine lifecycle, 표준 중단 타입처럼 해당 언어에서 사용하는
관례를 따른다. 모든 callback에 취소 인자를 넣는 방식으로 모양을 복제하지 않는다.

## 5. POSD 검토 기준

새 public interface를 확정할 때는 같은 기능을 두 가지 이상으로 설계하고 호출자가
알아야 하는 결정의 수를 비교한다. 다음 항목은 framework 공통 계약의 필수 검토 기준이다.

- transport 주소, stale 갱신, 실행 줄과 dispatch 최적화는 framework 내부에 둔다.
- 호출자가 선행 조회와 후속 복구를 정해진 순서로 수행해야 하는 API는 시간적 분해로
  보고, 하나의 capability나 operation이 순서를 소유할 수 있는지 먼저 검토한다.
- 기능은 같고 이름만 다른 nominal interface를 반복하지 않는다. capability별 instance를
  분리해야 해도 같은 계약이면 같은 interface를 재사용한다.
- 유효하지 않은 상태에서 실패하는 getter보다 해당 상태의 handler 인자나 명시적 상태
  값을 사용한다.
- 같은 상태를 nullable 값과 boolean처럼 독립된 두 값으로 표현하지 않는다. 결과의 경우
  sealed hierarchy나 tagged union을 사용해 유효한 상태만 만들 수 있게 한다.
- typed message의 packet identity는 registration descriptor가 소유한다. call site,
  payload instance와 handler가 같은 이름 결정 규칙을 반복하지 않는다.
- public call object는 operation별로 허용되는 설정을 제한하고 transport 조립, timeout,
  cancellation과 cleanup을 숨길 때만 유지한다. 설정과 완료 의미가 완전히 같은 단순
  one-way call은 공통 계약을 재사용한다. metadata, reply, timeout, cancellation처럼
  허용 capability가 다르면 같은 `submit` 이름을 쓴다는 이유만으로 합치지 않는다.

call builder 자체는 제거하지 않는다. 직접 메서드와 options 인자로 평면화하면 모든
호출자가 timeout, metadata, cancellation 조합을 다시 이해해야 하기 때문이다. 대신
send, request처럼 관찰 결과가 다른 operation만 별도 계약으로 유지하고 packet identity와
실행 방식 선택은 builder에서 제거한다.

## 6. 검증

문서는 계약의 기준이고 contract test는 실제 배포 public surface가 그 기준을
지키는지 검증하는 장치다. 언어별 검증은 최소한 다음을 확인해야 한다.

- 외부 사용자가 import할 수 있는 public 타입과 export 목록
- 전체 interface와 public method 시그니처
- generic, nullable, optional, default parameter와 overload
- 비동기 반환 타입과 해당 언어별 스펙이 정의한 취소 인자
- 공개 오류 타입과 lifecycle callback

정식 스펙과 구현이 다르면 해당 항목을 구현 완료로 판정하거나 release 완료로
표시하지 않는다. 문서 작업에서는 먼저 목표 계약과 구현 차이를 확정하고, 후속 코드
작업에서 contract test와 배포 산출물을 같은 기준으로 갱신한다.
