# Java framework public contract gap 구현 로그

이 문서는 Java 작업의 시간순 실행 기록을 보관한다. 현재 작업 상태와 완료 여부는
[`framework-public-contract-gap-implementation.ko.md`](../../framework-public-contract-gap-implementation.ko.md)의
진행표에서 확인한다.

| 실행 시각 | gate | 기준 commit | 명령 또는 검토 | exit code | 결과 | 증거 |
|-----------|------|-------------|----------------|-----------|------|------|
| 2026-07-12 22:05 KST | G0 baseline | working tree | `./gradlew --no-daemon test contractTest fakeBackendTest integrationTest sampleTest` | 1 | Redis actor-location-v2 peer row serializer가 정식 fixture의 `Draining:false`를 누락 | `ZLinkRedisLocationRowJsonTest.actorLocationV2FixtureMatchesCurrentCodecOutput` |
| 2026-07-12 22:07 KST | G0 inventory | working tree | Codex public contract, core/bindings 재사용, E2E/sample 3분할 read-only 감사 | 0 | public/runtime 14, coroutine 기능 중복 1, E2E 7개 gap을 ledger에 등록 | `java-g0-contract-ledger.ko.md`와 agent exact file:line finding |
| 2026-07-12 22:10 KST | G0 target contract | working tree | `:zlink-framework-core:contractTest --tests ...JavaTargetContractGapTest` | 1 | 14개 중 12개 목표 gap 재현, sealed location key와 일부 Spot manager 표면 2개는 이미 도달 | contractTest XML/report |
| 2026-07-12 22:12 KST | G0 documentation/E2E inventory | working tree | `:zlink-framework-core:contractTest --tests ...JavaDocumentationRegressionTest` | 1 | canonical spec hash 검증 PASS; ATD 19개와 OBS 13개 active fixture 누락을 fail-closed로 검출 | contractTest XML/report |
