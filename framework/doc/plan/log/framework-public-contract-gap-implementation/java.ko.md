# Java framework public contract gap 구현 로그

이 문서는 Java 작업의 시간순 실행 기록을 보관한다. 현재 작업 상태와 완료 여부는
[`route-mesh-11.0.0-execution-ledger.ko.md`](../../v11.0/route-mesh-11.0.0-execution-ledger.ko.md)의
진행표에서 확인한다.

| 실행 시각 | gate | 기준 commit | 명령 또는 검토 | exit code | 결과 | 증거 |
|-----------|------|-------------|----------------|-----------|------|------|
| 2026-07-12 22:05 KST | G0 baseline | working tree | `./gradlew --no-daemon test contractTest fakeBackendTest integrationTest sampleTest` | 1 | Redis actor-location-v2 peer row serializer가 정식 fixture의 `Draining:false`를 누락 | `ZLinkRedisLocationRowJsonTest.actorLocationV2FixtureMatchesCurrentCodecOutput` |
| 2026-07-12 22:07 KST | G0 inventory | working tree | Codex public contract, core/bindings 재사용, E2E/sample 3분할 read-only 감사 | 0 | public/runtime 14, coroutine 기능 중복 1, E2E 7개 gap을 ledger에 등록 | `java-g0-contract-ledger.ko.md`와 agent exact file:line finding |
| 2026-07-12 22:10 KST | G0 target contract | working tree | `:zlink-framework-core:contractTest --tests ...JavaTargetContractGapTest` | 1 | 14개 중 12개 목표 gap 재현, sealed location key와 일부 Spot manager 표면 2개는 이미 도달 | contractTest XML/report |
| 2026-07-12 22:12 KST | G0 documentation/E2E inventory | working tree | `:zlink-framework-core:contractTest --tests ...JavaDocumentationRegressionTest` | 1 | canonical spec hash 검증 PASS; ATD 19개와 OBS 13개 active fixture 누락을 fail-closed로 검출 | contractTest XML/report |
| 2026-07-13 19:00 KST | G3 lifecycle regression | working tree | `:zlink-framework-testkit:fakeBackendTest` | 0 | actor request 안의 `leaveActor()`와 LEFT lifecycle 재진입, Entry Spot 복귀 순서 포함 전체 fake backend 통과 | fakeBackendTest XML/report |
| 2026-07-13 19:00 KST | G3 전체 suite | working tree | `test contractTest fakeBackendTest integrationTest sampleTest` | 1 | unit, contract, fake backend 통과; client-first route-mesh integration 1건이 core ROUTER handover 결함으로 실패 | `ChannelMessagingTest.clientServerSpotRouteEgress_requestReplySucceeds` |
| 2026-07-13 19:15 KST | G5 공통 sample | working tree | `ZLINK_SAMPLE_LANGUAGES=java ./samples/run_samples.sh` | 0 | 공통 spec 6종 client self-check와 server evidence 전체 통과 | `All Java/Kotlin samples passed` |
| 2026-07-13 19:17 KST | G7 package evidence | working tree | `./scripts/verify_packaged_contract.sh java` | 0 | 임시 Maven 저장소에서 새 consumer compile/실행 통과 | `java packaged contract verification passed` |
| 2026-07-13 19:18 KST | bindings local package | working tree | `./scripts/local-package/build-wsl.sh java` | 0 | core 9.0.1 native와 multipart request binding을 포함한 Maven package 재배포 | `.artifacts/wsl/maven/systems/zlink/zlink/9.0.1/` |
| 2026-07-14 02:51 KST | G5 공통 sample 재검증 | working tree | `ZLINK_SAMPLE_LANGUAGES=java ./samples/run_samples.sh` | 0 | Redis 격리 runner를 사용하는 공통 spec 6종 전체 통과 | `All Java/Kotlin samples passed` |
