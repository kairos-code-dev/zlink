# Java/Kotlin runtime·package gate 재검증

실행일: 2026-08-03 20:19~20:25 KST

범위: Java/Kotlin Framework runtime, public API snapshot, packaged clean consumer

## 결과

### Runtime unit·integration

실행 위치는 `framework/languages/java`다.

```text
./gradlew --no-daemon --no-parallel --max-workers=5 --rerun-tasks \
  :zlink-framework-core:test \
  :zlink-framework-kotlin:test \
  :zlink-framework-kotlin:contractTest \
  :zlink-stream-connector:test \
  :zlink-http-client:test \
  :zlink-http-client-kotlin:test \
  :zlink-framework-core:integrationTest \
  :zlink-framework-kotlin:integrationTest
```

결과는 exit `0`, `BUILD SUCCESSFUL`, `41 actionable tasks`다. 이 실행은
stream HWM·handler-less queue·codec·actor admission·application HWM·Kotlin
coroutine/HTTP와 Java/Kotlin integration 범위를 확인한다. common E2E aggregate,
sample process, CI path와 role server evidence는 포함하지 않는다.

### Public API snapshot

`./scripts/verify_api_snapshot.sh java`와
`./scripts/verify_api_snapshot.sh kotlin`을 각각 실행했다. 두 명령 모두 exit `0`으로
source public surface, published package surface와 checked-in snapshot hash를 비교했다.

| 언어 | snapshot lines | sha256 |
|---|---:|---|
| Java | 2,852 | `441c2b8a3735dbabb7988472e99327832f1bfdeede6ed398da2d1add94f314b7` |
| Kotlin | 3,360 | `44c08eb1a6444f7a064031eaa8487879b003ea369e73937f5dd6c1d30fb03ca3` |

이 결과에 맞춰
`framework/doc/contract-inventory/jvm-api-snapshots/java.api.sha256`와
`kotlin.api.sha256`를 갱신했다. snapshot hash 갱신은 source와 package에서 실제로
관찰되는 현재 public surface를 고정하는 작업이며, process behavior 완료를 뜻하지 않는다.

### Packaged clean consumer

`./scripts/verify_packaged_contract.sh java`와
`./scripts/verify_packaged_contract.sh kotlin`을 각각 실행했다. 두 명령 모두 exit `0`으로
temporary repository에 declared artifact를 publish하고 clean consumer를 compile·실행했다.

```text
java packaged contract consumer passed
kotlin packaged contract consumer passed
```

이 gate는 package/API 조건을 닫지만, routed Spot/Actor, HTTP cancellation, stream
process E2E와 common Config 1–14 aggregate의 미완료 조건은 그대로 유지한다.

## 현재 판정

- runtime unit·integration: 현재 revision에서 PASS.
- Java/Kotlin public API snapshot: 현재 hash와 source/package surface가 일치한다.
- Java/Kotlin packaged clean consumer: PASS.
- JK-IMP-001, 002, 004, 005, 006, 007, 009, 010, 011, 012의 process-level
  evidence와 common E2E aggregate: 미완료.
- Java/Kotlin API snapshot/package gate를 제외한 E2E·sample·CI checklist: 기존
  blocker를 유지한다.

## 보존한 로그와 제한

이번 gate의 Gradle 상세 출력은 다음 file log에 보존했다.

- `/tmp/java-kotlin-runtime-gate-attachment-20260803.log`
- `/tmp/java-api-snapshot-attachment-20260803.log`
- `/tmp/kotlin-api-snapshot-candidate-attachment-20260803.log`

이번 실행은 process E2E가 아니므로 message trace나 role stdout/stderr evidence를 새로
생성하지 않았다. process debugging이 필요한 후속 실행은 각 suite의 `logs/` 아래에
`ZLINK_JAVA_STREAM_TRACE=1`과 함께 보존해야 한다.
