# Java GameQuest Sample Porting Inventory

기준: `framework/languages/dotnet/samples/GameQuest`.

## 상태

| 영역 | Java 포트 | 비고 |
|------|-----------|------|
| Gradle wiring | 완료 | `settings.gradle.kts`, samples aggregate, standalone settings에 등록 |
| Shared contracts | 완료 | dotnet 메시지 이름과 주요 필드 유지 |
| GameApi 역할 | 완료 | stream session이 gameplay request를 받고 EventId를 응답한 뒤 `GameplayMsg`를 one-way로 owner routing한다. API별 역방향 channel의 처리 결과는 현재 player에 bind된 session에만 push한다. |
| QuestMission 역할 | 완료 | 두 노드가 같은 spot mesh에 참여하고 `PlayerId` routing id의 `PlayerQuestSpot`이 quest 처리를 직렬화한다. player별 quest projection, dedupe, progress/completion/reward event를 기록하며, 최초 활성과 projection rebuild는 Redis stream의 progress delta와 후속 domain event를 fold한다. |
| GameplayStateStore | 완료 | API 노드가 Redis에 누적 gameplay fact를 기록하고 QuestMission의 sync가 같은 fact를 읽어 보정한다. 동일 event id는 한 번만 반영한다. |
| Client scenario | 완료 | join과 progress push를 검증하고, quest 완료 전에 같은 idempotency key를 재전송해 진행도가 증가하지 않는지 확인한다. Alice가 api-a 연결을 종료한 뒤 api-b로 다시 연결해 projection과 새 push를 검증하며, delete/rebuild, sync, server assertion도 실행한다. 별도 rehydrate 단계는 owner process 재기동 뒤 정상 channel 조회로 복원 상태를 확인한다. |
| Runner | 완료 | 기존 Java runner 방식의 log 보존, topology 대기, marker 검증에 더해 mission-a 실제 재기동과 rehydrate client를 실행한다. |

## 남은 확인 사항

GameplayStateStore 기반 reset 보정, event delta fold, Redis replay, 실제 process 재시작 gate, player owner Spot과 one-way owner notify는 닫혔다. framework session actor binding과 scale-out concurrency 같은 남은
차이는 `framework/doc/framework/spec/gaps/java.ko.md`에서 계속 추적한다.

## 검증

- `nice -n 15 timeout 600s ./run_sample.sh` 통과: `gamequest full client/server self-check completed`
- 같은 runner가 mission-a를 종료·재기동한 뒤 `gamequest startup replay restored player-alice`를 확인한다.
- 재기동 뒤 두 번째 client가 정상 channel 조회로 `gamequest-rehydrate=completed`를 확인한다.
- runner는 `surface=SPOT`의 `GameplayMsg` flow를 확인해 channel-only 구현이 다시 들어오지 못하게 한다.
- runner는 `GameplayMsg`가 CHANNEL·SPOT_ROUTE에서 `SEND`이고 `REQUEST`가 0건이며, 결과도 API channel의 `QuestProcessingMsg` SEND인지 확인한다.
