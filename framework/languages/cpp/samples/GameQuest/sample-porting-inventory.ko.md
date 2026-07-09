# GameQuest C++ sample porting inventory

최신 검증: 2026-07-07에 Redis 지원 root build에서 아래 명령을 실행했고,
`PASS GameQuest.Cpp`, `gamequest sample result=passed`, `gamequest-server-evidence=completed`,
`gamequest=completed`를 확인했다.

```bash
env CMAKE_BUILD_PARALLEL_LEVEL=1 GAMEQUEST_KEEP_RUN_DIR=1 nice -n 10 \
  timeout 420s framework/languages/cpp/samples/GameQuest/run_sample.sh
```

| 기준 | C++ 위치 | 상태 | 설명 |
|------|----------|------|------|
| `.NET Shared/Messages.cs` | `Shared/Contracts/messages.hpp` | done | gameplay request, quest progress, route request/reply, assertion DTO를 JSON 직렬화로 매핑한다. |
| `.NET Server/Configuration/SampleConfiguration.cs` | `Server/Configuration/sample_topology.hpp` | done | api-a/api-b, mission-a/mission-b endpoint와 Redis location store prefix를 환경 변수로 받는다. |
| `.NET GameApi session` | `Server/GameApi/main.cpp` | done | stream session이 client request를 받고, 먼저 owner QuestMission에 player quest Spot 생성을 보장한 뒤 public spot route request로 progress sync와 gameplay event 적용을 보낸다. |
| `.NET PlayerQuestSpotProvisioner.cs` | `Server/GameApi/main.cpp`, `Server/QuestMission/main.cpp` | done | C++는 `EnsurePlayerQuestSpotReq` channel request와 `spot_node_manager_t::get_or_create_spot`으로 player owner Spot을 보장한다. |
| `.NET PlayerQuestSpot.cs` | `Server/QuestMission/main.cpp` | done | `player_quest_spot_t`가 player id별 Spot으로 생성되고, gameplay event 적용, progress sync, progress 조회 handler를 소유한다. |
| `.NET QuestMission role` | `Server/QuestMission/main.cpp` | done | mission-a/mission-b가 owner channel과 spot route mesh를 열고, player owner Spot에서 quest projection과 completion notify를 처리한다. |
| `.NET Client/GameQuestClientScenario.cs` | `Client/gamequest_client_scenario.hpp` | done | Alice/Bob gameplay, duplicate idempotency, offline progress sync, completion notify, server evidence를 검증한다. |
| C++ sample runner convention | `run_sample.sh` | done | 필요한 CMake target을 빌드하고, `RUN_DIR`, `stdbuf`, Redis location store, GameApi caller-side spot router, QuestMission spot route/router/pub endpoints, flow trace grep, `ZLINK_CPP_BUILD_DIR` build dir를 사용한다. |

## .NET 파일 대응 보강

| .NET 파일 | C++ 대응 | 상태 | 설명 |
|-----------|----------|------|------|
| `Client/GameQuest.Client.csproj`; `Client/Program.cs`; `README.ko.md`; `Shared/GameQuest.Shared.csproj` | `Client/main.cpp`; `Client/gamequest_client_scenario.hpp`; `README.ko.md`; `Shared/Contracts/messages.hpp`; `framework/languages/cpp/CMakeLists.txt` | done | client entry, scenario, README, shared project/contract target을 C++ client/header/CMake로 대응한다. |
| `Server/Configuration/GameQuest.Server.Configuration.csproj`; `RedisJsonStore.cs`; `SampleFlowLog.cs` | `Server/Configuration/location_store.hpp`; `Server/Configuration/sample_names.hpp`; `Server/Configuration/sample_topology.hpp`; `sample_log_dir.hpp` | done | Redis location store, endpoint/name 설정, flow log 경로를 C++ configuration과 runner log convention으로 대응한다. |
| `Server/GameApi/GameQuest.GameApi.csproj`; `Server/GameApi/Program.cs`; `Session/GameQuestSession.cs`; `Session/GameQuestSessionHandlers.cs`; `Session/GameQuestSessionRegistry.cs` | `Server/GameApi/main.cpp` | done | GameApi executable이 stream session, session registry, gameplay request handling, player owner Spot 보장 요청을 맡는다. |
| `Server/GameApi/Application/GameplayActionService.cs`; `Server/GameApi/Domain/GameplayDomain.cs`; `Infrastructure/Store/GameQuestStores.cs`; `Infrastructure/ZLink/GameplayEventOwnerDispatcher.cs`; `Infrastructure/Http/HttpQuestProgressSynchronizer.cs` | `Server/GameApi/main.cpp`; `Shared/Contracts/messages.hpp` | done | gameplay action/domain, progress sync, owner dispatch, store/projection request는 C++ GameApi role의 typed request flow와 shared DTO로 대응한다. |
| `Server/QuestMission/GameQuest.QuestMission.csproj`; `Server/QuestMission/Program.cs`; `Application/QuestEventProcessor.cs`; `Application/QuestOwnerRouter.cs`; `Domain/QuestDomain.cs`; `Infrastructure/Store/QuestStores.cs`; `Infrastructure/Http/GameApiQuestClients.cs` | `Server/QuestMission/main.cpp`; `Shared/Contracts/messages.hpp` | done | QuestMission executable이 player owner Spot, quest aggregate/projection, completion fanout, owner routing, server evidence를 맡는다. |

## C++ public API 사용 경계

GameApi와 QuestMission은 공개 framework API만 사용한다. GameApi는 `route_client_t::request_to_node`의
spot 대상 overload로 owner Spot에 요청하고, QuestMission은 `spot_node_manager_t::get_or_create_spot`으로
player Spot을 생성한다. 샘플 코드에서 raw frame, private helper, 메시지별 codec 등록 우회는 사용하지
않는다.
