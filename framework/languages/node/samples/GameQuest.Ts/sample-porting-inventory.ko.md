# GameQuest.Ts .NET 기준 포팅 Inventory

이 문서는 `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`의
샘플 단위 절차에 따라 현재 Node GameQuest 샘플을 공통 event 샘플 문서와
`.NET` 기준 구현에 매핑한다. `gap`은 완료 판정이 아니라 다음 수정 대상이다.

## 파일과 역할 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/GameQuestClientScenario.cs` | `Client/gamequest-client-scenario.ts` | client-scenario | done | quest subscribe, progress notify, completion notify, idempotent action, offline progress, projection rebuild, snapshot sync, server evidence를 검증한다. |
| `.NET: Client/Program.cs` | `Client/main.ts` | client-entry | done | API A/B HTTP client와 stream connector를 만들고 scenario를 실행한다. |
| `.NET: Client/Configuration/SampleConfiguration.cs` | `Client/Configuration/sample-config.ts`, `Shared/Configuration/sample-names.ts` | configuration | done | runner가 넘긴 API/stream endpoint와 channel 이름을 읽는다. |
| `.NET: Server/Configuration/SampleConfiguration.cs` | `Server/Configuration/sample-config.ts` | configuration | done | registry, API stream, mission route endpoint를 읽는다. |
| `.NET: Server/Configuration/SampleFlowLog.cs` | role module의 message-flow trace 설정 | logging | not-needed | Node는 module별 trace log file과 label을 직접 설정한다. |
| `.NET: Server/Registry/Program.cs` | `Server/Registry/registry-module.ts`, `Server/main.ts --role registry` | server-role | done | registry host를 독립 role로 실행한다. |
| `.NET: Server/GameApi/Program.cs` | `Server/GameApi/game-api-module.ts`, `Server/GameApi/game-api-server.ts` | server-role | done | HTTP action API와 stream session endpoint를 제공한다. |
| `.NET: Server/GameApi/Application/GameplayActionService.cs` | `Server/GameApi/Application/gameplay-action-service.ts` | application | done | HTTP route는 gameplay service에 위임하고 action publish와 stream notify 흐름을 application service에 모았다. |
| `.NET: Server/GameApi/Domain/GameplayDomain.cs` | `Server/GameApi/Domain/gameplay-domain.ts` | domain | done | gameplay command 생성을 domain helper로 분리했다. |
| `.NET: Server/GameApi/Infrastructure/Http/HttpQuestProgressSynchronizer.cs` | `Server/GameApi/game-api-server.ts` | http-adapter | done | projection delete/rebuild와 snapshot sync용 self-check HTTP endpoint를 제공한다. |
| `.NET: Server/GameApi/Infrastructure/Store/GameQuestStores.cs` | `Server/Shared/Store/quest-progress-store.ts` | store | done | gameplay facts, quest events, projection, session binding 상태를 file store로 공유한다. |
| `.NET: Server/GameApi/Infrastructure/ZLink/GameplayEventPublisher.cs` | `Server/GameApi/Infrastructure/ZLink/gameplay-event-publisher.ts` | zlink-publisher | done | player id로 owner mission route를 계산하고 route client 요청과 retry를 ZLink adapter로 분리했다. |
| `.NET: Server/GameApi/Session/GameQuestSession.cs` | `Server/GameApi/gamequest-session.ts` | stream-session | done | player subscribe, progress query, sync request를 처리한다. |
| `.NET: Server/GameApi/Session/GameQuestSessionHandlers.cs` | `Server/GameApi/gamequest-session.ts` | stream-handler | done | stream request handlers를 session 안에서 처리한다. |
| `.NET: Server/GameApi/Session/GameQuestSessionRegistry.cs` | `Server/GameApi/player-session-directory.ts` | session-store | done | player별 stream session binding을 관리한다. |
| `.NET: Server/QuestMission/Application/QuestEventProcessor.cs` | `Server/QuestMission/Application/quest-event-processor.ts` | application | done | channel handler는 processor에 위임하고 quest command/query 처리를 application service에 모았다. |
| `.NET: Server/QuestMission/Application/QuestOwnerRouter.cs` | `Server/QuestMission/Application/quest-owner-router.ts`, `Server/Configuration/gamequest-routing.ts` | owner-routing | done | player id를 같은 규칙으로 mission A/B에 나누고 mission route mesh가 해당 owner로 요청을 보낸다. |
| `.NET: Server/QuestMission/Domain/QuestDomain.cs` | `Server/QuestMission/Domain/quest-domain.ts` | domain | done | quest id 매핑과 진행 상태 판정을 domain helper로 분리했다. |
| `.NET: Server/QuestMission/Infrastructure/Http/GameApiQuestClients.cs` | `Server/QuestMission/Handlers/query-and-self-check-handlers.ts` | http-adapter | done | snapshot sync와 projection/self-check 보조 요청을 처리한다. |
| `.NET: Server/QuestMission/Infrastructure/Store/QuestStores.cs` | `Server/Shared/Store/quest-progress-store.ts` | store | done | quest event stream과 projection을 file store로 유지한다. |
| `.NET: Server/QuestMission/Infrastructure/ZLink/GameplayEventHandler.cs` | `Server/QuestMission/Handlers/*` | fanout-handler | done | kill, item, mission, feature, area event를 받아 quest 진행으로 반영한다. |
| `.NET: Server/QuestMission/Infrastructure/ZLink/PlayerQuestSpotProvisioner.cs` | `Server/QuestMission/Infrastructure/ZLink/player-quest-spot-provisioner.ts` | spot-support | done | `ZLinkSpotManager.getOrCreate`로 player별 Spot을 생성하거나 확보한다. |
| `.NET: Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/PlayerQuestSpot.cs` | `Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot.ts` | spot | done | player별 Spot 생성 payload를 받고 readiness evidence를 남긴다. |
| `.NET: Server/QuestMission/Program.cs` | `Server/QuestMission/gamequest-quest-module.ts`, `Server/main.ts --role mission-a|mission-b` | server-role | done | QuestMission role 두 instance를 실행한다. |
| `.NET: Shared/Messages.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | gameplay action, quest progress, subscription, projection, snapshot 메시지를 정의한다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | registry, mission A/B, API A/B, client self-check 순서로 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Unix PowerShell에서는 검증된 Linux runner를 호출해 같은 process 경계와 self-check marker를 사용한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: Registry 1 instance` | `Server/Registry/registry-module.ts` | server-role | done | registry host를 하나 실행한다. |
| `common: GameApi 2 instances` | `Server/main.ts --role api-a|api-b`, `run_sample.sh` | server-role | done | API A/B가 HTTP와 stream endpoint를 각각 연다. |
| `common: QuestMission 2 instances` | `Server/main.ts --role mission-a|mission-b`, `run_sample.sh` | server-role | done | mission A/B route endpoints를 registry discovery로 노출한다. |
| `common: GameApi gameplay event fanout` | `Server/GameApi/game-api-module.ts`, `Server/GameApi/game-api-server.ts` | fanout | done | action HTTP handler가 gameplay event를 fanout으로 publish한다. |
| `common: QuestMission fanout subscription` | `Server/QuestMission/gamequest-quest-module.ts`, `Server/QuestMission/Handlers/*` | fanout-handler | done | 여러 gameplay event type을 받아 quest projection을 갱신한다. |
| `common: PlayerQuestSpot owner routing` | `Server/QuestMission/Application/quest-owner-router.ts`, `Server/QuestMission/Infrastructure/ZLink/player-quest-spot-provisioner.ts` | spot | done | mission route mesh가 player owner mission으로 요청을 보내고 owner mission에서 player별 Spot을 생성하거나 확보한다. |
| `common: QuestEventStore` | `Server/Shared/Store/quest-progress-store.ts` | event-store | done | player/quest별 quest event stream을 유지한다. |
| `common: QuestReadModelStore` | `Server/Shared/Store/quest-progress-store.ts` | projection | done | client 조회와 notify용 projection을 유지한다. |
| `common: GameplayStateStore` | `Server/Shared/Store/quest-progress-store.ts` | gameplay-state | done | kill, item, mission, feature, area 누적 fact를 저장한다. |
| `common: QuestSubscriptionStore` | `Server/GameApi/player-session-directory.ts`, `Server/Shared/Store/quest-progress-store.ts` | subscription | done | player별 stream binding과 projection replay를 연결한다. |
| `common: message gameplay actions` | `Shared/Contracts/messages.ts` | shared-contract | done | kill, collect, complete mission, unlock feature, enter area 요청을 둔다. |
| `common: message SubscribeQuest/GetQuestProgress` | `Shared/Contracts/messages.ts` | shared-contract | done | stream subscription과 progress query 메시지를 둔다. |
| `common: message SyncQuestProgress` | `Shared/Contracts/messages.ts` | shared-contract | done | gameplay snapshot 보정 요청과 응답을 둔다. |
| `common: message QuestProgressNotify/QuestCompletedNotify` | `Shared/Contracts/messages.ts` | shared-contract | done | progress와 completion push payload를 둔다. |
| `common: validation progress notify` | `Client/gamequest-client-scenario.ts` | validation | done | first-hunt 진행 알림을 stream connector wait로 검증한다. |
| `common: validation reward idempotency` | `Client/gamequest-client-scenario.ts` | validation | done | 중복 kill action이 같은 event id를 반환하고 reward는 중복 지급되지 않는 흐름을 검증한다. |
| `common: validation offline projection catch-up` | `Client/gamequest-client-scenario.ts` | validation | done | offline player progress가 subscribe 시 active quest로 복원되는지 확인한다. |
| `common: validation projection rebuild` | `Client/gamequest-client-scenario.ts` | validation | done | projection 삭제 뒤 rebuild 결과를 확인한다. |
| `common: validation snapshot reconciliation` | `Client/gamequest-client-scenario.ts` | validation | done | publish 없이 누적된 gameplay fact를 sync로 보정하는지 확인한다. |
| `common: validation scale-out logs` | `run_sample.sh` | validation | done | API A/B와 mission A/B flow 로그가 생겼는지 확인한다. |
| `common: success marker gamequest-server-evidence=completed` | `Client/gamequest-client-scenario.ts` | validation | done | server evidence 통과 뒤 출력한다. |
| `common: success marker gamequest=completed` | `Client/main.ts` | validation | done | scenario 완료 뒤 출력한다. |

## 남은 확인

- PowerShell runner의 Windows 전용 경로는 별도 Windows 환경에서 확인해야 한다.
