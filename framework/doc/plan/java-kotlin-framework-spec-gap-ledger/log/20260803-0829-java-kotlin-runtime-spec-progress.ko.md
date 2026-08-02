# Java/Kotlin Framework runtime·spec 진행 기록

실행 시각: 2026-08-03 08:29 KST
브랜치: `agent/framework-contract-runtime-update`
기준 revision: `b718b4d5cd7ed28f3e399339220b83431bdae902`

## 이번 범위

이번 진행에서는 Java/Kotlin Framework의 runtime 수정, public contract 문서, source-owner
inventory, JVM API snapshot과 clean package consumer를 다시 확인했다. 다른 언어 workstream의
변경은 조사 대상에 포함했지만 수정하지 않았다.

## 통과한 검증

- Java core contract/runtime focused test가 통과했다. 실행 범위는
  `ZLinkJavaRawSpotNodeM6BTest`, `ZLinkInboundDispatchBudgetTest`,
  `JavaTargetContractGapTest`, `JvmPublicContractSourceOwnerTest`,
  `StreamNetworkDefaultsTest`, `ZLinkStreamRuntimeIngressTest`이다.
- Kotlin contract/runtime focused test가 통과했다. `KotlinPublicSurfaceContractTest`와
  `KotlinConnectorWrapperTest`를 실행했다.
- Java stream connector의 `ConnectorDispatchTest`와 `ZLinkStreamTestHelperTest`가
  통과했다. TCP queue에서 새 message를 drop하고 기존 queue item과
  `RECEIVED_MESSAGE_DROPPED` evidence를 보존하는 경계를 확인했다.
- Java와 Kotlin의 local Spot route 및 local Actor dispatch에서 wire `contentType`을
  보존하는 regression이 통과했다. local Actor path는 수신 record를 만들 때 decoded
  content type을 전달하도록 수정했다.
- Java/Kotlin API snapshot candidate와 verify가 모두 통과했다.
  - Java: `dd5f7c3e071c7bf290d95148921919684ea84d46cd9a73c21db88e3374d6d994`, 2,851 lines
  - Kotlin: `0bf535f36206e713566375a40555a36d6fe1511dc905add8e89610cb75b4f287`, 3,359 lines
  source jar와 published package jar의 public JVM surface도 같은 결과였다.
- `./scripts/verify_packaged_contract.sh java`가 통과했다. 9개 Java artifact를 temporary
  Maven repository에 publish하고 clean Java consumer를 실행했다.
- `./scripts/verify_packaged_contract.sh kotlin`가 통과했다. 11개 Kotlin/공통 artifact를
  temporary Maven repository에 publish하고 clean Kotlin consumer를 실행했다.

위 결과는 source, focused test, package, API snapshot gate의 증거다. common E2E aggregate,
sample process, CI job 실행 결과를 대신하지 않는다.

## 실행했으나 닫히지 않은 검증

`framework/languages/java/e2e/SubmitAdmission/run_e2e.sh all`을 current candidate package로
실행하려 했지만 scenario 시작 전에 native Core hash preflight에서 중단됐다.

- candidate package가 참조한 `zlink` 10.6.3 SHA: `8a83a348...`
- 현재 `core/build/lib/libzlink.so` SHA: `fc12e01c...`
- package에 포함된 native SHA: `34561049...`
- 현재 Core version은 11.1.0이며, 따라서 이 결과는 SubmitAdmission scenario 실패가 아니라
  stale candidate/package 불일치 blocker다.

Kotlin에는 `framework/languages/java/e2e-kotlin/SubmitAdmission` runner가 현재 없다. 따라서
Java/Kotlin SubmitAdmission process evidence를 완료로 표시하지 않았다. historical log도
현재 candidate의 process 증거로 사용하지 않았다.

## 현재 판정

- JK-IMP-003, JK-IMP-008, JK-IMP-010, JK-IMP-012의 source/package/API snapshot 범위는
  추가 증거를 확보했다. 다만 각 항목의 전체 process E2E와 aggregate/CI 조건은 별도다.
- JK-IMP-004는 local Spot/Actor content type path를 추가로 확인했다. route/spot 전체
  process 조합과 모든 E2E role evidence는 남아 있다.
- JK-IMP-001, JK-IMP-002, JK-IMP-009, JK-IMP-011은 focused runtime 경계가 통과했지만
  process cancellation, shutdown, precise HWM, HTTP retry/body deadline 증거가 남아 있다.
- JK-IMP-005, JK-IMP-006, JK-IMP-007은 actor authority, global ActorId, fanout prefix에
  대한 focused 또는 선택 process 증거가 있으나 restart/takeover, 서로 다른 Mesh,
  SubmitAdmission과 aggregate process 증거가 남아 있다.
- Java/Kotlin common 374 scenario aggregate, six sample process, dedicated CI path와
  POSD/DDD 최종 review는 완료하지 않았다.

따라서 이번 실행으로 checklist의 API snapshot·package consumer 항목만 완료로 올리고,
전체 ledger 상태는 `진행 중`으로 유지한다.
