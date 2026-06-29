# Node Framework Sample .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/samples`의 정본 샘플 6종을 기준 구현으로 삼아
`framework/languages/node/samples`가 같은 역할 분리, 메시지 계약, client self-check, 실행 검증 수준을
제공하도록 정렬하는 절차를 정의한다.

샘플 포팅의 기준 계약은 공통 샘플 문서다. `.NET` 샘플은 Node/NestJS module, server role, client
scenario, runner 검증을 비교하기 위한 기준 구현으로 사용한다.

## 완료 기준

1. Node 샘플 6종이 공통 샘플 문서의 서버 역할, 메시지 흐름, 검증 순서를 같은 의미로 구현한다.
2. `.NET`의 `Shared`, `Client`, `Server/<Role>` 책임이 Node에 대응된다. `.NET` 샘플에 `Probe`가
   있는 경우에만 Node에도 같은 검증 책임을 둔다.
3. Node public framework API로 구현할 수 없는 항목은 `dist/runtime`, private module, raw frame 조작,
   샘플 전용 우회로 메우지 않고 `sample-porting-inventory.ko.md`에 gap으로 남긴다.
4. 샘플 코드는 사용자가 따라 할 공개 API 예시이므로 `@zlink-systems/framework`,
   `@zlink-systems/nestjs`의 public entrypoint만 사용한다.
5. 한 샘플의 inventory, 구현, `run_sample.sh`, `npm run build`, 전체 sample runner, 문서 갱신,
   Codex 에이전트 리뷰가 끝나기 전에는 다음 샘플로 넘어가지 않는다.

## 기준

1. 공통 샘플 문서:
   - `framework/doc/framework/common/sample/README.ko.md`
   - `framework/doc/framework/common/sample/<sample>/README.ko.md`
   - `framework/doc/framework/common/sample/event/*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/samples/<Sample>/`
   - `framework/doc/framework/dotnet/guide/samples/*.ko.md`
3. Node public surface:
   - `framework/doc/framework/node/`
   - `framework/languages/node/src/`
   - `framework/languages/node/packages/`
   - `framework/languages/node/test/`
4. Node 대상:
   - `framework/languages/node/samples/<Sample>.Ts/`
   - `framework/languages/node/samples/run_samples.sh`
   - `framework/languages/node/test/contract/sample-regression.test.js`

공통 샘플 문서는 모든 언어의 계약 기준이다. `.NET` 샘플과 공통 문서가 충돌하면 공통 문서를 우선하고,
필요하면 `.NET` 문서나 구현의 drift를 별도 이슈로 기록한다.

## 현재 Node 작업물 처리 원칙

현재 Node 샘플은 6개 샘플 루트가 모두 있으며, `Bingo`, `TicTacToe`, `SupportChat`은 full 구조 대상으로
분류되어 있다. `DeliveryDispatch`, `ShoppingMall`, `GameQuest`는 공통 문서상 compact 구현으로 남아
있지만, 현재 작업 트리에는 role 분리와 server module 재배치가 진행 중인 파일이 있다. 따라서 이 세
샘플은 compact 완료로 인정하지 않고 full 승격 inventory를 먼저 작성한다.

| 샘플 | 현재 판단 | 작업 원칙 |
|------|-----------|-----------|
| `Bingo.Ts` | full 구조 유지 대상 | Protobuf, Session/Api/Play/Registry, actor/session, Entry Spot, room Spot, timer, push 흐름을 inventory로 고정한다. |
| `TicTacToe.Ts` | full 구조 유지 대상 | Api/Play scale-out, Redis room route store, final state 검증을 `.NET`과 공통 문서에 맞춰 확인한다. |
| `SupportChat.Ts` | full 구조 유지 대상 | Session/Api/Support/Registry, conversation Spot, reconnect, idle timer, close 흐름을 유지한다. |
| `DeliveryDispatch.Ts` | compact에서 full 승격 진행 중 | DispatchApi, DispatchCenter, Courier, Tracking, Session, Registry, Probe role split과 delivery reassignment 검증을 inventory로 확인한다. |
| `ShoppingMall.Ts` | compact에서 full 승격 진행 중 | CommerceApi, OrderWorkflow, Registry 역할과 event sourced workflow/projection 경계를 full 구조로 맞춘다. |
| `GameQuest.Ts` | compact에서 full 승격 진행 중 | GameApi, QuestMission, Registry 역할과 quest event fanout, aggregate/projection 경계를 full 구조로 맞춘다. |

## 표준 Node Sample 구조

```text
framework/languages/node/samples/<Sample>.Ts/
|-- Shared/
|   |-- Configuration/
|   `-- Contracts/
|-- Client/
|   |-- Configuration/
|   |-- main.ts
|   `-- <sample>-client-scenario.ts
|-- Server/
|   |-- Configuration/
|   |-- <Role>/
|   |   |-- main.ts
|   |   |-- Handlers/
|   |   |-- Infrastructure/
|   |   `-- <sample>-<role>-module.ts
|   `-- <OtherRole>/
|-- logs/
|   `-- .gitignore
|-- README.ko.md
|-- sample-porting-inventory.ko.md
|-- package.json
|-- tsconfig.json
`-- run_sample.sh
```

NestJS module 구성은 Node 관례를 따른다. 하지만 여러 server role을 하나의 module 안에서 옵션으로
바꿔 실행하거나, client가 runner-only endpoint에만 의존해 성공을 판정하는 방식은 완료로 보지 않는다.

## Inventory 산출물

각 샘플은 `.NET` 기준 파일과 공통 샘플 요구를 Node 파일에 매핑한 문서를 둔다.

```text
framework/languages/node/samples/<Sample>.Ts/sample-porting-inventory.ko.md
```

형식:

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/<Sample>ClientScenario.cs` | `Client/<sample>-client-scenario.ts` | client-scenario | done/gap | self-check marker |
| `.NET: Server/<Role>/...` | `Server/<Role>/...` | server-role | done/gap | role 책임 |
| `common: message <Name>` | `Shared/Contracts/messages.ts` | shared-contract | done/gap | field 대응 |
| `common: validation <Step>` | `run_sample.sh` / client scenario | validation | done/gap | 로그와 PASS 조건 |

규칙:

- `.NET` 기준 파일 목록은 `find framework/languages/dotnet/samples/<Sample> -type f`로 생성하고,
  `bin`, `obj`, `logs` 산출물은 제외한다.
- Node에서 필요 없는 `.NET` 파일도 행을 지우지 않는다. 상태를 `not-needed`로 두고 근거를 적는다.
- TypeScript object literal을 wire 계약으로 흩어 놓지 않는다. 공통 메시지는 `Shared/Contracts` 타입으로
  둔다.
- `pending` 상태가 남아 있으면 샘플 완료로 보지 않는다.

## 진행 순서

| 순서 | 샘플 | 완료 조건 |
|------|------|-----------|
| 1 | `Bingo.Ts` | 기존 full 구조를 inventory로 고정하고 Protobuf, actor/session, Entry Spot, timer, push 검증 재실행 |
| 2 | `TicTacToe.Ts` | manual endpoint scale-out, Redis room route, client final state 검증 재실행 |
| 3 | `SupportChat.Ts` | conversation Spot, reconnect, idle timer, close 검증 재실행 |
| 4 | `DeliveryDispatch.Ts` | 현재 변경을 포함해 6개 server role과 probe, reassignment, tracking fanout 검증 통과 |
| 5 | `ShoppingMall.Ts` | CommerceApi/OrderWorkflow/Registry, event sourced order workflow, projection 검증 통과 |
| 6 | `GameQuest.Ts` | GameApi/QuestMission/Registry, quest event fanout, aggregate/projection 검증 통과 |

## 샘플 단위 절차

1. `.NET` 샘플 파일 목록과 공통 샘플 문서를 읽고 inventory를 만든다.
2. 현재 Node 파일을 유지, 이동, 재작성, 삭제 후보로 분류한다.
3. Shared contract와 codec extension 등록을 먼저 맞춘다.
4. server role module과 provider 책임을 `.NET`과 공통 문서에 맞춘다.
5. client scenario가 실제 서버 process를 호출하고 성공 marker를 검증하게 한다.
6. `run_sample.sh`가 npm build, port allocation, readiness, cleanup, 실패 로그 출력을 처리하게 한다.
7. Node guide, 샘플 README, `sample-regression.test.js`가 실제 구조와 맞는지 갱신한다.
8. `npm run build`, 개별 `run_sample.sh`, 상위 sample runner를 실행한다.
9. 실패하면 같은 샘플 안에서 원인을 고치고 다시 실행한다. cleanup 중 transient error는 최종 PASS와
   로그 marker를 함께 보고 판정한다.
10. Codex 에이전트 리뷰를 받고 이슈가 없어질 때까지 수정과 재검증을 반복한다.

## Codex 에이전트 리뷰 요청

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/node/samples/<Sample>.Ts
기준: framework/languages/dotnet/samples/<Sample>, framework/doc/framework/common/sample

확인할 것:
1. 공통 샘플 문서의 역할, 메시지, 검증 흐름이 Node에 빠짐없이 대응되는가.
2. .NET 기준의 Shared/Client/Server/<Role> 책임이 Node에서 같은 의미로 분리되었는가. .NET에 Probe가 있는 샘플은 Probe 검증도 대응되는가.
3. sample-porting-inventory.ko.md가 .NET 기준 파일과 공통 요구를 빠짐없이 담는가.
4. compact 구현을 full 구조로 과장하지 않았는가.
5. public contract에 없는 기능을 dist/runtime, private module, raw frame, 샘플 전용 adapter로 우회하지 않았는가.
6. run_sample.sh와 전체 sample runner가 실제 process 경계, readiness, cleanup, 실패 로그 출력을 검증하는가.
7. sample-regression.test.js와 README, guide의 설명이 실제 샘플 코드와 맞는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```
