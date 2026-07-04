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

## 남은 외부 제약

현재 샌드박스는 Gradle daemon과 테스트 runner의 local socket 생성을 막는다. 이 때문에 이 환경에서는
`./gradlew build`와 `run_sample.sh`를 끝까지 실행하지 못했고, 대신 현재 framework classpath를 앞에
둔 `javac` 대조 컴파일로 GameQuest Java 소스 전체를 확인했다.
