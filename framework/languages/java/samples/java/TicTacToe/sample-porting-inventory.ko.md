# Java TicTacToe sample porting inventory

이 문서는 Java `TicTacToe` 샘플을 `.NET` 구현과 공통 TicTacToe 샘플 문서에 맞춰 대조한
작업 목록이다. `gap`이나 `partial`이 남아 있으면 이 샘플은 완료로 보지 않는다.

## 기준 문서와 구현

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `framework/doc/framework/common/sample/tictactoe/README.ko.md` | 이 inventory와 Java sample source | scenario | done | API 2개, Play 2개, Redis room route, stream session, actor, room Spot, milestone observer 흐름을 검증한다. |
| `.NET: TicTacToe.sln` | `standalone.settings.gradle.kts` | build | done | Shared, Client, Server project를 포함한다. |
| `.NET: README.md` | `README.md` | docs | done | Java 역할 구조와 standalone 실행 방법을 설명한다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | Redis, API A/B, Play A/B, Client를 실행하고 marker를 확인한다. cleanup은 이 runner가 시작한 PID만 정리한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | shell runner와 같은 역할 구성을 제공한다. |

## Client

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/Program.cs` | `Client/src/main/java/.../client/Program.java` | client-entry | done | API URL과 timeout을 읽고 self-check scenario를 실행한다. |
| `.NET: Client/TicTacToeClientScenario.cs` | `Client/src/main/java/.../client/TicTacToeClientScenario.java` | validation | done | 연속 room의 owner round-robin과 고유 ID, 가득 찬 room의 join 거절, 진행 중 leave 무시, 종료 후 client leave 기반 destroy, host/guest/observer stream 연결, 인증, join, move, win, milestone push를 검증한다. host와 guest가 자기 join 알림을 받지 않는지도 typed callback으로 직접 계수한다. |
| common: client는 API 응답의 Play endpoint 사용 | `TicTacToeClientScenario.java` | validation | done | client 설정에 Play endpoint를 미리 넣지 않고 API 응답의 endpoint 목록으로 연결한다. |
| common: push 대기는 connector public wait API 사용 | `TicTacToeClientScenario.java` | validation | done | `PlayerJoinedNotify`, `GameStateNotify`, `WinMilestoneNotify`를 typed wait path로 기다린다. |
| common: inbound observer는 connect 전에 등록 | `TicTacToeClientScenario.java` | validation | done | host·guest·observer connector 생성 직후 observer를 등록한다. marker는 역할, message kind, packet name, request sequence와 payload byte length를 포함하며 payload 검증이나 push 대기를 대신하지 않는다. |

## Shared Contract

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Shared/Contracts/Messages.cs` | `Shared/src/main/java/.../shared/contracts/*.java` | shared-contract | done | HTTP, channel, stream, actor, Spot payload를 Java record/class로 나누어 둔다. |
| common: JSON payload | shared contract + framework default codec | codec | done | stream, channel, actor, room Spot payload는 sample 호출부에서 codec을 직접 다루지 않는다. |

## Server

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Server/Program.cs` | `Server/src/main/java/.../server/api/ApiProgram.java`, `server/play/PlayProgram.java` | server-entry | done | API와 Play를 별도 실행 진입점으로 시작하며 각 진입점은 설정 파일 경로만 받는다. |
| `.NET: Server/Api/*` | `Server/src/main/java/.../server/api/*` | api-role | done | `/games` HTTP endpoint와 player authentication channel handler를 제공한다. |
| `.NET: Server/Configuration/*` | `Server/src/main/java/.../server/configuration/*` | server-config | done | sample endpoint, Redis endpoint, routing id, logging, location store 설정을 모은다. |
| `.NET: Server/Play/PlayServer.cs` | `Server/src/main/java/.../server/play/PlayServer.java` | play-role | done | Play channel server, API channel client, stream server, actor runtime, Spot route/pubsub endpoint를 구성한다. |
| common: Play domain 경계 | `server/play/domain/tictactoe/*` | domain | done | board, turn, win/draw 판정을 framework 타입 없이 표현한다. |
| common: game creation use case | `server/play/application/gamecreation/TicTacToeGameCreator.java` | application | done | room 생성과 Redis room route 기록을 조율한다. |
| common: ZLink adapter 경계 | `server/play/infrastructure/zlink/*` | framework-adapter | done | session, actor, entry Spot, game Spot callback을 application/domain 호출로 변환한다. |
| common: observer milestone handler는 entry Spot 책임 | `PlayEntrySpot`, `PlayerWinMilestoneMsgHandler` | runtime-flow | done | 별도 public notification Spot 타입을 만들지 않고 entry Spot 안에서 milestone push를 처리한다. |

## Runner 검증

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| Redis room route store | `run_sample.sh`, `SampleLocationStore`, `RedisRoomRouteStore` | external-adapter | done | runner가 실행별 전용 Docker Redis를 만들고 그 endpoint만 사용한다. |
| API A/B와 Play A/B scale-out | `run_sample.sh` | validation | done | API 2개, Play 2개를 띄우고 manual channel, Spot route, Spot pub/sub endpoint를 서로 연결한다. |
| observer cross-node milestone | runner grep + client marker | validation | done | observer가 owner가 아닌 Play에 연결하고, API 응답의 endpoint/rid 매핑과 `WinMilestoneNotify`의 실제 수신 node rid가 같은지 확인한다. |
| message flow marker | `TICTACTOE_LOG_DIR` grep | validation | done | runner가 role별 flow log의 `message flow` marker를 확인한다. |
| final marker | `run_sample.sh` | validation | done | client의 `tictactoe completed` 계열 marker와 `PASS TicTacToe.Java`를 확인한다. |

## 남은 확인 사항

현재 Java `TicTacToe` 샘플 inventory에는 남은 `gap` 또는 `partial` 항목이 없다. 이후 공통 샘플 문서나
release gate가 바뀌면 이 문서도 같은 기준으로 다시 대조한다.

## 검증

- `nice -n 15 timeout 600s ./run_sample.sh` 통과: `PASS TicTacToe.Java`
- runner가 host·guest·observer의 `stream-inbound sample=TicTacToe` marker와 RESPONSE·SEND 수신을 확인했다.
