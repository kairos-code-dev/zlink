# Java/Kotlin runtime·PubSub·HTTP gate 재검증

실행 시각: 2026-08-03 10:36 KST

이번 작업은 Java/Kotlin runtime gap의 unit·integration·API snapshot·clean package
consumer와 PubSub process를 다시 확인한 기록이다. process를 디버깅할 때는 role의
message trace와 stdout/stderr file log를 함께 사용했다.

## 수정 내용

### Fanout readiness와 file log 진단

`ZLinkFanoutLocationRuntime`은 reconcile, bounded tick, subscriber connect 실패를
이전처럼 조용히 버리지 않고 `java.util.logging`으로 기록한다. role launcher의
`ROUTE_SETTLE_TIMEOUT`은 5초에서 20초로 늘렸다. Fanout publisher는 liveness beacon을
5초 간격으로 보내고 15초 동안 만료하지 않으므로, Redis 조회와 process scheduling을
포함한 bounded convergence 시간으로 20초를 사용한다.

PS-B1의 의도적인 slow subscriber queue가 독립된 PS-B2 publisher restart 결과에
영향을 주지 않도록 PS-B1 뒤에 sub-1 process를 종료하고 같은 역할을 다시 시작한다.
이는 client public API를 바꾸지 않으며, 두 scenario의 server evidence를 분리하는
runner cleanup이다.

### Kotlin HTTP cancellation regression

Kotlin coroutine waiter를 취소해도 이미 제출한 `CompletionStage`와 client lease가
취소되지 않는지, 첫 HTTP 시도가 실패한 뒤 pending retry가 계속되어 두 번째 시도가
정상 완료되는지를 `HttpClientCoroutineTest`에 추가했다.

## 검증 결과

| 구분 | 명령 또는 file log | 결과 |
|------|--------------------|------|
| Java/Kotlin runtime·integration | `./gradlew --no-daemon --no-parallel --max-workers=5 --rerun-tasks :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-kotlin:contractTest :zlink-stream-connector:test :zlink-http-client:test :zlink-http-client-kotlin:test :zlink-framework-core:integrationTest :zlink-framework-kotlin:integrationTest` | PASS, `41 actionable tasks`, exit 0 |
| Fanout focused regression | `:zlink-framework-core:test --tests systems.zlink.framework.runtime.channels.ZLinkFanoutLocationRuntimeTest --tests systems.zlink.framework.runtime.internal.service.ZLinkClassicFanoutLivenessTest` | PASS, exit 0 |
| Kotlin HTTP focused regression | `:zlink-http-client-kotlin:test --tests systems.zlink.httpclient.kotlin.HttpClientCoroutineTest` | PASS, exit 0 |
| Java API snapshot | `framework/languages/java/scripts/verify_api_snapshot.sh java` | PASS, SHA-256 `dd5f7c3e071c7bf290d95148921919684ea84d46cd9a73c21db88e3374d6d994`, 2,851 lines |
| Kotlin API snapshot | `framework/languages/java/scripts/verify_api_snapshot.sh kotlin` | PASS, SHA-256 `0bf535f36206e713566375a40555a36d6fe1511dc905add8e89610cb75b4f287`, 3,359 lines |
| Java clean package consumer | `framework/languages/java/scripts/verify_packaged_contract.sh java` | PASS, `java packaged contract consumer passed` |
| Kotlin clean package consumer | `framework/languages/java/scripts/verify_packaged_contract.sh kotlin` | PASS, `kotlin packaged contract consumer passed` |
| Java PubSub PS-B2 focused | `framework/languages/java/e2e/PubSub/run_e2e.sh PS-B2` | PASS, exit 0, `scenario PS-B2 passed`; message trace와 role stdout/stderr는 `framework/languages/java/e2e/PubSub/logs/20260803-102522-702721/`에 있다. |
| Java PubSub aggregate | `framework/languages/java/e2e/PubSub/run_e2e.sh all` | PASS, exit 0, PS-A1~A4·PS-B1~B2·PS-C1; file log는 `framework/languages/java/e2e/PubSub/logs/20260803-102803-716255/`에 있다. |
| Kotlin PubSub aggregate | `framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh all` | PASS, exit 0, PS-A1~A4·PS-B1~B2·PS-C1; file log는 `framework/languages/java/e2e-kotlin/PubSub/logs/20260803-102859-720084/`에 있다. |

## Message trace로 확인한 원인과 결과

이전 Java PS-B2 실패의 trace는 각 subscriber가 baseline 80개를 받은 뒤 publisher
restart 구간에서 topology row를 기다리기 전에 종료되었음을 보였다. Redis의 opaque
fanout index에는 publisher row가 있었지만 `/locations/publishers` snapshot은 비어
있었다. 공통 monitoring contract의 readiness는 socket connection만으로 완료되지
않고 application record 또는 liveness beacon을 요구한다. 따라서 5초 wait는 beacon
간격과 경합할 수 있었다.

Java aggregate의 첫 재실행에서는 PS-B1 slow subscriber의 message trace가
`RECEIVED=153 / DISPATCHED=67`로 남아 PS-B2의 독립 baseline을 오염시켰다. PS-B1 뒤
subscriber를 재시작한 후 aggregate trace는 각 scenario의 message count와 dispatch
count를 독립적으로 확인했고 Java·Kotlin aggregate가 모두 통과했다. 성공 실행의
각 role `*-flow.log`와 stdout/stderr는 위 run directory에서 확인할 수 있다.

## 현재 판정과 남은 조건

- JK-IMP-001, 002, 004, 007, 009, 010, 011, 012의 runtime·unit 및 현재 확인한
  Java/Kotlin PubSub process 범위는 최신 결과로 갱신했다.
- JK-IMP-003, 006, 008은 API snapshot과 clean package consumer를 포함한 public
  surface gate가 최신 결과로 통과했다.
- JK-IMP-005의 local admission과 focused transfer evidence는 통과했지만 routed
  transfer, restart/takeover, stale generation, cleanup failure와 full authority
  process evidence는 남아 있다.
- Stream 독립 process E2E, global ActorId cross-Mesh process, application-wide HWM
  role evidence, Kotlin HTTP cancellation/retry/lease process evidence는 아직
  전체 완료로 승격하지 않는다.
- common E2E 374개 exact inventory, 12개 sample process, CI path/skip gate와
  independent review gate는 이번 실행에 포함되지 않았다.

이 log의 process 판단은 message trace, role stdout/stderr와 client terminal 결과를
함께 기준으로 삼았다. historical log나 source-only 결과를 fresh process evidence로
계산하지 않는다.
