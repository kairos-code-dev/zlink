# Bingo.Ts .NET 기준 포팅 Inventory

이 문서는 `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`의
샘플 단위 절차에 따라 현재 Node Bingo 샘플을 공통 샘플 문서와 `.NET` 기준 구현에
매핑한다. `gap`은 완료 판정이 아니라 다음 수정 대상이다.

## 파일과 역할 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/BingoClientScenario.cs` | `Client/bingo-client-scenario.ts` | client-scenario | done | session stream 하나로 인증, 매칭, 카드 제출, push, observer reward 흐름을 검증한다. |
| `.NET: Client/Program.cs` | `Client/main.ts` | client-entry | done | client 설정을 읽고 scenario를 실행한다. |
| `.NET: Client/Configuration/SampleNames.cs` | `Client/Configuration/sample-names.ts`, `Shared/Contracts/messages.ts` | configuration | done | player id와 packet 이름을 공유한다. |
| `.NET: Server/Configuration/SampleTopology.cs` | `Server/Configuration/sample-config.ts` | configuration | done | runner가 만든 role별 endpoint와 Redis 설정을 읽는다. |
| `.NET: Server/Configuration/SampleFlowLog.cs` | role module의 message-flow trace 설정 | logging | not-needed | Node는 module별 trace log file과 label을 직접 설정한다. |
| `.NET: location store 등록` | `Server/Configuration/location-store.ts` | location-store | done | API/Play/Session role이 같은 Redis location store prefix를 사용한다. |
| `.NET: Server/Api/ApiServerHostFactory.cs` | `Server/Api/bingo-api-module.ts` | server-role | done | API channel server와 Play route mesh client를 구성한다. |
| `.NET: Server/Api/Handlers/AuthenticatePlayerHandler.cs` | `Server/Api/Handlers/authenticate-player-handler.ts` | handler | done | token을 player identity로 바꾼다. |
| `.NET: Server/Api/Handlers/MatchBingoHandler.cs` | `Server/Api/Handlers/match-bingo-handler.ts` | handler | done | API 요청을 Play route mesh의 room allocation request로 연결한다. |
| `.NET: Server/Api/Program.cs` | `Server/Api/main.ts` | server-entry | done | API role entry point다. |
| `.NET: Server/Session/SessionServerHostFactory.cs` | `Server/Session/bingo-session-module.ts` | server-role | done | stream server, API client, route mesh, session Spot node를 구성한다. |
| `.NET: Server/Session/Sessions/BingoSession.cs` | `Server/Session/Sessions/bingo-session.ts` | stream-session | done | 인증 이후 packet을 bound actor로 relay한다. |
| `.NET: Server/Session/Sessions/Handlers/AuthenticateSessionHandler.cs` | `Server/Session/Sessions/Handlers/authenticate-session-handler.ts` | stream-handler | done | session 인증, actor 생성, stream binding을 처리한다. |
| `.NET: Server/Session/Program.cs` | `Server/Session/main.ts` | server-entry | done | Session role entry point다. |
| `.NET: Server/Play/PlayServerHostFactory.cs` | `Server/Play/bingo-play-module.ts` | server-role | done | Play route mesh, actor runtime, Entry Spot, room Spot, Spot pub/sub을 구성한다. |
| `.NET: Server/Play/Application/RoomAllocation/*` | `Server/Play/Application/RoomAllocation/*` | application | done | Redis-backed waiting room allocation을 맡는다. |
| `.NET: Server/Play/Infrastructure/Redis/RedisBingoMatchQueue.cs` | `Server/Play/Infrastructure/ZLink/Matchmaking/redis-bingo-match-queue.ts` | store-adapter | done | Redis match queue adapter다. |
| `.NET: Server/Play/Domain/Bingo/*` | `Server/Play/Domain/Bingo/*` | domain | done | card, room state, draw, win 판단을 framework 타입 없이 표현한다. |
| `.NET: Server/Play/Infrastructure/ZLink/Actors/*` | `Server/Play/Infrastructure/ZLink/Actors/*` | actor | done | player actor와 factory를 제공한다. |
| `.NET: Server/Play/Infrastructure/ZLink/Handlers/AllocateBingoRoomHandler.cs` | `Server/Play/Infrastructure/ZLink/Handlers/allocate-bingo-room-handler.ts` | handler | done | room allocation request를 받고 owner Play node에 room Spot을 만든다. |
| `.NET: Server/Play/Infrastructure/ZLink/Handlers/EnsurePlayerActorHandler.cs` | `Server/Play/Infrastructure/ZLink/Handlers/ensure-player-actor-handler.ts` | handler | done | player actor를 생성하거나 기존 actor를 반환한다. |
| `.NET: Server/Play/Infrastructure/ZLink/Spots/EntrySpot/*` | `Server/Play/Infrastructure/ZLink/Spots/EntrySpot/*` | entry-spot | done | actor admission, match actor request, observer registration을 맡는다. |
| `.NET: Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/*` | `Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/*` | room-spot | done | room join, card submit, draw timer, reward pub/sub, notify publish를 맡는다. |
| `.NET: Shared/Contracts/bingo_messages.proto` | `Shared/Contracts/bingo_messages.proto` | shared-contract | done | Protobuf schema를 공유한다. |
| `.NET: Shared/Contracts/SampleConstants.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | packet names, sample players, reward constants를 둔다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | API A/B, Play A/B, Session A/B, Redis, client self-check를 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Windows PowerShell은 같은 Redis location readiness와 self-check marker를 사용하고, Unix PowerShell에서는 검증된 Linux runner를 호출한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: client는 Session stream endpoint 하나만 안다` | `Client/main.ts`, `Client/bingo-client-scenario.ts` | validation | done | client는 session A/B stream으로만 접속한다. |
| `common: API/Play/Session 2개 이상 실행` | `run_sample.sh`, `run_sample.ps1` | runner | done | API A/B, Play A/B, Session A/B를 실행한다. |
| `common: Redis location store 기반 peer 발견` | `location-store.ts`, `run_sample.sh`, `run_sample.ps1` | location-store | done | runner가 owner lease와 peer row readiness를 같은 Redis store에서 확인한다. |
| `common: Redis-backed match queue` | `redis-bingo-match-queue.ts`, `run_sample.sh` | store-adapter | done | runner가 Redis container와 key prefix를 준비한다. |
| `common: Entry Spot에서 room Spot join` | `match-bingo-actor-handler.ts`, `bingo-entry-spot.ts` | spot-flow | done | Entry Spot actor request handler가 allocated room으로 actor를 join시킨다. |
| `common: Spot pub/sub reward fanout` | `bingo-reward-acquired-event-handler.ts`, `bingo-room-spot.ts` | pubsub | done | reward event를 observer room으로 전달한다. |
| `common: Protobuf codec` | `protobuf-codec.ts`, `bingo_messages.proto` | codec | done | stream, channel, actor, Spot payload에 Protobuf를 사용한다. |
| `common: stream connector public wait` | `Client/bingo-client-scenario.ts` | validation | done | notify 대기를 connector wait interface로 수행한다. |
| `common: success marker PASS Bingo.Ts` | `run_sample.sh`, `run_sample.ps1` | validation | done | runner 성공 시 출력한다. |

## 남은 확인

- 없음.
