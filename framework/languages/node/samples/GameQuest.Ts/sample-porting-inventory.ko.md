# GameQuest TypeScript 포팅 인벤토리

정본은 `framework/languages/dotnet/samples/GameQuest`이다. 이 인벤토리는 P-N2 범위에서 Node 샘플이 맡은 파일과 검증 상태를 기록한다.

| dotnet 책임 | Node 파일 | 상태 |
|-------------|-----------|------|
| shared request/response/notify 계약 | `Shared/Contracts/messages.ts` | 구현 |
| 클라이언트 self-check | `Client/gamequest-client-scenario.ts` | 구현 |
| GameApi stream session | `Server/GameApi/game-api-session.ts` | 구현 |
| GameApi HTTP self-check endpoint | `Server/GameApi/game-api-server.ts` | 구현 |
| gameplay action service | `Server/GameApi/Application/gameplay-action-service.ts` | 구현 |
| owner route dispatcher | `Server/GameApi/Infrastructure/ZLink/gameplay-event-publisher.ts` | 구현 |
| QuestMission route handler | `Server/QuestMission/Infrastructure/ZLink/gameplay-event-route-handler.ts` | 구현 |
| player owner routing | `Server/QuestMission/Application/quest-owner-router.ts` | 구현 |
| quest event processor | `Server/QuestMission/Application/quest-event-processor.ts` | 구현 |
| PlayerQuestSpot hosting | `Server/QuestMission/Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot.ts` | 등록 |
| shared event/projection/gameplay state | `Server/Shared/Store/quest-progress-store.ts` | 구현 |
| runner orchestration | `run_sample.sh`, `run_sample.ps1` | 구현 |

검증 기준은 `npm run build`, `GameQuest.Ts/run_sample.sh`, 그리고 Node sample regression required sample 확장이다. 남은 항목은 없다.
