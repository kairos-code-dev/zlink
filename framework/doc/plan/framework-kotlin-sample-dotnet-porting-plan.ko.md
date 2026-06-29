# Kotlin Framework Sample .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/samples`의 정본 샘플 6종을 기준 구현으로 삼아
`framework/languages/java/samples/kotlin`이 같은 역할 분리, 메시지 계약, client self-check, 실행 검증
수준을 제공하도록 정렬하는 절차를 정의한다.

Kotlin 샘플은 Java framework core를 사용하지만, 단순 Java 샘플의 문법 변환으로 완료하지 않는다.
Kotlin DSL, suspending handler, coroutine 경계가 공통 샘플 문서와 같은 사용자 체감 동작을 제공해야
한다.

## 완료 기준

1. Kotlin 샘플 6종이 공통 샘플 문서의 서버 역할, 메시지 흐름, 검증 순서를 같은 의미로 구현한다.
2. Gradle project와 package 배치는 Kotlin/Gradle 관례를 따르되, `.NET`의 `Shared`, `Client`,
   `Server/<Role>` 책임이 대응된다. `.NET` 샘플에 `Probe`가 있는 경우에만 Kotlin에도 같은 검증 책임을 둔다.
3. Kotlin public framework API로 구현할 수 없는 항목은 Java internal helper, 테스트 전용 adapter,
   raw frame 우회로 메우지 않고 `sample-porting-inventory.ko.md`에 gap으로 남긴다.
4. 샘플 코드는 사용자가 따라 할 공개 API 예시이므로 Java runtime 내부 package나 private bridge에
   의존하지 않는다.
5. 한 샘플의 inventory, 구현, `run_sample.sh`, 전체 sample runner, 문서 갱신, Codex 에이전트 리뷰가
   끝나기 전에는 다음 샘플로 넘어가지 않는다.

## 기준

1. 공통 샘플 문서:
   - `framework/doc/framework/common/sample/README.ko.md`
   - `framework/doc/framework/common/sample/<sample>/README.ko.md`
   - `framework/doc/framework/common/sample/event/*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/samples/<Sample>/`
   - `framework/doc/framework/dotnet/guide/samples/*.ko.md`
3. Kotlin public surface:
   - `framework/doc/framework/kotlin/`
   - `framework/languages/java/zlink-framework-core/`
   - `framework/languages/java/zlink-framework-kotlin/`
   - `framework/languages/java/zlink-framework-testkit/`
4. Kotlin 대상:
   - `framework/languages/java/samples/kotlin/<Sample>/`
   - `framework/languages/java/samples/run_samples.sh`

공통 샘플 문서는 모든 언어의 계약 기준이다. `.NET` 샘플과 공통 문서가 충돌하면 공통 문서를 우선하고,
필요하면 `.NET` 문서나 구현의 drift를 별도 이슈로 기록한다.

## 현재 Kotlin 작업물 처리 원칙

현재 Kotlin 샘플은 6개 샘플 루트가 모두 있다. 공통 문서는 `Bingo`, `TicTacToe`, `SupportChat`,
`DeliveryDispatch`, `GameQuest`를 full 구조 구현으로, `ShoppingMall`을 compact 구현으로 분류한다.
따라서 기본 전략은 기존 구현 보존이지만, `ShoppingMall`은 full 구조 승격 여부를 inventory로 먼저
판정한다. 현재 작업 중인 `DeliveryDispatch` 변경처럼 role wiring, runner, coroutine handler 흐름이
바뀌는 샘플은 inventory와 실제 runner 결과로 완료 여부를 판정한다.

| 샘플 | 현재 판단 | 작업 원칙 |
|------|-----------|-----------|
| `Bingo` | full 구조 유지 대상 | Protobuf, Session/Api/Play/Registry, actor/session, Entry Spot, room Spot, timer, push 흐름을 inventory로 고정한다. |
| `TicTacToe` | full 구조 유지 대상 | Api/Play scale-out, Redis room route store, final state 검증을 `.NET`과 공통 문서에 맞춰 확인한다. |
| `SupportChat` | full 구조 유지 대상 | Session/Api/Support/Registry와 Probe, conversation Spot, reconnect, idle timer, close 흐름을 유지한다. |
| `DeliveryDispatch` | full 구조 검증 대상 | DispatchApi, DispatchCenter, Courier, Tracking, Session, Registry, Probe role wiring과 delivery reassignment 검증을 현재 변경과 함께 재확인한다. |
| `ShoppingMall` | compact에서 full 승격 검증 대상 | 공통 문서는 Kotlin `ShoppingMall`을 compact 구현으로 분류한다. 현재 코드가 CommerceApi, OrderWorkflow, Registry와 event sourced workflow/projection 경계를 full 구조로 갖췄는지 inventory로 확인하고, 맞다면 공통 문서 갱신을 함께 처리한다. |
| `GameQuest` | full 구조 유지 대상 | GameApi, QuestMission, Registry와 quest event fanout, aggregate/projection 책임을 유지한다. |

## 표준 Kotlin Sample 구조

```text
framework/languages/java/samples/kotlin/<Sample>/
|-- Shared/
|   `-- src/main/kotlin/.../shared/
|-- Client/
|   |-- build.gradle.kts
|   `-- src/main/kotlin/.../client/
|-- Server/
|   |-- Configuration/
|   |-- <Role>/
|   |   |-- build.gradle.kts
|   |   `-- src/main/kotlin/.../<role>/
|   `-- <OtherRole>/
|-- logs/
|   `-- .gitignore
|-- sample-porting-inventory.ko.md
`-- run_sample.sh
```

Gradle multi-project 구성은 언어 관례를 따른다. 하지만 여러 server role을 하나의 application에서
`--role` 옵션으로 바꿔 실행하는 방식은 `.NET` 역할 분리와 다르므로 완료로 보지 않는다.

## Inventory 산출물

각 샘플은 `.NET` 기준 파일과 공통 샘플 요구를 Kotlin 파일에 매핑한 문서를 둔다.

```text
framework/languages/java/samples/kotlin/<Sample>/sample-porting-inventory.ko.md
```

형식:

| 기준 | Kotlin 대응 | 분류 | 상태 | 비고 |
|------|-------------|------|------|------|
| `.NET: Client/<Sample>ClientScenario.cs` | `Client/src/main/kotlin/...ClientScenario.kt` | client-scenario | done/gap | self-check marker |
| `.NET: Server/<Role>/...` | `Server/<Role>/src/main/kotlin/...` | server-role | done/gap | role 책임 |
| `common: message <Name>` | `Shared/src/main/kotlin/...` | shared-contract | done/gap | field 대응 |
| `common: validation <Step>` | `run_sample.sh` / client scenario | validation | done/gap | 로그와 PASS 조건 |

규칙:

- `.NET` 기준 파일 목록은 `find framework/languages/dotnet/samples/<Sample> -type f`로 생성하고,
  `bin`, `obj`, `logs` 산출물은 제외한다.
- Kotlin에서 필요 없는 `.NET` 파일도 행을 지우지 않는다. 상태를 `not-needed`로 두고 근거를 적는다.
- 공통 문서의 메시지 필드는 Kotlin data class, value type, enum과 일대일로 대조한다.
- `pending` 상태가 남아 있으면 샘플 완료로 보지 않는다.

## 진행 순서

| 순서 | 샘플 | 완료 조건 |
|------|------|-----------|
| 1 | `Bingo` | 기존 full 구조를 inventory로 고정하고 Protobuf, actor/session, Entry Spot, timer, push 검증 재실행 |
| 2 | `TicTacToe` | manual endpoint scale-out, Redis room route, client final state 검증 재실행 |
| 3 | `SupportChat` | conversation Spot, reconnect, idle timer, close, probe 검증 재실행 |
| 4 | `DeliveryDispatch` | 현재 변경을 포함해 6개 server role과 probe, reassignment, tracking fanout 검증 통과 |
| 5 | `ShoppingMall` | CommerceApi/OrderWorkflow/Registry, event sourced order workflow, projection 검증 통과 |
| 6 | `GameQuest` | GameApi/QuestMission/Registry, quest event fanout, aggregate/projection 검증 통과 |

## 샘플 단위 절차

1. `.NET` 샘플 파일 목록과 공통 샘플 문서를 읽고 inventory를 만든다.
2. 현재 Kotlin 파일을 유지, 이동, 재작성, 삭제 후보로 분류한다.
3. Shared contract와 codec extension 등록을 먼저 맞춘다.
4. coroutine handler와 suspending API 사용이 public contract에 맞는지 확인한다.
5. server role project와 package 책임을 `.NET`과 공통 문서에 맞춘다.
6. client scenario가 실제 서버 process를 호출하고 성공 marker를 검증하게 한다.
7. `run_sample.sh`가 Gradle build, port allocation, readiness, cleanup, 실패 로그 출력을 처리하게 한다.
8. `framework/doc/framework/kotlin/guide/samples/` 문서가 실제 구조와 맞는지 갱신한다.
9. 개별 `run_sample.sh`와 상위 sample runner를 실행한다.
10. 실패하면 같은 샘플 안에서 원인을 고치고 다시 실행한다. Java internal bridge나 runner-only helper로
    덮지 않는다.
11. Codex 에이전트 리뷰를 받고 이슈가 없어질 때까지 수정과 재검증을 반복한다.

## Codex 에이전트 리뷰 요청

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/java/samples/kotlin/<Sample>
기준: framework/languages/dotnet/samples/<Sample>, framework/doc/framework/common/sample

확인할 것:
1. 공통 샘플 문서의 역할, 메시지, 검증 흐름이 Kotlin에 빠짐없이 대응되는가.
2. .NET 기준의 Shared/Client/Server/<Role> 책임이 Kotlin에서 같은 의미로 분리되었는가. .NET에 Probe가 있는 샘플은 Probe 검증도 대응되는가.
3. sample-porting-inventory.ko.md가 .NET 기준 파일과 공통 요구를 빠짐없이 담는가.
4. public contract에 없는 기능을 Java internal API, private bridge, 샘플 전용 adapter로 우회하지 않았는가.
5. coroutine/suspending 사용이 호출자에게 framework 내부 순서나 transport detail을 노출하지 않는가.
6. run_sample.sh와 전체 sample runner가 실제 process 경계, readiness, cleanup, 실패 로그 출력을 검증하는가.
7. Kotlin guide의 설명이 실제 샘플 코드와 맞는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```
