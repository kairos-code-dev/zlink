# GameQuest C++ sample porting inventory

| 기준 | C++ 위치 | 상태 | 설명 |
|------|----------|------|------|
| `.NET Shared/Messages.cs` | `Shared/Contracts/messages.hpp` | done | gameplay request, quest progress, route request/reply, assertion DTO를 JSON 직렬화로 매핑한다. |
| `.NET Server/Configuration/SampleConfiguration.cs` | `Server/Configuration/sample_topology.hpp` | done | api-a/api-b, mission-a/mission-b endpoint와 Redis location store prefix를 환경 변수로 받는다. |
| Java `SampleTopology.ownerRouteRid` | `Server/Configuration/sample_topology.hpp` | done | player id UTF-8 byte sum modulo 2 방식으로 QuestMission owner RID를 고른다. |
| Java `Server/GameApi/Program.java` | `Server/GameApi/main.cpp` | done | stream session이 client request를 받고 owner QuestMission channel로 요청한다. |
| Java `Server/QuestMission/Program.java` | `Server/QuestMission/main.cpp` | done | mission-a/mission-b가 owner별 client/server channel로 server를 연다. |
| `.NET Client/GameQuestClientScenario.cs` | `Client/gamequest_client_scenario.hpp` | done | Alice/Bob gameplay, duplicate idempotency, offline progress sync, completion notify, server evidence를 검증한다. |
| C++ sample runner convention | `run_sample.sh` | done | `RUN_DIR`, `stdbuf`, Redis location store, flow trace grep, `ZLINK_CPP_BUILD_DIR` build dir를 사용한다. |
