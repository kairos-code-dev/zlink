# Java/Kotlin runtime·sample·E2E follow-up

실행 시각: 2026-08-03 18:40 KST

이번 기록은 사용자가 지정한 `runtime gap 구현·unittest → sample → E2E` 순서를
따라 최신 Java/Kotlin 변경을 재검증한 결과다. process E2E aggregate와 CI 완료는
부분 결과와 구분한다.

## Runtime

다음 명령은 최신 direct RouteMesh handler registration API 변경 뒤 다시 실행했다.

```text
cd framework/languages/java
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

결과는 exit `0`, `BUILD SUCCESSFUL`, `41 actionable tasks`, 1분 48초다.
로그는 `/tmp/java-kotlin-runtime-gate-after-route-handler-api-20260803.log`에 있다.

Kotlin suspending direct route handler를 명시적으로 등록할 수 있도록 Java builder의
handler class generic bound를 runtime scanner가 지원하는 class model에 맞춰 정렬했고,
Kotlin Provider의 route handler 등록과 Java/Kotlin exact interface 문서를 함께 맞췄다.
Kotlin RM-C2 process에서 `route readiness peer=api-b`와 실제 reply를 확인해 이 변경을
unit compile만이 아니라 process 수준에서도 확인했다.

## Samples

```text
ZLINK_JAVA_STREAM_TRACE=1 ZLINK_SAMPLE_KEEP_RUN_DIR=1 \
  ./framework/languages/java/samples/run_samples.sh
```

결과는 exit `0`이며 `All Java/Kotlin samples passed`다. Java와 Kotlin의 Bingo,
TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall, GameQuest가 모두 process로
실행됐고, 각 client self-check와 server evidence marker를 확인했다. 전체 로그는
`/tmp/java-kotlin-samples-aggregate-final-20260803.log`에 있다. GameQuest run directory는
`/tmp/tmp.EplFi7uWw5`와 `/tmp/tmp.SVDCpZfQFZ`에 기록됐다.

각 sample과 runtime process에는 `ZLINK_JAVA_STREAM_TRACE=1`을 사용했다. 샘플의
message flow file log는 각 sample의 `logs/flow-*.log`, process stdout/stderr는
`build/sample-logs/`에 남는다.

## E2E 결과와 남은 조건

Canonical aggregate preflight는 여전히 다음과 같이 구조적으로 중단된다.

- Java: `aggregate_incomplete reason=missing_suite suite=ChannelEgressRouting`
- Kotlin: `aggregate_incomplete reason=missing_suite suite=StoreFailure`

현재 실행의 Java/Kotlin default preflight 로그는 각각
`/tmp/java-e2e-aggregate-final-20260803.log`와
`/tmp/kotlin-e2e-aggregate-final-20260803.log`다. 이 결과는 missing suite를 성공으로
계산하지 않는다.

부분 process 결과는 다음과 같다.

- Java와 Kotlin `RegistrationCodec`, `PubSub` aggregate는 통과했다.
- Kotlin `RegistryMessaging/RM-C2`는 route readiness, location readiness, targeted
  reply, missing RID failure를 통과했다. message trace는
  `framework/languages/java/e2e-kotlin/RegistryMessaging/logs/20260803-182242-3359504/`에
  있다.
- Java `RegistryMessaging/RM-C2`는 공통 spec에 맞지 않는 오래된 `TimeoutException`
  assertion을 `REQUEST_TARGET_NOT_FOUND`로 정정했다. 그 뒤 재실행에서는 Core
  `socket_poller.cpp:496 Bad address`로 provider가 종료되어 Java process pass를
  확정하지 않았다.
- Java `RuntimeMonitoring`은 HTTP health readiness를 runner에 추가한 뒤에도
  `HTTP/1.1 header parser received no bytes`가 재현됐다. client는 MON-A1·MON-A2 뒤
  중단했고, 해당 process log는
  `framework/languages/java/e2e/RuntimeMonitoring/logs/20260803-183739-3434553/`에 있다.
- Kotlin `SpotService`는 현재 Kotlin public handler/context API와 맞지 않는 stale E2E
  source 때문에 `:Server:Play:compileKotlin`에서 중단했다. 이는 runtime unit pass가
  아니라 E2E fixture/public-interface alignment blocker다.

## POSD·DDD 적용 범위

이번 단계에서 확인 가능한 리팩토링은 다음 경계에 한정했다.

- actor placement와 instance Spot target 선택은 caller가 retry·연결 상태를 해석하지
  않도록 coordinator/runtime owner가 local target과 admitted peer를 우선 선택한다.
- lifecycle close와 authority generation 변경은 owner layer의 CAS와 close fence에서
  terminal 상태를 한 번만 확정하도록 정리했다.
- stale route fallback은 caller나 sample에 raw frame 변환을 추가하지 않고 channel/spot
  runtime이 payload ownership과 reply part close를 소유하도록 정리했다.
- Java/Kotlin E2E assertion은 구현 결과에 맞춰 임의로 timeout을 기대하지 않고 공통 spec의
  `NotFound` 오류 계약을 사용하도록 수정했다.

이는 runtime owner와 bounded context 경계를 점검한 결과이며, ledger의 R0·R1·R2·E0·E1·S0·S1·F0
전체 review 완료를 의미하지 않는다.

## 다음 재개 지점

1. Core `socket_poller.cpp:496`의 owner-layer 원인을 Core workstream에서 해결하고,
   Java/Kotlin route E2E를 같은 source revision에서 재실행한다.
2. Kotlin SpotService E2E fixture를 현재 public Kotlin handler/context interface에 맞춘다.
3. Java RuntimeMonitoring의 HTTP process EOF 원인을 message trace와 file log로 좁힌다.
4. missing suite를 추가하거나 승인된 ownership/alias를 문서와 aggregate manifest에
   반영한 뒤 canonical aggregate를 재실행한다.
