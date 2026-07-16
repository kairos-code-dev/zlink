# Framework 공개 계약 관리

[스펙 목차](README.ko.md) · [다음: 개요](01-overview.ko.md)

## 1. 목적

이 문서는 ZLink Framework 10.0.0 공개 계약의 소유권과 검증 규칙을 정의한다. 공개 계약은
사용자가 호출할 수 있는 타입과 operation뿐 아니라 timeout, 취소, 오류, callback 순서, ownership과
완료 조건을 포함한다.

## 2. 계약 소유권

공개 계약은 공통 의미와 언어별 표현으로 나눈다.

| 위치 | 소유하는 계약 |
|---|---|
| 이 디렉토리와 package별 공통 스펙 | 언어와 무관한 기능, 상태, 완료 조건, 오류 의미 |
| package의 `languages/<lang>/` | 실제 public 타입, 메서드 시그니처, generic·nullable 규칙, 언어별 비동기 표현 |
| Core 10.0.0 스펙 | MeshNode, Spot, Actor, STREAM session과 raw socket의 하위 계약 |

공통 스펙은 특정 언어의 문법을 표준으로 삼지 않는다. 각 언어는 같은 기능과 관찰 가능한 결과를
자기 언어의 관례로 표현한다. .NET RouteMesh·MeshNode의 정확한 시그니처는
[.NET RouteMesh·MeshNode 인터페이스](server/languages/dotnet/05-route-mesh.ko.md)가 소유한다.

## 3. 공통 계약의 필수 항목

공개 기능을 정의할 때 다음 항목을 함께 고정한다.

- 기능이 속하는 package와 runtime owner
- operation 입력, 결과와 유효한 호출 시점
- timeout, cancellation과 backpressure 의미
- callback 실행 순서와 직렬화 범위
- 메시지와 결과 객체의 ownership
- 설정 오류와 runtime 오류의 구분
- 자동 discovery와 manual peer의 선택 기준
- contract test와 E2E에서 관찰할 결과

함수 이름만 나열해서는 계약이 완성되지 않는다. 사용자가 성공으로 판단할 수 있는 시점과 실패를
받는 위치까지 설명해야 한다.

## 4. 공개 계약 절차

공개 계약을 추가하거나 바꿀 때는 다음 순서를 따른다.

1. 공통 기능과 사용자가 관찰하는 결과를 공통 스펙에 기록한다.
2. 영향을 받는 각 언어 스펙에 정확한 public interface를 기록한다.
3. 현재 checkout과 목표 계약의 차이를 임시 전환 inventory에 기록한다. 정식 스펙은 이 inventory를
   참조하거나 구현 진행 상태를 설명하지 않는다.
4. Core 또는 bindings 계약이 필요하면 해당 package의 정식 스펙을 먼저 맞추고 public header와 구현을
   그 계약에 맞춘다.
5. contract test, 공통 E2E와 sample이 공개 표면만 사용하는지 검증한다.
6. 배포 package의 실제 export와 문서의 시그니처를 대조한다.
7. 독립 리뷰에서 계약, 구현, test와 package 사이의 차이가 없음을 확인한 뒤 변경을 승인한다.

공통 E2E와 다른 언어 코드는 계약 해석을 검증하는 자료다. 그 자체만으로 public interface를 만들지는
않는다. public interface는 반드시 정식 스펙에 근거해야 한다.

## 5. 언어별 표현 원칙

언어별 인터페이스는 기능을 같게 유지하면서 다음 관례를 따른다.

- .NET은 `Task`, `ValueTask`, `CancellationToken`과 DI 관례를 사용한다.
- Java는 Java type system과 `CompletionStage` 관례를 사용한다.
- Kotlin은 coroutine을 제공하는 표면에서 `suspend`, `Flow`와 coroutine 취소 규칙을 사용한다.
- Node.js는 `Promise`, TypeScript optional 표현과 필요한 장기 operation의 `AbortSignal`을 사용한다.
- C++는 명시적인 ownership, value type과 coroutine 규칙을 사용한다.

한 언어의 타입 이름과 overload 구성을 다른 언어에 그대로 복제하지 않는다. 기능, 완료 조건과 오류의
의미가 같으면 같은 공통 계약을 투영한 것으로 본다.

## 6. 설계 검토 기준

새 public interface는 호출자가 알아야 하는 결정을 줄여야 한다.

- node direct, channel select-one과 Logical Multicast는 선택과 submit을 하나의 operation으로 제공한다.
- transport endpoint, peer 선택, packet encoding과 reply correlation은 runtime이 소유한다.
- Spot, Actor와 STREAM session의 주소와 generation은 typed handle이나 context가 보존한다.
- 같은 기능을 이름만 달리한 interface로 반복하지 않는다.
- 유효하지 않은 상태 조합을 여러 nullable 값과 boolean으로 나누어 표현하지 않는다.
- timeout이나 metadata처럼 operation마다 허용 범위가 다른 설정은 해당 call object에만 둔다.

비자명한 설계는 두 가지 이상을 비교하고, public interface가 더 작으며 transport 지식이 덜 노출되는
방식을 선택한다.

## 7. 검증

각 언어의 contract test는 최소한 다음을 확인한다.

- 외부 package에서 import할 수 있는 public export
- public 타입과 메서드 시그니처
- generic, nullable, optional, 기본값과 overload
- 비동기 결과, timeout과 취소
- 공개 오류 kind와 lifecycle callback
- MeshName, ChannelName, RID와 owner별 메시징 계약
- Redis location store의 명시 등록과 manual peer 구성

검증은 source tree만 보지 않는다. 실제 배포 package를 외부 consumer가 참조해 같은 public surface와
동작을 얻는지 확인한다.
