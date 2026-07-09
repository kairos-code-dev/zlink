# C++ Framework Sample .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/samples`의 정본 샘플 6종을 기준 구현으로 삼아
`framework/languages/cpp/samples`가 같은 역할 분리, 메시지 계약, client self-check, 실행 검증
수준을 제공하도록 정렬하는 절차를 정의한다.

샘플 포팅의 기준 계약은 공통 샘플 문서다. `.NET` 샘플은 파일 구조와 검증 흐름을 비교하기 위한
기준 구현이며, `.NET`에 있다는 이유만으로 C++ public API를 새로 추가하지 않는다.

## 완료 기준

1. C++ 샘플 6종이 공통 샘플 문서의 서버 역할, 메시지 흐름, 검증 순서를 같은 의미로 구현한다.
2. `.NET`의 `Shared`, `Client`, `Server/<Role>` 책임이 C++에 대응된다. `.NET` 샘플에 `Probe`가
   있는 경우에만 C++에도 같은 검증 책임을 둔다.
3. C++ public framework API로 구현할 수 없는 항목은 내부 helper, raw frame 조작, 샘플 전용
   우회로 메우지 않고 `sample-porting-inventory.ko.md`에 gap으로 남긴다.
4. 샘플 코드는 사용자가 따라 할 공개 API 예시이므로 `runtime`, private header, 테스트 전용 adapter에
   의존하지 않는다.
5. 한 샘플의 inventory, 구현, `run_sample.sh`, `run_samples.sh` 포함 검증, 문서 갱신, Codex 에이전트
   리뷰가 끝나기 전에는 다음 샘플로 넘어가지 않는다.
6. 작업 중 버그가 드러나면 우회하지 않는다. 실패 로그와 재현 조건을 먼저 좁히고, 원인을 수정한 뒤
   같은 문제가 다시 숨지 않도록 회귀테스트나 sample runner 검증을 추가한다.
7. 서버 구동에는 역할별 build와 framework 초기화에 필요한 충분한 시간을 준다. 다만 로컬 샘플 테스트에서
   허용 가능한 준비 시간을 넘어 계속 대기해야 통과하는 경우는 느린 환경 문제가 아니라 버그로 판정하고,
   readiness, role wiring, port allocation, dependency 초기화 원인을 찾아 수정한다.

## 기준

1. 공통 샘플 문서:
   - `framework/doc/framework/common/sample/README.ko.md`
   - `framework/doc/framework/common/sample/<sample>/README.ko.md`
   - `framework/doc/framework/common/sample/event/*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/samples/<Sample>/`
   - `framework/doc/framework/dotnet/guide/samples/*.ko.md`
3. C++ public surface:
   - `framework/languages/cpp/framework/include/`
   - `framework/doc/framework/cpp/`
   - `framework/languages/cpp/tests/`
4. C++ 대상:
   - `framework/languages/cpp/samples/<Sample>/`
   - `framework/languages/cpp/samples/run_samples.sh`

공통 샘플 문서는 모든 언어의 계약 기준이다. `.NET` 샘플과 공통 문서가 충돌하면 공통 문서를 우선하고,
필요하면 `.NET` 문서나 구현의 drift를 별도 이슈로 기록한다.

## 현재 C++ 작업물 처리 원칙

현재 C++ 샘플은 6개 샘플 루트가 모두 있으며, role별 디렉터리와 sample runner evidence를 기준으로
판정한다. `DeliveryDispatch`, `ShoppingMall`, `GameQuest`는 compact 구현에서 full 역할 구조로
승격된 상태이므로 이후 작업에서는 inventory와 runner 증거를 기준으로 회귀를 막는다.

| 샘플 | 현재 판단 | 작업 원칙 |
|------|-----------|-----------|
| `Bingo` | full 구조 완료 | Protobuf, Session/Api/Play, Redis location store, Entry Spot, room Spot, timer, bound push 흐름을 `.NET`과 공통 문서에 맞췄다. `sample-porting-inventory.ko.md`가 남은 gap 없음 상태를 기록한다. |
| `TicTacToe` | full 구조 완료 | Api 2개, Play 2개, 수동 endpoint, Redis room route store, public resolver 흐름을 유지하고 `.NET` client self-check와 로그 marker를 대조했다. `sample-porting-inventory.ko.md`가 남은 gap 없음 상태를 기록한다. |
| `SupportChat` | conversation Spot 승격 완료 | Session, Api, Support, Probe가 Redis location store로 자동 연결되고, conversation Spot, per-conversation agent actor, reconnect, idle timer, close 검증을 유지한다. `sample-porting-inventory.ko.md`가 `PASS SupportChat.Cpp` 통과를 기록한다. |
| `DeliveryDispatch` | full 역할 승격 완료 | DispatchApi, DispatchCenter, CustomerGateway, CourierSession, CourierGateway, CourierActorNode, Tracking, Probe가 `.NET`과 공통 문서의 책임을 갖는다. C++는 registry role 대신 Redis location store를 사용하며, `sample-porting-inventory.ko.md`가 runner 통과를 기록한다. |
| `ShoppingMall` | order Spot 승격 완료 | CommerceApi, OrderWorkflow, Redis location store, order별 `OrderWorkflowSpot`, event sourced workflow/projection 경계를 full 구조로 맞췄다. `sample-porting-inventory.ko.md`가 `PASS ShoppingMall.Cpp` 통과를 기록한다. |
| `GameQuest` | player owner Spot 승격 완료 | GameApi, QuestMission, Redis location store, player quest Spot, fanout, quest aggregate, projection 경계를 full 구조로 맞췄다. 2026-07-07 `run_sample.sh`에서 `PASS GameQuest.Cpp`를 확인했다. |

2026-07-07에는 `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 900s
framework/languages/cpp/samples/run_samples.sh`를 실행해 여섯 샘플을 상위 runner에서 함께 검증했다.
출력에서 CTest sample gate 통과와 `PASS TicTacToe.Cpp`, `PASS SupportChat.Cpp`,
`PASS GameQuest.Cpp`, `PASS ShoppingMall.Cpp`, `bingo full client/server self-check completed`,
`deliverydispatch sample result=passed`를 확인했다. 이 검증은 상위 runner가 개별 샘플을 빠뜨리지
않는지 확인하기 위한 증거이며, 이후 샘플 변경 때 같은 명령을 다시 실행한다.

## 표준 C++ Sample 구조

```text
framework/languages/cpp/samples/<Sample>/
|-- Shared/
|   `-- Contracts/
|-- Client/
|   |-- main.cpp
|   `-- <sample>_client_scenario.hpp
|-- Server/
|   |-- Configuration/
|   |-- <Role>/
|   |   |-- main.cpp
|   |   |-- Handlers/
|   |   |-- Infrastructure/
|   |   `-- Support/
|   `-- <OtherRole>/
|-- logs/
|   `-- .gitignore
|-- README.ko.md
|-- sample-porting-inventory.ko.md
`-- run_sample.sh
```

언어 특성상 header/source 분리는 C++ 관례를 따른다. 하지만 여러 server role을 하나의 실행 파일에서
`--role` 옵션으로 바꿔 실행하는 방식은 `.NET` 역할 분리와 다르므로 완료로 보지 않는다.

## Inventory 산출물

각 샘플은 `.NET` 기준 파일과 공통 샘플 요구를 C++ 파일에 매핑한 문서를 둔다.

```text
framework/languages/cpp/samples/<Sample>/sample-porting-inventory.ko.md
```

형식:

| 기준 | C++ 대응 | 분류 | 상태 | 비고 |
|------|----------|------|------|------|
| `.NET: Client/<Sample>ClientScenario.cs` | `Client/<sample>_client_scenario.hpp` | client-scenario | done/gap | self-check marker |
| `.NET: Server/<Role>/...` | `Server/<Role>/...` | server-role | done/gap | role 책임 |
| `common: message <Name>` | `Shared/Contracts/...` | shared-contract | done/gap | field 대응 |
| `common: validation <Step>` | `run_sample.sh` / client scenario | validation | done/gap | 로그와 PASS 조건 |

규칙:

- `.NET` 기준 파일 목록은 `find framework/languages/dotnet/samples/<Sample> -type f`로 생성하고,
  `bin`, `obj`, `logs` 산출물은 제외한다.
- `.NET` 파일 하나가 C++ 여러 파일로 나뉘면 대응 파일 칸에 모두 적는다.
- C++에서 필요 없는 파일도 행을 지우지 않는다. 상태를 `not-needed`로 두고 근거를 적는다.
- `pending` 상태가 남아 있으면 샘플 완료로 보지 않는다.

## 진행 순서

| 순서 | 샘플 | 완료 조건 |
|------|------|-----------|
| 1 | `Bingo` | 기존 full 구조를 inventory로 고정하고 Protobuf, actor/session, Entry Spot, timer, push 검증 통과 |
| 2 | `TicTacToe` | manual endpoint scale-out, Redis room route, client final state 검증 통과 |
| 3 | `SupportChat` | conversation Spot, reconnect, idle timer, close, probe 검증 통과 |
| 4 | `DeliveryDispatch` | compact 흔적 없이 server role과 probe가 분리되고 delivery reassignment와 tracking fanout 검증 통과 |
| 5 | `ShoppingMall` | CommerceApi/OrderWorkflow/Redis location store 분리와 event sourced order workflow, projection 검증 통과 |
| 6 | `GameQuest` | GameApi/QuestMission/Redis location store와 player quest Spot 분리, quest event fanout, aggregate/projection 검증 통과 |

## 샘플 단위 절차

1. `.NET` 샘플 파일 목록과 공통 샘플 문서를 읽고 inventory를 만든다.
2. 현재 C++ 파일을 유지, 이동, 재작성, 삭제 후보로 분류한다.
3. Shared contract를 먼저 맞춘다.
4. server role을 `.NET`과 공통 문서의 책임에 맞춰 분리한다.
5. client scenario가 실제 서버 process를 호출하고 성공 marker를 검증하게 한다.
6. `run_sample.sh`가 build, port allocation, readiness, cleanup, 실패 로그 출력을 처리하게 한다.
7. 언어별 guide와 샘플 README가 실제 구조와 맞는지 갱신한다.
8. 개별 `run_sample.sh`와 상위 `run_samples.sh`를 실행한다.
9. 실패하면 같은 샘플 안에서 원인을 고치고 다시 실행한다. sleep 추가, retry 횟수 증가, raw frame 우회로
   덮지 않는다. 버그가 확인되면 재현 조건을 남기고 회귀테스트나 runner 검증을 추가한 뒤 수정한다.
   서버 준비 대기는 build와 초기화에 필요한 범위로 제한하며, 로컬 테스트에서 납득하기 어려운 긴 대기가
   필요하면 readiness나 서버 시작 경로의 버그로 보고 원인을 추적한다.
10. Codex 에이전트 리뷰를 받고 이슈가 없어질 때까지 수정과 재검증을 반복한다.

## Codex 에이전트 리뷰 요청

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/cpp/samples/<Sample>
기준: framework/languages/dotnet/samples/<Sample>, framework/doc/framework/common/sample

확인할 것:
1. 공통 샘플 문서의 역할, 메시지, 검증 흐름이 C++에 빠짐없이 대응되는가.
2. .NET 기준의 Shared/Client/Server/<Role> 책임이 C++에서 같은 의미로 분리되었는가. .NET에 Probe가 있는 샘플은 Probe 검증도 대응되는가.
3. sample-porting-inventory.ko.md가 .NET 기준 파일과 공통 요구를 빠짐없이 담는가.
4. compact 구현을 full 구조로 과장하지 않았는가.
5. public contract에 없는 기능을 private API, raw frame, 샘플 전용 adapter로 우회하지 않았는가.
6. run_sample.sh와 run_samples.sh가 실제 process 경계, readiness, cleanup, 실패 로그 출력을 검증하는가.
7. README와 guide의 설명이 실제 샘플 코드와 맞는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```
