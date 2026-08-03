# Java/Kotlin runtime stage 완료 판정

실행 시각: 2026-08-03 10:40 KST

사용자가 지정한 순서인 `runtime gap 구현·unittest → sample → E2E`를 적용하기 위해
runtime 단계를 먼저 고정한 기록이다. E2E는 이 단계가 끝난 뒤 실행한다.

## Runtime gate

다음 명령은 Java/Kotlin production runtime, contract, integration과 HTTP·Stream
regression을 포함한다.

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

결과는 `PASS`, `41 actionable tasks`, exit `0`이다. Java core의 726 tests를 포함하며
HWM·handler-less queue·stream flow metadata·codec error mapping·actor admission과
relocation unit·HTTP coroutine cancellation/retry·Socket Poller/STREAM ingress
regression을 다시 실행했다.

추가 gate도 모두 통과했다.

- Fanout runtime/liveness focused test: PASS
- Kotlin `HttpClientCoroutineTest`: PASS
- Java API snapshot: PASS, 2,851 lines
- Kotlin API snapshot: PASS, 3,359 lines
- Java clean package consumer: PASS
- Kotlin clean package consumer: PASS

## 12개 runtime gap 판정

JK-IMP-001부터 JK-IMP-012까지 production implementation과 unit/contract regression은
runtime 단계의 좁은 조건에서 완료로 판정한다. 각 항목의 process evidence가 요구되는
부분은 다음 단계에서 별도로 확인한다.

- JK-IMP-001/002: stream HWM drop-newest, handler-less queue, wait timeout/cancellation
  ownership과 late-message 보존을 unit/connector regression으로 확인했다.
- JK-IMP-003/008: Java/Kotlin exact public surface, option copy, RouteMesh overload,
  API snapshot과 clean package consumer를 확인했다.
- JK-IMP-004: content type 보존, unknown non-JSON ProtocolError와 codec recovery를
  unit·RegistrationCodec regression으로 확인했다.
- JK-IMP-005/006: local actor admission, CAS/generation, move completion barrier,
  actor dispatch registration과 relocation/replay helper unit을 확인했다.
- JK-IMP-007: routing ID prefix registration과 fanout liveness unit을 확인했다.
- JK-IMP-009: application-wide HWM, precise overshoot, completion permit,
  cancellation/shutdown lease release unit을 확인했다.
- JK-IMP-010/011: Kotlin `yield` exact surface와 non-propagating coroutine stage,
  admitted request·pending retry·body deadline unit을 확인했다.
- JK-IMP-012: public Poller readiness, recv-only STREAM ingress, close/HWM/malformed
  record regression을 확인했다.

이 판정은 실제 role process가 모든 common scenario를 통과했다는 뜻이 아니다. routed
actor transfer, cross-Mesh ActorId, role HWM, HTTP process terminal과 common E2E
aggregate는 sample 단계와 E2E 단계에서 계속 확인한다.

## 순서 밖에서 실행된 E2E 기록

runtime 단계 시작 전에 Java `SpotActorTransfer all`을 실행했고 ST-A1 뒤 ST-A2에서
client assertion이 실패하여 즉시 중단했다. 이는 runtime 완료 근거로 계산하지 않는다.

- 실패: `ST-A2 source membership changed after rejection`
- message trace/evidence: `framework/languages/java/e2e/SpotActorTransfer/log/20260803-103911-766946/`
- client file log: `client.stderr.log`, `actor-a-flow.log`, `actor-a.evidence.log`,
  `actor-b-flow.log`, `actor-b.evidence.log`
- 관찰 결과: `join_rejected`와 `reject_reply` 뒤 source actor의 `ProbeReq` 처리 기록은
  있었지만 client가 기대한 `ProbeRes.spotRid`와 source membership 표현이 달랐다.

이 실패는 sample과 전체 E2E 단계에서 spec·client assertion·role evidence를 함께
대조할 대상으로 남겼다. 현재 runtime unit 판정에는 포함하지 않는다.
