# Framework 문서 구조 정리 계획

작성일: 2026-07-10

이 문서는 framework 문서에서 초기 아이디어와 중복 설명을 제거하고, 정식 문서의
책임을 `spec`, `guide`, `sample`, `e2e`, `internals`로 다시 나누기 위한 작업
계획이다. 구현이 끝나기 전까지 사용하는 임시 문서이며, 정식 framework 계약이
아니다.

이 계획을 모두 적용하고 검증한 뒤에는 이 문서를 삭제한다. `framework/doc/plan/`
아래의 다른 문서도 구현 작업을 위한 임시 자료이므로, 이번 작업에서는 그 문서들이
가진 이전 경로 참조를 갱신하지 않는다. 완료 후 불필요해진 plan 문서는 별도 보존하지
않고 삭제한다.

## 1. 목표

- framework의 언어 공통 계약은 `common/spec`이 소유한다. 다만 저장소 전체의
  공개 C API 계약은 `core/include/zlink.h`와 루트 `doc/spec/`이 소유하므로,
  framework 문서가 그 계약을 다시 정의하거나 넓히지 않는다.
- 실제 업무 흐름의 정본 설명은 `common/sample`에 둔다.
- 공통 구현 검증 요구사항은 `common/e2e`에 둔다.
- 언어별 공개 API 계약은 각 언어의 `spec`에 둔다.
- 언어별 사용법과 실행 예제 설명은 각 언어의 `guide`와 `guide/samples`에 둔다.
- 유지보수자만 알아야 하는 내부 구조와 검증 정책만 언어별 `internals`에 둔다.
- 초기 아이디어, 구현되지 않은 응용 분야 설명, 완료된 구현 계획은 정식 문서에서
  제거한다.
- 같은 의미를 여러 문서에서 반복하지 않고, 책임 문서 한 곳에서만 설명한다.

## 2. 문서별 책임

| 위치 | 독자 | 남길 내용 | 넣지 않을 내용 |
|------|------|-----------|---------------|
| `common/spec` | 모든 framework 언어의 구현자와 계약 검토자 | 언어 공통 의미, 동작, 제약 | 사용 예제, 초기 아이디어, 언어별 API 모양 |
| `<lang>/spec` | 해당 언어 사용자와 구현자 | 공개 타입, 함수, 오류, lifecycle 계약 | 내부 클래스와 실행 배선, CI 계획 |
| `<lang>/guide` | 해당 언어 사용자 | 기능의 목적, 선택 기준, 사용 방법 | 공개되지 않은 내부 구현 |
| `common/sample` | 모든 언어의 sample 구현자 | 정본 업무 시나리오와 완료 조건 | 구현되지 않은 응용 분야 아이디어 |
| `<lang>/guide/samples` | 해당 언어 사용자 | 정본 시나리오의 언어별 실행 방법 | 별도 업무 시나리오 정의 |
| `common/e2e` | 모든 언어의 검증 담당자 | 공통 검증 요구사항 | 새 public API의 근거 |
| `<lang>/internals` | 해당 언어 framework 유지보수자 | backend 경계, 내부 실행 구조, CI와 회귀 정책 | 사용자가 따라야 할 공개 계약 |

`sample`과 `e2e`는 public contract를 검증하고 설명하지만, 그 자체만으로 새 public
API를 만들지는 않는다. 새 API가 필요하면 먼저 공통 spec 또는 해당 언어 spec에서
계약 근거를 확인한다.

### 2.1 계약 근거의 우선순위

문서를 옮기거나 삭제할 때는 다음 순서로 계약 근거를 확인한다.

1. core 또는 binding 공개 API에 관한 내용은 `core/include/zlink.h`와 루트
   `doc/spec/`의 현재 계약을 먼저 확인한다.
2. framework 공통 의미는 `framework/doc/framework/common/spec/`에서 확인한다.
3. 언어별 타입, 함수, 오류와 lifecycle은 해당 언어의 `spec`에서 확인한다.
4. 공통 `sample`은 사용자가 따라 할 정본 업무 흐름이고, 공통 `e2e`는 구현 검증
   요구사항이다. 둘 다 누락을 찾는 근거로 사용하지만 새 public API의 단독 근거로
   사용하지 않는다.
5. 다른 언어의 구현과 public surface는 계약 해석을 비교하는 증거일 뿐, 그 자체를
   새 계약의 출처로 삼지 않는다.

공통 guide에 이미 승인된 사용 원칙이 있으면 계약 해석에 함께 사용한다. guide의
예제가 spec과 충돌하면 예제를 근거로 계약을 바꾸지 않고, spec과 실제 공개 API를
기준으로 guide를 고친다.

framework의 언어별 문서는 모두 `framework/doc/` 아래에서 정리한다. 이 작업 중
`framework/languages/<lang>/doc/` 아래에 새 문서를 만들지 않는다. 기존 문서 위치를
바꿔야 해도 `framework/doc/framework/<lang>/`의 책임 디렉토리를 사용한다.

## 3. 작업 범위에서 제외하는 항목

- `framework/doc/plan/` 아래 기존 문서의 링크와 경로 참조는 갱신하지 않는다.
- plan 문서의 과거 목록을 정식 문서의 깨진 링크 검사 결과에 포함하지 않는다.
- 문서 위치를 바꾸기 위해 public API나 runtime 동작을 임의로 변경하지 않는다.
  다만 삭제 대상인 gap 목록이나 open-item 문서에서 실제 미구현 계약을 발견하면,
  공통 spec에 근거가 있는지 먼저 확인한 뒤 코드·테스트·E2E·sample까지 같은 작업에서
  완료한다. 계약 근거가 없으면 새 API를 만들지 않고 해당 아이디어를 폐기한다.
- 삭제하는 아이디어를 보존하기 위해 새 sample, helper, public API를 만들지 않는다.
- 다른 언어에만 구현되어 있다는 이유로 공통 spec에 계약을 추가하지 않는다.
- 현재 작업 트리의 관련 없는 코드와 문서 변경은 수정하거나 되돌리지 않는다.

### 3.1 구현이 필요한 gap을 발견했을 때의 제한

이 문서 정리 과정에서 실제 구현 gap을 발견하더라도 문서 삭제를 통과시키기 위한
우회 코드를 만들지 않는다.

- 먼저 기존 public API와 표준 사용 패턴으로 해결할 수 있는지 확인한다.
- 새 helper, adapter, wrapper, option, public method를 추가하기 전에 같은 책임의
  기존 표면이 있는지 조사한다.
- raw buffer 해석, 호출부의 별도 encode/decode, handler별 codec 등록, test 전용
  adapter로 framework 책임을 사용자 코드나 sample에 밀어내지 않는다.
- 기본 메시지 직렬화는 framework의 typed JSON serializer 경로를 사용한다.
  메시지별 codec 등록 API를 새로 만들거나 제거된 API를 되살리지 않는다.
- .NET framework가 binding 기능을 필요로 하면 binding의 public API를 사용한다.
  reflection이나 `InternalsVisibleTo`로 internal/private 멤버 접근을 우회하지 않는다.
- binding package나 참조 버전을 바꾸는 경우
  `scripts/local-package/README.ko.md`의 배포 위치와 언어별 버전 고정 규칙을 따른다.
- 비자명한 설계 변경은 두 가지 이상의 대안을 비교하고, 호출자 복잡성과 정보 누출이
  더 적은 안을 선택한 근거를 남긴다.

수정 후에는 패스스루 메서드, 실행 순서에 따라 쪼갠 클래스, 인터페이스와 구현의
복잡도가 비슷한 얕은 모듈, 특수 코드와 범용 코드의 혼합, 코드를 반복하는 주석이
새로 생기지 않았는지 다시 검토한다. 발견한 위험 신호는 해당 POSD 원칙, 검토한 두
가지 이상의 대안, 선택 이유, 수정 후 해소 근거를 함께 남긴다.

기존 표면으로 해결할 수 없고 새 공개 계약이 필요하면 즉시 구현하지 않는다. 먼저
계약 초안을 별도 draft로 분리해 리뷰받는다. core 공개 계약을 바꾸는 초안은
`doc/spec/draft/` 규칙을 따르며, 첫머리에 구현 전 초안이고 현재 공개 계약이 아님을
명시한다. 승인과 구현이 끝난 뒤에만 현재 코드, 공개 헤더, 테스트, 오류 문서,
binding 문서와 맞춰 정식 spec으로 나누어 반영한다.

## 4. 공통 use-case 문서 제거

### 4.1 삭제 대상

다음 디렉토리와 그 안의 문서를 모두 삭제한다.

```text
framework/doc/framework/common/use-cases/
```

현재 삭제 대상은 README와 다음 아홉 문서다.

- `01-service-to-service-rpc.ko.md`
- `02-playhouse-play-to-api.ko.md`
- `03-worker-dispatch.ko.md`
- `04-domain-event-fanout.ko.md`
- `05-cache-invalidation-and-config-refresh.ko.md`
- `06-stage-state-sync.ko.md`
- `07-real-time-notification-fanout.ko.md`
- `08-scatter-gather-query.ko.md`
- `09-workflow-orchestration.ko.md`

다음 문서도 함께 삭제한다.

```text
framework/doc/framework/common/spec/usecase-validation.ko.md
```

이 문서는 구현 계약을 검증하지 않고 초기 use-case 설명의 존재 여부를 확인하므로,
정식 spec으로 유지하지 않는다.

### 4.2 정식 문서 참조 정리

`framework/doc/plan/`을 제외하고 다음 문서에서 삭제 대상 링크와 설명을 제거한다.

- `framework/doc/README.ko.md`
- `framework/doc/framework/common/README.ko.md`
- `framework/doc/framework/common/perf/README.ko.md`
- `framework/doc/framework/common/spec/channel-topology.ko.md`
- `framework/doc/framework/common/spec/framework-api.ko.md`
- `framework/doc/framework/common/spec/interaction-model.ko.md`
- `framework/doc/framework/common/spec/message-model.ko.md`
- `framework/doc/framework/common/spec/overview.ko.md`
- `framework/doc/framework/common/spec/spot-address-messaging.ko.md`
- `framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md`
- `framework/doc/framework/node/internals/regression-test-matrix.ko.md`

`doc/principal/documentation/documentation-principles.ko.md`에 use-case 문서를 정식
입력처럼 설명하는 문장이 있으면, `common/sample`의 정본 시나리오와 공통 spec을
기준으로 읽도록 고친다. 그래야 이후 작업에서 삭제한 디렉토리를 다시 만들지 않는다.

### 4.3 내용 정리

링크만 지우고 초기 개념을 공통 spec에 남겨 두지 않는다. 다음 개념이 현재 구현과
공개 계약에 근거하지 않는다면 관련 표, 절, 확장 예고를 함께 제거한다.

- worker dispatch
- scatter-gather aggregate helper
- workflow orchestration
- 구현되지 않은 별도 조합 모델

현재 계약으로 구현된 request/response, command, publish/subscribe, stream,
actor/Spot 의미는 유지한다. 삭제할 개념과 이름이 비슷하다는 이유만으로 구현된
기능까지 제거하지 않는다.

## 5. 공통 sample을 정본 시나리오로 정리

`framework/doc/framework/common/sample/README.ko.md`는 다음 여섯 sample을 정본
업무 시나리오로 안내한다.

- Bingo
- TicTacToe
- SupportChat
- DeliveryDispatch
- ShoppingMall
- GameQuest

공통 README와 각 언어 README의 문서 읽기 순서는 다음처럼 정리한다.

1. 공통 의미와 제약은 `common/spec`에서 확인한다.
2. 실제 업무 흐름은 `common/sample`에서 확인한다.
3. 언어별 API는 `<lang>/spec`에서 확인한다.
4. 언어별 실행 방법은 `<lang>/guide/samples`에서 확인한다.
5. 구현 검증 조건은 `common/e2e`에서 확인한다.

기존 use-case 문장을 sample로 기계적으로 복사하지 않는다. sample 코드와 실행
검증으로 뒷받침되는 흐름만 정본 sample 설명에 남긴다.

## 6. 언어별 case-studies 제거

### 6.1 삭제 대상

다음 네 디렉토리를 모두 삭제한다. `.NET`만 삭제해서 언어별 문서 구조가 다르게
남지 않도록 Java, Kotlin, Node.js에도 같은 결정을 적용한다.

```text
framework/doc/framework/dotnet/guide/case-studies/
framework/doc/framework/java/guide/case-studies/
framework/doc/framework/kotlin/guide/case-studies/
framework/doc/framework/node/guide/case-studies/
```

각 디렉토리에 있는 전자상거래, 마이크로서비스 연결, 실시간 게임, 배차, 채팅,
거래 시스템 문서를 모두 삭제한다.

### 6.2 시나리오 연결 원칙

실행 가능한 정본 sample이 있는 분야는 다음처럼 직접 안내한다.

| 기존 설명 분야 | 정본 sample |
|----------------|-------------|
| 전자상거래와 보상 처리 | ShoppingMall |
| 배차와 재배정 | DeliveryDispatch |
| 고객 지원 대화와 재연결 | SupportChat |
| 실시간 게임과 방 단위 상태 | Bingo, TicTacToe, GameQuest |

마이크로서비스 연결 방식은 특정 응용 분야 case study로 유지하지 않는다. 공통 spec의
channel/location 계약과 실제 sample 및 E2E에서 필요한 범위만 설명한다.

거래 시스템, 라이브 커머스 채팅, 게임 채팅처럼 대응하는 정본 sample이 없는 초기
응용 분야 설명은 다른 문서로 옮기지 않고 삭제한다. 문장을 보존하기 위해 sample의
지원 범위를 넓혀 보이게 만들지 않는다.

### 6.3 참조 정리

`framework/doc/plan/`과 삭제되는 case-study 문서 자체를 제외하고 다음 문서군의
링크, 표, 이전/다음 탐색 링크를 정리한다.

- `framework/doc/README.ko.md`
- `.NET`, Java, Kotlin, Node.js의 `README.ko.md`
- `.NET/guide/13-grpc-alternative.ko.md`
- Java, Kotlin, Node.js의 `guide/12-grpc-alternative.ko.md`
- `.NET/guide/01-overview.ko.md`
- `.NET/guide/11-feature-map.ko.md`
- `.NET/guide/samples/*.ko.md`

`grpc-alternative` 문서의 기술 비교가 현재 계약과 일치한다면 그 비교는 유지한다.
case-study 목차와 case-study 대 sample 구분만 제거하고, 구체적인 사용 흐름은
`common/sample`과 언어별 sample 문서로 연결한다.

`.NET/guide/11-feature-map.ko.md`의 case-study 열은 제거한다. 필요하면 다음 두
연결만 남긴다.

- 정본 업무 시나리오: `common/sample`
- `.NET` 실행 예제: `dotnet/guide/samples`

## 7. 모든 언어의 internals 축소

`.NET`, C++, Node.js, Java, Kotlin에 같은 분류 원칙을 적용한다. 어느 언어도
`internals`를 완료된 계획과 공개 계약의 보관소로 사용하지 않는다.

### 7.1 공통 처리 원칙

각 언어의 internals 문서를 다음 네 종류로 나눈다.

| 문서 내용 | 처리 |
|-----------|------|
| 사용자가 관찰하는 허용 조합, 오류, lifecycle | 해당 언어의 책임 spec에 흡수 |
| 사용 예제와 sample 설명 | 해당 언어의 guide 또는 guide/samples에 흡수 |
| 완료 전 구현 계획, 이식 매핑, gap 목록, 작업 기록 | 구현 완료와 현재 코드 확인 후 삭제 |
| backend 경계, 내부 실행 구조, CI와 회귀 정책 | 중복을 줄여 internals에 유지 |

공개 내용을 spec으로 옮길 때 다른 언어 문서를 그대로 복사하지 않는다. 공통 spec에
계약 근거가 있고 해당 언어 코드와 테스트가 실제로 제공하는 동작만 현재 언어 spec에
반영한다.

runtime 객체의 구체적인 시작·종료 순서, 내부 queue, adapter 배선처럼 사용자가
알 필요가 없는 내용은 spec에 넣지 않는다. 코드 유지보수에 실제로 필요한 경우에만
작은 runtime internals 문서로 남긴다.

### 7.2 .NET

#### 7.2.1 `behavior-matrix.ko.md`

허용 조합, startup validation, 공개 오류를 다음 책임 문서로 나누어 옮긴다.

| 기존 내용 | 흡수 대상 |
|-----------|-----------|
| Channel 역할과 handler 조합 | `spec/aspnet-core-channel-messaging.ko.md` |
| Spot과 SpotNode 조합 | `spec/aspnet-core-spot.ko.md`, `spec/spot-node.ko.md` |
| Stream node 조합 | `spec/aspnet-core-stream.ko.md` |
| Actor/session dispatch 조합 | `spec/aspnet-core-actor.ko.md`, `spec/session-actor-dispatch.ko.md` |
| Monitoring 등록 조합 | `spec/aspnet-core-monitoring.ko.md` |
| Location store 조합 | `spec/aspnet-core-location.ko.md` |

모든 공개 동작이 책임 spec에 반영된 뒤 문서를 삭제한다.

#### 7.2.2 `di-capability-exposure-policy.ko.md`

항상 등록되는 client, 역할별 public service 등록 조건, SpotNode와 actor factory의
구성 조건, 호출 시점 오류, bound session 조건을 `spec/handler-interfaces.ko.md`에
합친다. 내부 registrar와 validator 구현 과정은 옮기지 않고 문서를 삭제한다.

#### 7.2.3 `lifecycle-and-failure-semantics.ko.md`

request/send/publish 실패, timeout, cancellation, stream callback, Spot lifecycle,
reconnect, actor/session binding의 공개 의미를 해당 기능 spec에 나누어 반영한다.
내부 시작·종료 순서가 현재 코드 유지보수에 필요하면 문서를
`runtime-lifecycle.ko.md`로 축소하고, 필요하지 않으면 삭제한다.

#### 7.2.4 삭제와 유지

- `implementation-scope-and-nongoals.ko.md`는 완료 전 범위 문서이므로 삭제한다.
- `backend-dependency-policy.ko.md`는 backend 격리 정책으로 유지한다.
- `regression-test-matrix.ko.md`는 CI와 release gate 문서로 유지한다.

정리 후 목표는 다음과 같다.

```text
backend-dependency-policy.ko.md
regression-test-matrix.ko.md
runtime-lifecycle.ko.md        # 내부 순서가 실제로 필요할 때만 유지
```

### 7.3 Node.js

`.NET`과 같은 이름으로 복제된 정책 문서는 같은 기준으로 정리한다.

- `behavior-matrix.ko.md`의 공개 조합과 오류를 `spec/nestjs-*` 및
  `spec/session-actor-dispatch.ko.md`에 나누어 흡수한 뒤 삭제한다.
- `di-capability-exposure-policy.ko.md`의 provider 노출 조건과 오류를
  `spec/handler-interfaces.ko.md` 및 책임 `nestjs-*` spec에 흡수한 뒤 삭제한다.
- `lifecycle-and-failure-semantics.ko.md`의 공개 의미를 책임 spec에 흡수한다. 내부
  NestJS module 시작·종료 순서가 필요하면 작은 runtime lifecycle 문서만 남긴다.
- `implementation-scope-and-nongoals.ko.md`는 완료 전 범위 문서이므로 삭제한다.
- `dotnet-to-node-surface-mapping.ko.md`는 이식 작업용 문서다. 현재 Node.js public
  계약과 공통 parity를 spec과 테스트에서 확인한 뒤 삭제한다.
- `node-binding-public-api-gap-list.ko.md`는 gap이 실제로 모두 닫혔는지 확인한다.
  닫힌 목록은 삭제하고, 남은 gap이 있으면 정식 문서에 완료처럼 두지 말고 별도
  구현 작업으로 해결한 뒤 삭제한다.
- `cross-language-smoke.ko.md`의 유효한 검증 항목은
  `regression-test-matrix.ko.md` 또는 공통 E2E에 합치고 문서를 삭제한다.
- `backend-dependency-policy.ko.md`와 `regression-test-matrix.ko.md`는 유지한다.

정리 후 목표는 `.NET`과 마찬가지로 backend, 회귀 검증, 꼭 필요한 내부 lifecycle
문서만 남기는 것이다.

### 7.4 Java와 Kotlin

Kotlin은 Java framework runtime을 공유하므로 현재처럼 Java의 spec과 internals를
공유할 수 있다. 하지만 공유한다는 이유로 완료된 이식 문서와 공개 계약 중복을
유지하지 않는다. 별도의 Kotlin internals 디렉토리를 만들 필요도 없다.

- `behavior-matrix.ko.md`의 공개 validation, 실패, dispatch ordering을
  `spec/spring-boot-*`와 `spec/handler-interfaces.ko.md`에 흡수한 뒤 삭제한다.
- `di-capability-exposure-policy.ko.md`의 Spring bean 노출 조건을
  `spec/handler-interfaces.ko.md`와 책임 Spring Boot spec에 흡수한 뒤 삭제한다.
- `lifecycle-and-failure-semantics.ko.md`의 공개 의미를 책임 spec에 흡수한다. 내부
  Spring lifecycle 순서가 꼭 필요하면 축소된 runtime lifecycle 문서만 남긴다.
- `implementation-scope-and-nongoals.ko.md`는 완료 전 범위 문서이므로 삭제한다.
- `dotnet-to-java-surface-mapping.ko.md`는 Java/Kotlin 이식 작업용 문서다. 현재
  public 계약과 coroutine 차이가 spec과 테스트에 반영됐는지 확인한 뒤 삭제한다.
- `backend-dependency-policy.ko.md`와 `regression-test-matrix.ko.md`는 Java/Kotlin
  공유 유지보수 문서로 남긴다.

Kotlin README에서 삭제된 Java internals를 나열하지 않도록 고친다. Kotlin의
`suspend`, coroutine handler, `Flow`처럼 Java와 다른 공개 동작은 Java/Kotlin 공유
spec에서 명확하게 계약으로 설명한다. 사용법만 Kotlin guide에 둔다.

### 7.5 C++

C++ internals에는 공개 계약, sample, 구현 계획, 작업 로그가 크게 섞여 있으므로
다음처럼 정리한다.

#### 7.5.1 sample 문서

다음 문서는 internals가 아니라 사용자 예제다.

- `channel-messaging-samples.ko.md`
- `spot-samples.ko.md`
- `stream-samples.ko.md`

현재 C++ sample 코드와 일치하는 실행 예제는 `guide/samples/` 아래의 대응 문서에
흡수한다. 기능 guide는 같은 예제를 복제하지 않고 `guide/samples/`를 링크한다.
공통 정본 sample과 연결되는 전체 색인은 `guide/14-samples-map.ko.md`에서 안내한다.
흡수가 끝나면 internals의 세 문서를 삭제한다.

#### 7.5.2 완료 전 계획과 작업 기록

- `cpp-framework-implementation-plan.ko.md`는 구현 완료 여부를 코드, 테스트,
  sample 실행으로 확인한 뒤 삭제한다. 미완료 항목이 있으면 문서만 보존하지 말고
  구현과 검증을 완료한 뒤 삭제한다.
- `cpp-framework-posd-refactoring-log.ko.md`는 완료된 작업 기록이다. 현재 설계
  원칙이나 회귀 조건으로 남겨야 할 내용만 책임 spec, runtime internals,
  regression matrix에 반영하고 로그 자체는 삭제한다.
- `stream-open-items.ko.md`의 공개 STREAM 의미는 `spec/cpp-stream.ko.md`로 옮긴다.
  실제 미구현 항목은 구현과 테스트를 닫은 뒤 문서를 삭제한다.

#### 7.5.3 overview와 policy

- `cpp-framework-overview.ko.md`의 사용자 관점 설명은 `guide/01-overview.ko.md`로,
  공개 API 계약은 책임 spec으로 옮긴다.
- `cpp-framework-policy.ko.md`의 public API, lifecycle, 오류, serializer 계약은 책임
  spec으로 옮긴다. 사용 선택 기준은 guide로 옮긴다.
- 설치 header 경계, PIMPL, runtime owner, CMake 비공개 경계는
  `internals/runtime-architecture.ko.md`로 합친다. backend adapter와 binding 격리
  정책은 `internals/backend-dependency-policy.ko.md`가 소유한다.
- 흡수와 축소가 끝나면 기존 overview와 거대한 policy 문서는 삭제한다.

`regression-test-matrix.ko.md`는 C++ CI와 release gate를 설명하는 유지보수 문서로
남긴다. 최종 C++ internals도 runtime/backend 경계와 회귀 검증 문서만 남긴다.

### 7.6 모든 언어의 유지 대상 정리

유지하는 internals에서도 삭제된 use-case, case-study, 계획 문서, 이식 문서 참조를
정리한다. 공개 계약을 반복하는 긴 표는 책임 spec을 링크하도록 줄인다.

언어마다 파일 이름을 억지로 같게 만들 필요는 없다. 다음 목록은 허용 파일 수를
기계적으로 제한하는 규칙이 아니라, internals에 남길 수 있는 책임의 범위다. 코드를
읽기 전에 전체 구조를 이해하는 데 필요한 구현 설명은 독립 문서로 남길 수 있지만,
그 필요성과 코드 근거를 절 단위 이관 추적표에 기록한다.

```text
backend-dependency-policy.ko.md   # backend와 private runtime 경계
runtime-architecture.ko.md        # 별도 구조 설명이 코드 이해에 필요할 때만 유지
runtime-lifecycle.ko.md           # 코드만으로 파악하기 어려울 때만 유지
regression-test-matrix.ko.md      # CI, 회귀 검증, release gate
```

## 8. 모든 언어의 spec 정리 원칙

internals 내용을 옮길 때 기존 spec의 분량만 늘리지 않는다.

- 공개 시그니처와 반환·오류·lifecycle 계약만 남긴다.
- 내부 클래스명, queue 구조, adapter 생성 절차는 넣지 않는다.
- 사용 예제와 선택 기준은 guide 또는 guide/samples로 보낸다.
- 동일한 계약이 여러 spec에 필요하면 한 문서가 의미를 소유하고 다른 문서는 링크한다.
- 현재 구현되지 않은 내용을 정식 spec으로 승격하지 않는다.
- `초안`, `계획`, `아직 부족한 부분` 같은 문구가 남아 있다면 현재 코드와 테스트를
  확인한다. 구현된 계약이면 현재형으로 고치고, 구현 전 설계라면 정식 spec에서
  제거해 별도 draft 검토 대상으로 분리한다.
- API가 각각 무엇을 하는지는 산문에 함수 이름을 길게 나열하지 않는다. 산문에서는
  목적과 사용 시점을 설명하고, 일대일 API 설명은 짧은 예제의 해당 호출 옆 주석으로
  보여 준다.
- 예제 코드는 핵심 호출의 의도, 수명, 순서, 제약을 코드 주석으로 다시 설명한다.
  코드를 그대로 읽어 주는 주석은 추가하지 않는다.

공개 API 주석을 수정해야 하는 경우에는
`doc/principal/source-comment-principles.ko.md`를 먼저 확인한다. 공개 주석에는
호출자가 알아야 하는 timeout, cancellation, callback, ownership, disposal, error
계약만 적고, 내부 배선과 사용 예제는 각각 internals와 guide에 둔다.

## 9. README와 탐색 링크 정리

삭제와 흡수가 끝난 뒤 다음 진입 문서를 갱신한다.

- `framework/doc/README.ko.md`
- `framework/doc/framework/common/README.ko.md`
- `framework/doc/framework/cpp/README.ko.md`
- `framework/doc/framework/dotnet/README.ko.md`
- `framework/doc/framework/java/README.ko.md`
- `framework/doc/framework/kotlin/README.ko.md`
- `framework/doc/framework/node/README.ko.md`

상단과 하단의 이전/다음 탐색 링크도 함께 확인한다. 삭제한 문서로 이어지는 링크는
가장 가까운 현재 정식 문서로 연결하거나, 탐색 순서에 필요하지 않으면 제거한다.

README는 모든 파일을 길게 나열하는 대신 다음 읽기 순서를 보여 주는 데 집중한다.

1. 공통 spec
2. 공통 sample
3. 공통 E2E
4. 언어별 spec
5. 언어별 guide와 samples
6. 유지보수자용 internals

## 10. 내용 이관과 자동 검증 이전

### 10.1 절 단위 이관 추적표

삭제할 문서는 파일 단위로만 확인하지 않는다. 각 원본의 `#`, `##`, `###` 제목을
모두 수집하고, 아래 추적표에 한 번씩 기록한다. 한 제목 아래에 공개 계약과 내부
설명이 섞여 있으면 행을 나눈다.

| 원본 문서와 절 | 분류 | 처리 | 대상 문서와 절 | 코드·테스트 근거 | 상태 |
|----------------|------|------|------------------|------------------|------|
| 예: `behavior-matrix` Channel 표 | 공개 계약 | 이동 후 원본 삭제 | channel spec의 validation 절 | validator test 이름 | 미완료 |
| 예: C++ POSD 과거 작업 기록 | 과거 기록 | 삭제 | 없음 | 현재 코드와 회귀 테스트로 대체 | 미완료 |

분류는 다음 값 가운데 하나를 사용한다.

- `공개 계약`: 책임 spec에 반영한다.
- `사용법`: guide 또는 guide/samples에 반영한다.
- `내부 구조`: runtime architecture, lifecycle, backend 문서에 반영한다.
- `검증 정책`: regression matrix나 실제 검증 코드에 반영한다.
- `과거 기록`: 현재 코드와 테스트에 필요한 결정이 남아 있지 않음을 확인하고 삭제한다.
- `폐기 아이디어`: 공통 spec 근거와 구현이 없음을 확인하고 다른 문서로 옮기지 않는다.

다음 원본은 반드시 절 단위 추적표를 작성한다.

- 공통 `use-cases`와 `usecase-validation`
- 네 언어의 모든 case-study
- `.NET`, Node.js, Java의 behavior, DI, lifecycle, scope 문서
- Node.js와 Java의 `.NET` 이식 매핑 문서
- Node.js gap 목록과 cross-language smoke 문서
- C++ overview, policy, implementation plan, POSD 로그, STREAM open items
- C++ internals의 channel, Spot, Stream sample 문서

원본의 모든 제목 행이 `완료`이고, 대상 문서와 코드·테스트 근거를 실제로 확인한
뒤에만 원본을 삭제한다. 단순히 `필요한 내용을 반영했다`고 적는 것으로 완료하지
않는다.

### 10.2 문서 경로를 고정한 자동 검증 이전

현재 테스트와 스크립트가 삭제 대상 문서를 직접 검증 기준으로 사용한다. 문서 삭제
전에 아래 의존성을 영구적인 코드, 설정, spec 또는 regression matrix로 옮긴다.

#### .NET

- `Documentation/Regression.cs`의 `DotNetDraftDocuments`를 현재 정식 spec, sample,
  internals 책임에 맞는 인벤토리로 바꾸고 `Draft`라는 완료 전 의미를 제거한다.
- `GuideCaseStudyDocuments`와 case-study 디렉토리 존재 검사를 제거한다.
- 삭제할 behavior, DI, scope, lifecycle 파일명을 요구하는 exact-list assertion을
  새 최종 문서 구조에 맞춘다.
- `DotNetRegressionMatrix_References_AllDraftDocuments`를 삭제하거나, 유지하는
  regression matrix가 실제 회귀 책임 문서만 참조하는 검사로 바꾼다.
- spec에 내부 클래스 구현을 넣도록 강제하는
  `DotNetSessionActorDispatch_Documents_ExecutionSerialization_Core_Code` 계열 검사는
  제거한다. 필요한 내부 직렬화 설명은 runtime architecture 문서와 runtime unit
  test가 소유하도록 바꾼다.
- sample 문서와 새 `guide/samples` 위치를 사용하는 모든 `ResolveDoc(...)` 호출을
  현재 경로 기준으로 갱신한다.

#### Node.js

- `test/contract/documentation-regression.test.js`에서
  `internals/di-capability-exposure-policy.ko.md`를 직접 읽는 목록을 책임 spec으로
  바꾼다.
- `scripts/verify_node_abi_matrix.js`가
  `implementation-scope-and-nongoals.ko.md`에서 플랫폼 목록을 읽지 않도록 바꾼다.
  플랫폼과 Node 버전 기준은 workflow와 유지되는 regression matrix가 소유한다.
- 삭제할 internals를 전제로 하는 documentation regression과 release script를 모두
  새 최종 구조로 갱신한다.

#### C++

- `test_cpp_framework_layout_contract.cpp`가 implementation plan과 overview의 표를
  정본으로 읽지 않도록 바꾼다.
- public/private header 경계와 runtime owner 검사는 실제 설치 파일 목록, CMake
  target, public header compile test를 기준으로 수행한다.
- goal별 기능 완료 여부는 plan 문구가 아니라 실제 contract test와 CTest label이
  소유하도록 바꾼다.
- `verify_ctest_label_contract.cmake`가 implementation plan의 명령을 읽지 않도록 하고,
  CMake/CTest의 실제 등록 label과 명시적인 테스트 manifest를 검증한다.
- `http_perf_policy.cmake`가 implementation plan을 요구하지 않도록 하고, 유지되는
  perf 정책 또는 실제 CMake option을 기준으로 검증한다.

#### Java와 Kotlin

- Java contract test가 guide/spec/internals를 동적으로 검사하는 범위를 새 최종
  디렉토리 구조와 맞춘다.
- Kotlin이 공유하는 Java spec과 internals 링크를 검사하고, 삭제한 문서 이름을
  요구하는 assertion이 없는지 다시 검색한다.

자동 검증을 바꾼 뒤에는 삭제 파일명을 저장소 전체에서 검색한다. Markdown만 찾지
않고 C#, JavaScript, TypeScript, Java, Kotlin, C++, CMake, shell, workflow와 JSON도
포함한다.

### 10.3 `framework/doc/plan` 종료 목록

`framework/doc/plan/`의 문서는 모두 임시 자료다. 현재 존재하는 다음 22개 파일을
각각 코드·테스트·E2E·sample과 대조한다. 구현이 완료된 문서는 정식 문서에 필요한
계약만 반영하고 삭제한다. 미완료 항목이 있으면 먼저 구현과 검증을 닫고 삭제한다.

| plan 문서 | 구현 증거 확인 | 정식 문서 반영 | 삭제 |
|-----------|----------------|----------------|------|
| `actor-spot/README.ko.md` | [ ] | [ ] | [ ] |
| `actor-spot/cpp-worker.ko.md` | [ ] | [ ] | [ ] |
| `actor-spot/dotnet-worker.ko.md` | [ ] | [ ] | [ ] |
| `actor-spot/java-kotlin-worker.ko.md` | [ ] | [ ] | [ ] |
| `actor-spot/node-worker.ko.md` | [ ] | [ ] | [ ] |
| `framework-actor-location-registry-plan.ko.md` | [ ] | [ ] | [ ] |
| `framework-actor-manager-ref-surface-plan.ko.md` | [ ] | [ ] | [ ] |
| `framework-document-structure-cleanup-plan.ko.md` | [ ] | [ ] | [ ] |
| `framework-java-e2e-sample-gap-closure-plan.ko.md` | [ ] | [ ] | [ ] |
| `framework-java-kotlin-yd-bindactors-session-bind-bug.ko.md` | [ ] | [ ] | [ ] |
| `framework-kotlin-e2e-sample-gap-closure-plan.ko.md` | [ ] | [ ] | [ ] |
| `framework-kotlin-sm-d12-remote-session-bind-bug.ko.md` | [ ] | [ ] | [ ] |
| `framework-kotlin-supportchat-actor-bound-reply-bug.ko.md` | [ ] | [ ] | [ ] |
| `framework-kotlin-yd-e3-route-recovery-bug.ko.md` | [ ] | [ ] | [ ] |
| `framework-node-yd-e3-stream-disconnect-bug.ko.md` | [ ] | [ ] | [ ] |
| `framework-public-contract-posd-redesign.ko.md` | [ ] | [ ] | [ ] |
| `framework-ref-target-unification-cpp-worker-prompt.ko.md` | [ ] | [ ] | [ ] |
| `framework-ref-target-unification-dotnet-worker-prompt.ko.md` | [ ] | [ ] | [ ] |
| `framework-ref-target-unification-java-worker-prompt.ko.md` | [ ] | [ ] | [ ] |
| `framework-ref-target-unification-kotlin-worker-prompt.ko.md` | [ ] | [ ] | [ ] |
| `framework-ref-target-unification-node-worker-prompt.ko.md` | [ ] | [ ] | [ ] |
| `framework-ref-target-unification-plan.ko.md` | [ ] | [ ] | [ ] |

최종 상태에서는 `framework/doc/plan/` 아래에 파일이 남지 않아야 한다. 이 계획
문서는 다른 plan의 구현 증거와 삭제를 모두 확인한 뒤 마지막으로 삭제한다.

## 11. 적용 순서

1. 삭제 대상과 정식 문서의 inbound link 목록을 저장소 전체에서 다시 수집한다.
2. 삭제 문서의 모든 제목을 절 단위 이관 추적표에 등록한다.
3. 삭제 대상 문서를 정본으로 읽는 테스트와 자동 검증을 영구 기준으로 이전한다.
4. 공통 spec에서 초기 use-case 개념과 참조를 제거한다.
5. `common/use-cases`와 `common/spec/usecase-validation.ko.md`를 삭제한다.
6. 네 언어의 `guide/case-studies`를 삭제하고 sample 직접 링크로 바꾼다.
7. `.NET`, C++, Node.js, Java/Kotlin internals의 공개 계약을 각 책임 spec에 중복 없이
   흡수한다.
8. 각 언어의 sample 설명을 고정된 guide/samples 위치로 옮긴다.
9. 완료 전 계획, 이식 매핑, gap 목록, 작업 로그와 중복 internals를 삭제한다.
10. 모든 언어에서 유지하는 internals를 backend, runtime architecture, 내부 lifecycle,
    회귀 검증 중심으로 축소한다.
11. README, feature map, guide, sample의 탐색 링크를 갱신한다.
12. 22개 plan 문서의 구현 증거와 정식 문서 반영을 확인하고 완료된 문서를 삭제한다.
13. 저장소 전체에서 삭제된 경로와 초기 개념의 잔존 여부를 검사한다.
14. 문서 링크, 자동 검증, contract test, E2E, sample을 검증한다.
15. 변경 사항을 다시 검토한 뒤 이 계획 문서를 마지막으로 삭제한다.

## 12. 검증 기준

### 12.1 삭제된 경로 참조

검색할 때 `framework/doc/plan/`은 구현 중에는 제외한다. `framework/doc`만 검색하지
말고 코드, 테스트, 스크립트, workflow와 상위 문서까지 모두 확인한다. 다음 검색
결과가 없어야 한다.

```bash
rg -n 'common/use-cases|use-cases/|usecase-validation\.ko\.md' \
  framework doc scripts .github --glob '!framework/doc/plan/**'

rg -n 'guide/case-studies|case-studies/' \
  framework doc scripts .github --glob '!framework/doc/plan/**'
```

모든 언어의 internals에서 삭제하기로 한 문서도 같은 방식으로 확인한다.

```bash
rg -n 'behavior-matrix\.ko\.md|di-capability-exposure-policy\.ko\.md|implementation-scope-and-nongoals\.ko\.md|dotnet-to-(node|java)-surface-mapping\.ko\.md|node-binding-public-api-gap-list\.ko\.md|cross-language-smoke\.ko\.md|cpp-framework-implementation-plan\.ko\.md|cpp-framework-posd-refactoring-log\.ko\.md|stream-open-items\.ko\.md' \
  framework doc scripts .github --glob '!framework/doc/plan/**'
```

`lifecycle-and-failure-semantics.ko.md`를 삭제하거나 이름을 바꾼 경우 그 경로도
별도로 검색한다. 각 언어의 최종 internals 파일 목록도 직접 출력해 허용한 책임 외의
문서가 남지 않았는지 확인한다.

```bash
find framework/doc/framework/{dotnet,node,java,kotlin,cpp}/internals \
  -maxdepth 1 -type f -printf '%p\n' 2>/dev/null | sort
```

### 12.2 초기 아이디어 잔존 여부

다음 용어는 무조건 0건이어야 한다는 뜻이 아니다. 현재 공개 계약, 코드, sample,
E2E 근거가 있는지 문맥별로 확인하고, 삭제한 use-case를 설명하기 위해서만 남은
문장은 제거한다.

```bash
rg -n 'worker dispatch|worker-dispatch|scatter-gather|workflow orchestration' \
  framework/doc/framework --glob '*.md'
```

### 12.3 문서 책임 검사

- common spec에 언어별 API 시그니처가 새로 들어가지 않았는지 확인한다.
- 언어별 spec에 내부 클래스와 구현 순서가 새로 들어가지 않았는지 확인한다.
- sample에 구현되지 않은 응용 분야가 지원 기능처럼 추가되지 않았는지 확인한다.
- E2E 문서를 새 public API의 유일한 근거로 사용하지 않았는지 확인한다.
- backend와 CI 정책이 public spec에 섞이지 않았는지 확인한다.
- 같은 계약 문장이 여러 파일에 복제되지 않았는지 확인한다.

### 12.4 링크와 문서 형식

- 저장소의 Markdown 링크 검사나 문서 검증 테스트가 있으면 실행한다.
- 삭제한 파일을 가리키는 상대 링크가 정식 문서에 남지 않아야 한다.
- 이전/다음 탐색 링크가 존재하는 파일만 가리켜야 한다.
- 금지 표현과 문서 작성 규칙을 다시 검사한다.
- ASCII 다이어그램을 수정했다면 영문 전용과 고정 폭 정렬을 확인한다.
- `AGENTS.md`에서 금지한 표현이 문서 본문에 없어야 한다.
- 영어 명사를 여러 개 이어 붙인 압축 표현을 쉬운 한국어 문장으로 풀어 썼는지
  확인한다.
- 흐름도와 시퀀스는 Mermaid를 사용하고, 계층·메모리·프레임 구조는 영문 전용 ASCII
  다이어그램을 사용했는지 확인한다.
- ASCII 다이어그램의 행 너비, 테두리 정렬, 탭과 trailing space를 확인한다.

### 12.5 자동 검증, E2E, sample

문서 경로를 고정했던 테스트를 먼저 실행하고, 그다음 언어별 전체 검증을 실행한다.
현재 저장소의 runner와 task 이름이 바뀌었다면 실제 등록된 명령을 다시 확인해 같은
범위로 실행한다.

```bash
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj
dotnet test framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj
dotnet test framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj

npm --prefix framework/languages/node run verify:release

(cd framework/languages/java && ./gradlew contractTest sampleTest)

ctest --test-dir framework/languages/cpp/build --output-on-failure
```

E2E와 sample은 실제 공통 시나리오를 확인하기 위해 언어별 runner로 실행한다.

```bash
framework/languages/dotnet/e2e/run_e2e_all.sh
framework/languages/dotnet/samples/run_samples.sh

framework/languages/node/e2e/run_e2e_all.sh
framework/languages/node/samples/run_samples.sh

framework/languages/java/e2e/run_e2e_all.sh
framework/languages/java/e2e-kotlin/run_e2e_all.sh
framework/languages/java/samples/run_samples.sh

framework/languages/cpp/e2e/run_e2e_all.sh
framework/languages/cpp/samples/run_samples.sh
```

### 12.6 변경 범위 검사

```bash
git diff --check -- framework/doc
git status --short -- framework/doc
```

기존 작업 트리에 있던 다른 변경과 이번 문서 구조 변경을 구분한다. 커밋할 때는
이번 계획에 포함된 코드, 테스트, 문서만 선별해서 stage한다. stage한 뒤 새 파일까지
포함한 검사를 추가로 실행한다.

```bash
git diff --cached --check
git diff --cached --stat
```

모든 plan 문서를 삭제한 최종 상태도 확인한다.

```bash
test -z "$(find framework/doc/plan -type f -print -quit)"
```

## 13. 실행 체크리스트

체크 표시는 파일을 삭제하거나 옮긴 직후가 아니라, 새 책임 문서 반영과 참조 검사까지
끝난 뒤에 한다. 공개 계약을 다른 문서에 옮겨야 하는 항목은 원문 삭제만으로 완료로
보지 않는다.

### 13.1 작업 전 확인

- [ ] `git status --short -- framework/doc`로 기존 사용자 변경을 기록했다.
- [ ] 삭제 대상 파일 목록을 `find`와 `rg --files`로 다시 확인했다.
- [ ] `framework/doc/plan/`을 제외한 inbound link 목록을 저장했다.
- [ ] 각 언어의 현재 코드, contract test, E2E, sample을 공개 계약 판정 기준으로
  확인했다.
- [ ] 다른 언어 구현만을 근거로 새 public API를 추가하지 않기로 확인했다.
- [ ] 삭제 대상 문서의 모든 `#`, `##`, `###` 제목을 절 단위 이관 추적표에 등록했다.
- [ ] 삭제 경로를 직접 읽는 코드, 테스트, 스크립트, workflow 목록을 저장소 전체에서
  수집했다.
- [ ] 절 단위 이관 추적표의 모든 행에 정확한 처리, 대상, 코드·테스트 근거를 적었다.
- [ ] core와 binding 공개 계약은 `core/include/zlink.h`와 루트 `doc/spec/`을 먼저
  확인했다.
- [ ] framework 공통 계약, 언어별 계약, sample, E2E의 근거 우선순위를 확인했다.
- [ ] 새 언어별 문서를 `framework/languages/<lang>/doc/`에 만들지 않고 모두
  `framework/doc/` 아래의 책임 위치에 두었다.
- [ ] 실제 미구현 API가 있으면 정식 spec에 넣지 않고 구현 전 draft 규칙에 따라
  별도 검토 대상으로 분리했다.
- [ ] 새 helper나 public API가 필요한 변경은 기존 표면을 먼저 조사하고 두 가지 이상
  설계 대안을 비교했다.
- [ ] sample과 E2E에 raw buffer 처리, 호출부 codec, 테스트 전용 우회가 추가되지
  않도록 기준을 확인했다.

### 13.2 공통 use-case 제거

- [ ] `common/use-cases/README.ko.md`를 삭제했다.
- [ ] `01-service-to-service-rpc.ko.md`를 삭제했다.
- [ ] `02-playhouse-play-to-api.ko.md`를 삭제했다.
- [ ] `03-worker-dispatch.ko.md`를 삭제했다.
- [ ] `04-domain-event-fanout.ko.md`를 삭제했다.
- [ ] `05-cache-invalidation-and-config-refresh.ko.md`를 삭제했다.
- [ ] `06-stage-state-sync.ko.md`를 삭제했다.
- [ ] `07-real-time-notification-fanout.ko.md`를 삭제했다.
- [ ] `08-scatter-gather-query.ko.md`를 삭제했다.
- [ ] `09-workflow-orchestration.ko.md`를 삭제했다.
- [ ] `common/spec/usecase-validation.ko.md`를 삭제했다.
- [ ] `framework/doc/README.ko.md`의 use-case 색인과 검증 링크를 제거했다.
- [ ] `common/README.ko.md`의 use-case 탐색 링크와 역할 설명을 제거했다.
- [ ] `common/perf/README.ko.md`의 use-case validation 의존 설명을 정리했다.
- [ ] `common/spec/channel-topology.ko.md`의 초기 조합 모델을 정리했다.
- [ ] `common/spec/framework-api.ko.md`의 구현되지 않은 확장 예고를 정리했다.
- [ ] `common/spec/interaction-model.ko.md`의 worker/scatter/workflow 설명을 정리했다.
- [ ] `common/spec/message-model.ko.md`의 use-case 참조를 정리했다.
- [ ] `common/spec/overview.ko.md`의 초기 모델 목록을 정리했다.
- [ ] `common/spec/spot-address-messaging.ko.md`의 use-case 참조를 정리했다.
- [ ] `.NET`과 Node.js regression matrix의 use-case validation 참조를 제거했다.
- [ ] `doc/principal/documentation/documentation-principles.ko.md`가 정본 sample과
  공통 spec을 입력으로 안내하도록 고쳤다.

### 13.3 공통 sample과 E2E 책임

- [ ] `common/sample/README.ko.md`가 여섯 정본 sample을 명확히 안내한다.
- [ ] Bingo 시나리오와 실제 sample/E2E가 일치한다.
- [ ] TicTacToe 시나리오와 실제 sample/E2E가 일치한다.
- [ ] SupportChat 시나리오와 실제 sample/E2E가 일치한다.
- [ ] DeliveryDispatch 시나리오와 실제 sample/E2E가 일치한다.
- [ ] ShoppingMall 시나리오와 실제 sample/E2E가 일치한다.
- [ ] GameQuest 시나리오와 실제 sample/E2E가 일치한다.
- [ ] sample이 새 public API 계약의 근거처럼 쓰이지 않았는지 확인했다.
- [ ] `common/e2e`가 검증 요구사항이고 계약의 단독 출처가 아니라는 설명을 확인했다.

### 13.4 case-studies 제거

아래 아홉 파일이 `.NET`, Java, Kotlin, Node.js 네 디렉토리에서 모두 삭제됐는지
확인한다.

- [ ] `13-case-ecommerce-checkout.ko.md`가 네 언어에서 삭제됐다.
- [ ] `14-case-microservice-mesh.ko.md`가 네 언어에서 삭제됐다.
- [ ] `15-case-realtime-game.ko.md`가 네 언어에서 삭제됐다.
- [ ] `16-case-ride-hailing.ko.md`가 네 언어에서 삭제됐다.
- [ ] `17-case-chat-messaging.ko.md`가 네 언어에서 삭제됐다.
- [ ] `17-1-case-marketplace-chat.ko.md`가 네 언어에서 삭제됐다.
- [ ] `17-2-case-live-commerce-chat.ko.md`가 네 언어에서 삭제됐다.
- [ ] `17-3-case-game-chat.ko.md`가 네 언어에서 삭제됐다.
- [ ] `18-case-trading-system.ko.md`가 네 언어에서 삭제됐다.
- [ ] 네 언어의 `guide/case-studies/` 디렉토리가 비어 있거나 제거됐다.
- [ ] 네 언어 README의 case-study 설명과 링크를 제거했다.
- [ ] 네 언어 `grpc-alternative` 문서의 case-study 목차와 탐색 링크를 제거했다.
- [ ] `.NET guide/01-overview.ko.md`의 case-study 참조를 제거했다.
- [ ] `.NET guide/11-feature-map.ko.md`의 case-study 열을 제거했다.
- [ ] `.NET guide/samples/*.ko.md`의 case-study 탐색 링크를 제거했다.
- [ ] ShoppingMall, DeliveryDispatch, SupportChat, Bingo/TicTacToe/GameQuest로 연결할 수
  있는 설명만 정본 sample로 연결했다.
- [ ] 대응 sample이 없는 거래 시스템과 세부 채팅 아이디어를 다른 정식 문서로
  복사하지 않았다.

### 13.5 .NET internals

- [ ] `behavior-matrix.ko.md`의 Channel 계약을 channel spec에 반영했다.
- [ ] `behavior-matrix.ko.md`의 Spot/SpotNode 계약을 Spot spec에 반영했다.
- [ ] `behavior-matrix.ko.md`의 Stream 계약을 Stream spec에 반영했다.
- [ ] `behavior-matrix.ko.md`의 Actor/session 계약을 Actor/session spec에 반영했다.
- [ ] `behavior-matrix.ko.md`의 Monitoring/Location 계약을 각 spec에 반영했다.
- [ ] 중복 계약을 만들지 않고 `behavior-matrix.ko.md`를 삭제했다.
- [ ] DI public service 등록 조건을 `spec/handler-interfaces.ko.md`에 반영했다.
- [ ] 내부 registrar/validator 설명을 spec에 넣지 않고
  `di-capability-exposure-policy.ko.md`를 삭제했다.
- [ ] lifecycle/failure의 공개 의미를 책임 spec에 나누어 반영했다.
- [ ] 내부 lifecycle 문서 유지 필요성을 코드와 테스트로 판정했다.
- [ ] 기존 lifecycle 문서를 삭제하거나 내부 내용만 가진
  `runtime-lifecycle.ko.md`로 축소했다.
- [ ] `implementation-scope-and-nongoals.ko.md`를 삭제했다.
- [ ] `backend-dependency-policy.ko.md`를 backend 경계만 남도록 검토했다.
- [ ] `regression-test-matrix.ko.md`에서 삭제 문서 참조와 중복 계약을 정리했다.
- [ ] `.NET README`, guide, sample, spec의 internals 링크를 모두 갱신했다.
- [ ] `.NET Documentation/Regression.cs`의 draft 문서 목록과 case-study 목록을 최종
  구조에 맞게 바꿨다.
- [ ] `.NET` spec에 내부 구현 코드를 넣도록 강제하는 문서 regression test를
  runtime unit test 또는 runtime architecture 검사로 바꿨다.
- [ ] `.NET`의 삭제 문서 파일명을 직접 요구하는 `ResolveDoc(...)`와 assertion이
  0건이다.

### 13.6 Node.js internals

- [ ] `behavior-matrix.ko.md`의 공개 계약을 `spec/nestjs-*`와 session spec에 반영하고
  문서를 삭제했다.
- [ ] `di-capability-exposure-policy.ko.md`의 provider 조건을 handler 및 책임 spec에
  반영하고 문서를 삭제했다.
- [ ] `lifecycle-and-failure-semantics.ko.md`의 공개 의미를 책임 spec에 반영했다.
- [ ] 내부 NestJS lifecycle 문서 유지 필요성을 코드와 테스트로 판정했다.
- [ ] 기존 lifecycle 문서를 삭제하거나 내부 내용만 가진
  `runtime-lifecycle.ko.md`로 축소했다.
- [ ] `implementation-scope-and-nongoals.ko.md`를 삭제했다.
- [ ] `dotnet-to-node-surface-mapping.ko.md`의 유효 계약이 spec과 테스트에 있는지
  확인하고 문서를 삭제했다.
- [ ] `node-binding-public-api-gap-list.ko.md`의 모든 gap을 실제 구현과 테스트로 닫고
  문서를 삭제했다.
- [ ] `cross-language-smoke.ko.md`의 검증 항목을 regression matrix 또는 E2E에 반영하고
  문서를 삭제했다.
- [ ] `backend-dependency-policy.ko.md`를 backend 경계만 남도록 검토했다.
- [ ] `regression-test-matrix.ko.md`에서 삭제 문서 참조와 중복 계약을 정리했다.
- [ ] Node.js README, guide, sample, spec의 internals 링크를 모두 갱신했다.
- [ ] Node.js documentation regression이 삭제된 DI policy를 직접 읽지 않도록 바꿨다.
- [ ] Node.js ABI matrix가 삭제된 implementation scope를 직접 읽지 않도록 바꿨다.
- [ ] Node.js release script와 contract test에 삭제 문서 파일명이 0건이다.

### 13.7 Java와 Kotlin internals

- [ ] `behavior-matrix.ko.md`의 공개 계약을 Spring Boot 책임 spec에 반영하고 문서를
  삭제했다.
- [ ] `di-capability-exposure-policy.ko.md`의 Spring bean 조건을 handler 및 책임 spec에
  반영하고 문서를 삭제했다.
- [ ] `lifecycle-and-failure-semantics.ko.md`의 공개 의미를 책임 spec에 반영했다.
- [ ] 내부 Spring lifecycle 문서 유지 필요성을 코드와 테스트로 판정했다.
- [ ] 기존 lifecycle 문서를 삭제하거나 내부 내용만 가진
  `runtime-lifecycle.ko.md`로 축소했다.
- [ ] `implementation-scope-and-nongoals.ko.md`를 삭제했다.
- [ ] `dotnet-to-java-surface-mapping.ko.md`의 유효 계약이 Java/Kotlin 공유 spec과
  테스트에 있는지 확인하고 문서를 삭제했다.
- [ ] `backend-dependency-policy.ko.md`를 Java/Kotlin backend 경계만 남도록 검토했다.
- [ ] `regression-test-matrix.ko.md`에서 삭제 문서 참조와 중복 계약을 정리했다.
- [ ] Kotlin의 `suspend`, coroutine handler, `Flow` 공개 의미가 공유 spec에
  빠짐없이 정의됐는지 확인했다.
- [ ] Kotlin README에서 삭제된 Java internals 링크와 목록을 제거했다.
- [ ] 별도 Kotlin internals를 중복 생성하지 않았다.
- [ ] Java와 Kotlin README, guide, sample, spec의 internals 링크를 모두 갱신했다.
- [ ] Java/Kotlin contract test의 문서 스캔 범위를 최종 spec, guide, internals 구조와
  맞췄다.

### 13.8 C++ internals

- [ ] `channel-messaging-samples.ko.md`의 유효 예제를 `guide/samples/`의 대응 문서에
  반영하고 원본을 삭제했다.
- [ ] `spot-samples.ko.md`의 유효 예제를 `guide/samples/`의 대응 문서에 반영하고
  원본을 삭제했다.
- [ ] `stream-samples.ko.md`의 유효 예제를 `guide/samples/`의 대응 문서에 반영하고
  원본을 삭제했다.
- [ ] C++ sample 안내와 여섯 정본 sample의 연결을 `guide/14-samples-map.ko.md`에서
  확인했다.
- [ ] `cpp-framework-implementation-plan.ko.md`의 모든 goal을 코드, 테스트, sample
  실행으로 확인하고 문서를 삭제했다.
- [ ] `cpp-framework-posd-refactoring-log.ko.md`에서 현재 필요한 설계 결정과 회귀
  조건만 책임 문서에 반영하고 로그를 삭제했다.
- [ ] `stream-open-items.ko.md`의 공개 의미를 `spec/cpp-stream.ko.md`에 반영했다.
- [ ] `stream-open-items.ko.md`의 실제 미구현 항목을 구현·검증하고 문서를 삭제했다.
- [ ] `cpp-framework-overview.ko.md`의 사용자 설명을 guide에 반영했다.
- [ ] `cpp-framework-overview.ko.md`의 공개 계약을 책임 spec에 반영하고 문서를
  삭제했다.
- [ ] `cpp-framework-policy.ko.md`의 public 계약과 사용 선택 기준을 spec과 guide에
  나누어 반영했다.
- [ ] C++ 설치 header, PIMPL, runtime owner, CMake 비공개 경계를
  `internals/runtime-architecture.ko.md`에 반영했다.
- [ ] C++ backend adapter와 binding 격리 정책을
  `internals/backend-dependency-policy.ko.md`에 반영했다.
- [ ] `cpp-framework-policy.ko.md`를 삭제했다.
- [ ] `regression-test-matrix.ko.md`에서 삭제 문서 참조와 중복 계약을 정리했다.
- [ ] C++ README, guide, sample map, spec의 internals 링크를 모두 갱신했다.
- [ ] C++ layout contract가 implementation plan과 overview 문서를 정본으로 읽지 않도록
  실제 설치 header, CMake target, test manifest 기준으로 바꿨다.
- [ ] C++ CTest label 검사가 implementation plan 명령을 읽지 않도록 바꿨다.
- [ ] C++ HTTP perf policy 검사가 implementation plan을 요구하지 않도록 바꿨다.
- [ ] C++ contract/perf/CMake 코드에 삭제 문서 파일명이 0건이다.

### 13.9 모든 언어의 spec과 internals 최종 점검

- [ ] `.NET` internals가 backend, 필요한 architecture/lifecycle, regression 책임만 가진다.
- [ ] Node.js internals가 backend, 필요한 architecture/lifecycle, regression 책임만 가진다.
- [ ] Java/Kotlin 공유 internals가 backend, 필요한 architecture/lifecycle, regression 책임만 가진다.
- [ ] C++ internals가 runtime/backend architecture, 필요한 lifecycle, regression 책임만
  가진다.
- [ ] 언어별 spec에 내부 클래스, queue, adapter 생성 절차가 들어가지 않았다.
- [ ] 언어별 guide에 내부 socket, endpoint 배선, private runtime 설명이 들어가지 않았다.
- [ ] 현재 구현되지 않은 계약을 정식 spec에 새로 넣지 않았다.
- [ ] 공통 계약 근거가 있는 기능의 언어별 public contract 누락이 없는지 비교했다.
- [ ] 같은 계약을 여러 문서에 복제하지 않고 책임 문서와 링크로 정리했다.
- [ ] internals 유지 여부를 파일 수가 아니라 유지보수자가 코드 전에 알아야 하는
  내부 구조인지로 판정했다.
- [ ] 유지한 internals는 내부 소켓 배선, 데이터 흐름, 스레드 모델, lifecycle 같은
  구현 구조만 설명하고 사용법과 공개 계약을 반복하지 않는다.
- [ ] 공개 API 주석을 바꾼 경우 source comment 원칙과 실제 공개 계약이 일치한다.
- [ ] .NET framework 변경이 binding의 public API만 사용하며 reflection과
  `InternalsVisibleTo` 우회가 없다.
- [ ] 메시지별 codec 등록 API나 호출부 encode/decode 우회가 새로 생기지 않았다.
- [ ] POSD 위험 신호를 다시 검사했고, 비자명한 설계 변경에는 두 가지 이상 대안과
  선택 근거가 남아 있다.

### 13.10 링크와 검증

- [ ] 삭제 경로의 저장소 전체 `rg` 검사 결과가 `framework/doc/plan/`을 제외하고
  0건이다.
- [ ] 절 단위 이관 추적표의 미완료 행이 0건이다.
- [ ] 최종 internals 파일 목록이 언어별 허용 책임과 일치한다.
- [ ] 초기 아이디어 용어 검색 결과를 문맥별로 검토했다.
- [ ] 모든 상대 Markdown 링크가 존재하는 파일을 가리킨다.
- [ ] 상단과 하단 이전/다음 탐색 링크가 끊기지 않았다.
- [ ] 저장소의 문서 검증 테스트를 실행해 통과했다.
- [ ] 관련 contract test를 실행해 통과했다.
- [ ] 관련 E2E를 실행해 통과했다.
- [ ] 여섯 정본 sample을 언어별 지원 범위에 맞게 실행해 통과했다.
- [ ] 문서 금지 표현과 작성 규칙을 검사했다.
- [ ] 산문과 코드 주석의 API 설명 역할을 분리했고, 예제 핵심 줄에 의도와 제약을
  설명하는 주석이 있다.
- [ ] Mermaid와 ASCII 다이어그램의 용도, 영문 전용, 폭과 정렬 규칙을 확인했다.
- [ ] `git diff --check -- framework/doc`가 통과했다.
- [ ] 선별 stage 뒤 `git diff --cached --check`가 통과했다.
- [ ] `git status --short -- framework/doc`로 관련 없는 변경이 섞이지 않았는지
  확인했다.
- [ ] 마지막 전체 재검토에서 누락, 깨진 링크, 중복 계약, 미완료 항목이 0건이다.
- [ ] 10.3절의 22개 plan 문서가 모두 구현 증거 확인, 정식 문서 반영, 삭제 상태다.
- [ ] `framework/doc/plan/`에는 현재 계획 문서 외에 파일이 남지 않았다.
- [ ] 모든 체크를 완료한 뒤 이 계획 문서를 삭제했다.

## 14. 완료 조건

- `common/use-cases`, use-case validation, 네 언어 case-studies가 삭제됐다.
- 공통 spec, sample, E2E의 책임이 겹치지 않는다.
- `.NET`, C++, Node.js, Java, Kotlin의 공개 계약이 각 책임 spec에 빠짐없이 남았다.
- 모든 언어의 완료 전 계획, 이식 매핑, gap 목록, 작업 로그가 삭제됐다.
- 모든 언어의 internals에는 backend/runtime architecture, 꼭 필요한 lifecycle,
  회귀 검증만 남았다.
- 삭제 문서를 정본으로 읽던 자동 검증이 코드, 설정, spec, regression matrix 기반으로
  이전됐다.
- 절 단위 이관 추적표에 미완료 행이 없다.
- `framework/doc/plan/`의 22개 임시 문서가 모두 구현 확인 후 삭제됐다.
- README와 탐색 링크가 새 문서 구조와 일치한다.
- 문서 검사, contract test, E2E, sample 검증이 통과했다.
- 새 중복 문서나 새 미완료 정리 항목이 남지 않았다.
- 13절의 체크리스트가 모두 완료됐으며 마지막으로 이 계획 문서를 삭제했다.
