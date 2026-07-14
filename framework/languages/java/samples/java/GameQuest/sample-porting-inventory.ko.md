# Java GameQuest Sample Porting Inventory

기준: `framework/languages/dotnet/samples/GameQuest`.

## 상태

| 영역 | Java 포트 | 비고 |
|------|-----------|------|
| Gradle wiring | 완료 | `settings.gradle.kts`, samples aggregate, standalone settings에 등록 |
| Shared contracts | 완료 | dotnet 메시지 이름과 주요 필드 유지 |
| GameApi 역할 | 완료 | stream session이 gameplay request를 받고 QuestMission route channel로 owner routing한다. owner 응답의 notification은 대상 player가 현재 session에 bind된 경우에만 push한다. |
| QuestMission 역할 | 부분 구현 | player별 quest projection, dedupe, progress/completion/reward event를 기록한다. projection rebuild는 progress delta와 후속 domain event를 fold한다. owner Spot과 Redis startup replay는 아직 남아 있다. |
| GameplayStateStore | 완료 | API 노드가 Redis에 누적 gameplay fact를 기록하고 QuestMission의 sync가 같은 fact를 읽어 보정한다. 동일 event id는 한 번만 반영한다. |
| Client scenario | 완료 | join과 progress push를 검증하고, quest 완료 전에 같은 idempotency key를 재전송해 진행도가 증가하지 않는지 확인한다. Alice가 api-a 연결을 종료한 뒤 api-b로 다시 연결해 projection과 새 push를 검증하며, delete/rebuild, sync, server assertion도 실행한다. |
| Runner | 완료 | 기존 Java runner 방식의 log 보존, topology 대기, marker 검증 |

## 남은 확인 사항

GameplayStateStore 기반 reset 보정과 event delta fold는 닫혔다. owner Spot, Redis startup replay, session binding 같은 남은
차이는 `framework/doc/framework/spec/gaps/java.ko.md`에서 계속 추적한다.

## 검증

- `nice -n 15 timeout 600s ./run_sample.sh` 통과: `gamequest full client/server self-check completed`
