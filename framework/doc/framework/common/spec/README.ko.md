# ZLink Framework 스펙

이 디렉토리는 framework의 공통 의미 계약과 언어별 공개 계약을 함께 관리한다.
공통 스펙은 모든 언어가 제공해야 하는 동작을 정의하고, 언어별 스펙은 그 동작이
각 언어에서 보이는 정확한 public API를 정의한다.

## 공통 의미 계약

| 문서 | 범위 |
|------|------|
| [공개 계약 관리](public-contract-governance.ko.md) | 정식 계약, draft, 변경 통제와 검증 규칙 |
| [언어별 구현 차이](implementation-gap.ko.md) | 정식 spec과 현재 `.NET`, Java/Kotlin, Node.js, C++ 구현의 차이 |
| [비동기 실행과 coroutine 정책](async-execution-policy.ko.md) | 비동기 완료, 취소, coroutine 투영 규칙 |
| [framework API](framework-api.ko.md) | framework 역할과 등록 표면의 공통 기준 |
| [Actor 모델](actor-model.ko.md) | actor lifecycle과 호출 의미 |
| [Spot Actor Join / Transfer](spot-actor.ko.md) | actor 이동과 callback 순서 |
| [Session Actor Dispatch](session-actor-dispatch.ko.md) | session에서 actor로 전달하는 공통 계약 |

나머지 기능별 공통 스펙도 이 디렉토리에 있으며, 특정 언어의 타입이나 문법을
공통 계약으로 강제하지 않는다.

공통 문서에 이해를 돕는 언어별 예시가 있더라도 그 예시는 비규범 설명이다. 정확한
public 타입과 시그니처는 `languages/<lang>/` 문서만 소유한다.

## 언어별 공개 계약

| 언어 | 정식 스펙 |
|------|-----------|
| `.NET` | [languages/dotnet](languages/dotnet/README.ko.md) |
| Java | [languages/java](languages/java/README.ko.md) |
| Kotlin | [languages/kotlin](languages/kotlin/README.ko.md) |
| Node.js / TypeScript | [languages/node](languages/node/README.ko.md) |
| C++ | [languages/cpp](languages/cpp/README.ko.md) |

언어별 디렉토리의 정식 스펙은 각 언어가 제공해야 하는 목표 public contract를
고정한다. 현재 구현이 정식 스펙과 다르면 정식 스펙을 현재 코드에 맞춰 축소하지
않고 [언어별 구현 차이](implementation-gap.ko.md)에 기록한 뒤 구현이 스펙을
따르도록 한다.
