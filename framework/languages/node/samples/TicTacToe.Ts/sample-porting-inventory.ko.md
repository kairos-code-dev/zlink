# TicTacToe.Ts .NET 기준 포팅 Inventory

이 문서는 `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`의
샘플 단위 절차에 따라 현재 Node TicTacToe 샘플을 공통 샘플 문서와 `.NET` 기준 구현에
매핑한다.

## 파일과 역할 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/TicTacToeClientScenario.cs` | `Client/tictactoe-client-scenario.ts` | client-scenario | done | HTTP room 생성, Play stream 선택, host/guest/observer 인증, join, move, milestone push를 검증한다. |
| `.NET: Client/Program.cs` | `Client/main.ts` | client-entry | done | API HTTP endpoint를 읽고 scenario를 실행한다. |
| `.NET: Server/Configuration/SampleSettings.cs` | `Server/Configuration/sample-settings.ts` | configuration | done | timeout과 sample 설정을 둔다. |
| `.NET: Server/Configuration/SampleNames.cs` | `Shared/Contracts/messages.ts`, `Server/Configuration/sample-config.ts` | configuration | done | channel, route, packet 이름과 role endpoint를 공유한다. |
| `.NET: Server/Configuration/RedisRoomRouteStore.cs` | `Server/Configuration/redis-room-route-store.ts` | store-adapter | done | room id에서 owner SpotNode route를 찾는 Redis store다. |
| `.NET: Server/Api/ApiServer.cs` | `Server/Api/tictactoe-api-module.ts` | server-role | done | HTTP API, API channel server, Play channel client를 구성한다. |
| `.NET: Server/Api/Handlers/AuthenticatePlayerHandler.cs` | `Server/Api/Handlers/authenticate-player-handler.ts` | handler | done | Play session 인증 요청을 처리한다. |
| `.NET: Server/Api/Handlers/CreateGameHttpHandler.cs` | `Server/Api/Handlers/create-game-http-handler.ts` | handler | done | HTTP room 생성 요청을 Play channel request로 연결한다. |
| `.NET: Server/Program.cs` | `Server/Api/main.ts`, `Server/Play/main.ts` | server-entry | done | Node는 API와 Play entry point를 분리해 실행한다. |
| `.NET: Server/Play/PlayServer.cs` | `Server/Play/tictactoe-play-module.ts` | server-role | done | Play channel server, stream server, actor runtime, Spot mesh를 구성한다. |
| `.NET: Server/Play/Application/GameCreation/TicTacToeGameCreator.cs` | `Server/Play/Application/GameCreation/tictactoe-game-creator.ts` | application | done | room id 생성, Spot 생성, Redis room route 기록을 조율한다. |
| `.NET: Server/Play/Domain/TicTacToe/*` | `Server/Play/Domain/TicTacToe/*` | domain | done | board, turn, win 판단을 framework 타입 없이 표현한다. |
| `.NET: Server/Play/Infrastructure/ZLink/Actors/*` | `Server/Play/Infrastructure/ZLink/Actors/*` | actor | done | Play actor와 factory를 제공한다. |
| `.NET: Server/Play/Infrastructure/ZLink/Handlers/CreateGameHandler.cs` | `Server/Play/Infrastructure/ZLink/Handlers/create-game-handler.ts` | handler | done | Play channel room 생성 요청을 application use case로 연결한다. |
| `.NET: Server/Play/Infrastructure/ZLink/Sessions/*` | `Server/Play/Infrastructure/ZLink/Sessions/*` | stream-session | done | stream 인증, actor binding, actor relay를 맡는다. |
| `.NET: Server/Play/Infrastructure/ZLink/Spots/EntrySpot/*` | `Server/Play/Infrastructure/ZLink/Spots/EntrySpot/*` | entry-spot | done | room join, milestone observer, actor destroy lifecycle를 맡는다. |
| `.NET: Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/*` | `Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/*` | room-spot | done | board state, turn, leave, timer, move handling을 맡는다. |
| `.NET: Server/Play/Infrastructure/ZLink/TicTacToeGameRoomProvisioner.cs` | `Server/Play/Infrastructure/ZLink/tictactoe-game-room-provisioner.ts` | spot-support | done | room Spot 생성과 Redis route 기록을 adapter로 숨긴다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | JSON 기반 HTTP/channel/stream/actor/Spot payload를 정의한다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | Redis, API A/B, Play A/B, client self-check를 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Unix PowerShell에서는 검증된 Linux runner를 호출해 같은 topology와 self-check marker를 사용한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: Registry/Discovery 없이 수동 endpoint` | `run_sample.sh`, `sample-config.ts` | topology | done | API/Play/Spot route endpoint를 runner가 직접 설정한다. |
| `common: API 2개와 Play 2개` | `run_sample.sh` | runner | done | API A/B와 Play A/B를 별도 process로 실행한다. |
| `common: client는 API 응답의 Play endpoint를 사용` | `Client/tictactoe-client-scenario.ts` | validation | done | client 설정에 Play stream endpoint를 미리 넣지 않는다. |
| `common: Redis room route store` | `redis-room-route-store.ts` | store-adapter | done | room owner SpotNode 위치를 Redis 뒤에 숨긴다. |
| `common: actor public API로 room join` | `play-actor-join-game-handler.ts`, `play-session.ts` | actor-spot | done | actor binding 뒤 Entry Spot handler가 room join을 수행한다. |
| `common: JSON codec` | `tictactoe-play-module.ts`, `tictactoe-api-module.ts` | codec | done | sample payload에 JSON codec을 사용한다. |
| `common: stream connector public wait` | `Client/tictactoe-client-scenario.ts` | validation | done | push notify를 connector wait interface로 검증한다. |
| `common: success marker PASS TicTacToe.Ts` | `run_sample.sh` | validation | done | runner 성공 시 출력한다. |

## 남은 항목

- 없음.
