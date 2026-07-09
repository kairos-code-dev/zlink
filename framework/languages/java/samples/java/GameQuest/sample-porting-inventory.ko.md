# Java GameQuest Sample Porting Inventory

기준: `framework/languages/dotnet/samples/GameQuest`.

## 상태

| 영역 | Java 포트 | 비고 |
|------|-----------|------|
| Gradle wiring | 완료 | `settings.gradle.kts`, samples aggregate, standalone settings에 등록 |
| Shared contracts | 완료 | dotnet 메시지 이름과 주요 필드 유지 |
| GameApi 역할 | 완료 | stream session이 gameplay request를 받고 QuestMission route channel로 owner routing |
| QuestMission 역할 | 완료 | player별 quest projection, dedupe, progress/completion/reward event 기록 |
| Client scenario | 완료 | dotnet 순서의 join, progress push, duplicate, reconnect, delete/rebuild, sync, server assertion 검증 |
| Runner | 완료 | 기존 Java runner 방식의 log 보존, topology 대기, marker 검증 |

## 남은 확인 사항

현재 Java `GameQuest` 샘플 inventory에는 남은 `gap` 또는 `partial` 항목이 없다. 이후 공통 샘플
문서나 release gate가 바뀌면 이 문서도 같은 기준으로 다시 대조한다.

## 검증

- `nice -n 15 timeout 600s ./run_sample.sh` 통과: `gamequest full client/server self-check completed`
