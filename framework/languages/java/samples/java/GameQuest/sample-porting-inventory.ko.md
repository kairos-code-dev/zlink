# Java GameQuest Sample Porting Inventory

기준: `framework/languages/dotnet/samples/GameQuest`.

## 상태

| 영역 | Java 포트 | 비고 |
|------|-----------|------|
| Gradle wiring | 완료 | `settings.gradle.kts`, samples aggregate, standalone settings에 등록 |
| Shared contracts | 완료 | dotnet 메시지 이름과 주요 필드 유지 |
| GameApi 역할 | 완료 | stream session이 gameplay request를 받고 QuestMission route channel로 owner routing |
| QuestMission 역할 | 완료 | player별 quest projection, dedupe, progress/completion/reward event를 기록한다. server assertion은 완료 event의 중복 부재와 dedupe 분기 실행을 함께 확인한다. |
| GameplayStateStore | 완료 | API 노드가 Redis에 누적 gameplay fact를 기록하고 QuestMission의 sync가 같은 fact를 읽어 보정한다. 동일 event id는 한 번만 반영한다. |
| Client scenario | 완료 | join과 progress push를 검증하고, quest 완료 전에 같은 idempotency key를 재전송해 진행도가 증가하지 않는지 확인한다. reconnect, delete/rebuild, sync, server assertion도 실행한다. |
| Runner | 완료 | 기존 Java runner 방식의 log 보존, topology 대기, marker 검증 |

## 남은 확인 사항

GameplayStateStore 기반 reset 보정은 닫혔다. owner Spot, event replay, session binding 같은 남은
차이는 `framework/doc/framework/spec/gaps/java.ko.md`에서 계속 추적한다.

## 검증

- `nice -n 15 timeout 600s ./run_sample.sh` 통과: `gamequest full client/server self-check completed`
