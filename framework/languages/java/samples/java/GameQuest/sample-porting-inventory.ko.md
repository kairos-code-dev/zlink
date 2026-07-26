# Java GameQuest Sample Porting Inventory

계약 기준은 `framework/doc/framework/common/sample/event/gamequest.ko.md`와
`framework/doc/framework/common/sample/README.ko.md`다. `.NET` 구현은 언어별 동작 차이를
찾기 위한 비교 자료로만 사용하며, Java 완료 여부는 공통 계약과 Java runner 결과로 판단한다.

## 상태

| 영역 | Java 포트 | 비고 |
|------|-----------|------|
| Gradle wiring | 완료 | `settings.gradle.kts`, samples aggregate, standalone settings에 등록 |
| Shared contracts | 완료 | 공통 GameQuest 계약의 메시지 이름과 주요 필드를 유지한다. |
| GameApi 역할 | 완료 | stream session이 gameplay request를 받고 EventId를 응답한 뒤 `GameplayMsg`를 one-way로 owner routing한다. API별 역방향 channel의 처리 결과는 현재 player에 bind된 session에만 push한다. |
| QuestMission 역할 | 완료 | 두 instance가 같은 owner channel과 Spot mesh에 참여한다. `PlayerId`로 만든 전역 `SpotRid`가 quest 처리를 직렬화하며, framework와 Location Store가 실제 owner node를 결정한다. player별 quest projection, dedupe, progress/completion/reward event를 기록하며, 최초 활성과 projection rebuild는 Redis stream의 progress delta와 후속 domain event를 fold한다. |
| GameplayStateStore | 완료 | API 노드가 Redis에 누적 gameplay fact를 기록하고 QuestMission의 sync가 같은 fact를 읽어 보정한다. 동일 event id는 한 번만 반영한다. |
| Client scenario | 완료 | join과 progress push를 검증하고, quest 완료 전에 같은 idempotency key를 재전송해 진행도가 증가하지 않는지 확인한다. Alice가 api-a 연결을 종료한 뒤 api-b로 다시 연결해 projection과 새 push를 검증하며, delete/rebuild, sync, server assertion도 실행한다. scale-out 단계는 두 player request/push를 동시에 in-flight로 만들고, 별도 rehydrate 단계는 owner process 재기동 뒤 정상 channel 조회로 복원 상태를 확인한다. |
| Runner | 완료 | 기존 Java runner 방식의 log 보존, topology 대기, marker 검증에 더해 mission-a 실제 재기동과 rehydrate client를 실행한다. |

## 남은 확인 사항

GameplayStateStore 기반 reset 보정, event delta fold, Redis replay, 실제 process 재시작 gate, player owner Spot, one-way owner notify와 scale-out concurrency는 닫혔다. framework session actor binding 같은 남은
차이는 `framework/doc/framework/common/spec/90-implementation-gap.ko.md`에서 계속 추적한다.

## 검증

- `nice -n 15 timeout 600s ./run_sample.sh` 통과: `gamequest full client/server self-check completed`
- 같은 runner가 mission-a를 종료·재기동한 뒤 `gamequest startup replay restored player-alice`를 확인한다.
- 재기동 뒤 두 번째 client가 정상 channel 조회로 `gamequest-rehydrate=completed`를 확인한다.
- runner는 `surface=SPOT`의 `GameplayMsg` flow를 확인해 channel-only 구현이 다시 들어오지 못하게 한다.
- runner는 `GameplayMsg`가 CHANNEL·SPOT_ROUTE에서 `SEND`이고 `REQUEST`가 0건이며, 결과도 API channel의 `QuestProcessingMsg` SEND인지 확인한다.
- runner는 두 scale-out player가 각각 mission-a와 mission-b owner Spot에서 처리되고 `gamequest-scale-out=completed`가 출력되는지 확인한다.
