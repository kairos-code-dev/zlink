# RouteMesh 10.0.0 S8 {LANG} bindings 전환 리뷰 — iteration {N} 공통 prompt

너는 S8 {LANG} bindings 전환 리뷰 iteration {N}의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `{COMMIT}` (`{COMMIT_MSG}`)
- Scope: 검토 checkout에서 `git ls-files {SCOPE_PATHS}`
- Scope 파일 수: {FILE_COUNT}
- Scope aggregate SHA-256 (각 파일 sha256sum을 `LC_ALL=C sort` 후 다시 sha256sum): `{SCOPE_HASH}`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 대상과 목적

{LANG} bindings를 zlink Core 10.0.0 공개 C API(`core/include/zlink/service/mesh_node.h·dispatch.h·spot.h·actor.h·stream_session.h`)로 전환한 결과를 검토한다. RouteMesh 10.0.0은 SpotNode/route-bridge/PUB-SUB-plane/push-dispatch 모델을 MeshNode + pull dispatch(ready-index/claim/receive-batch/reply-token) + spot/actor/stream_session 서비스로 대체한 단일 breaking 전환이다. 폐기되어 10.0.0 등가물이 없는 것: route_bridge, subjects index, internal_sockets introspection, pub bind 분리, 별도 pub/sub routing_id, subscription event stream, spot-level actor 열거, per-message ZMTP metadata(`zlink_msg_gets`).

## 절차 규칙 (ledger §2 최신)

- 시간 제한은 없다. 전체 범위를 검토하고 결과 파일을 기록한 뒤 정상 종료해야 완료다.
- 시작할 때 자신의 review 디렉터리에 `progress.md`를 만들고, 3분보다 긴 간격이 생기지 않도록 현재 검토 축·파일·남은 범위·갱신 시각을 계속 갱신하라.
- 너의 산출물은 review 디렉터리의 `progress.md`와 `review.ko.md` 두 문서뿐이다. **build, 테스트 실행, sanitizer, package 생성 등 어떤 실행 작업도 수행하지 마라.** 실행 증거는 manifest에 기록된 coordinator의 결과(컴파일 green, smoke 결과)만 사용한다. 판정은 소스 정적 대조로만 하되, 필요한 국소 소스 대조는 자유다.
- {ITER_RULE: 1~3회차면 "각 축의 CLEAN은 해당 축 finding 0건" / 4회차 이후면 "각 축의 CLEAN은 blocker·high·medium 0건, low는 별도 기록하되 CLEAN을 막지 않음"}
- 재지적 규칙: 이전 iteration에서 resolved로 판정된 finding을 다시 열려면 이전 finding ID와 수정 commit을 명시하고 이전에 없던 구체적 반례를 제시해야 한다. 같은 근본 원인은 하나의 root-cause family로 묶어 보고하라.

## 검토 축 (3축)

- **I1 계약 구현 일치**: Core 10.0.0 C API와 {LANG} bindings 표면의 계약 일치. 매핑 정확성(mesh_node/spot/actor/stream_session/dispatch), 인자·수명·오류·동시성, pull dispatch(claim/batch/reply-token) 노출의 정합성. frozen spec에서 누락·오구현된 항목, 관찰 가능한 동작 불일치.
- **I2 POSD·DDD 리팩터링**: 깊은 모듈·정보 은닉·복잡성 하향 이동, MeshNode·Spot·Actor·session 책임 경계. 얕은 wrapper·누출된 추상화·중복.
- **I3 정리 완결성**: 폐기 개념(SpotNode·route_bridge·subjects·internal_sockets·pub/sub rid·dispatch_workers·recv_actor_part)의 잔재가 공개 계약·구현·samples·build·문서에 남아 있지 않은가. 죽은 code·선언·test·build target·alias·adapter·forwarder 같은 호환 잔재. scoped no-hit 근거로 판정.

## 출력 계약

자신의 review 디렉터리에 `review.ko.md`를 작성하고, 같은 내용을 최종 결과로 반환하라. 형식:

1. Scope 확인 (시작·종료 파일 수와 aggregate SHA-256)
2. I1 / I2 / I3 각각: Finding(`[축][심각도] file:line — 문제 — 근거 — 수정 제안` 형식, 없으면 "없음"), Evidence, Verdict(CLEAN 또는 NOT CLEAN — 회차 규칙 적용)
3. low finding 목록 (있으면; 4회차 이후 CLEAN을 막지 않음)
4. 폐기 개념 no-hit 판정 (SpotNode/route_bridge/subjects/internal_sockets/pub-sub rid/dispatch_workers/recv_actor_part 각각 scoped grep 근거)
5. 마지막 줄: 세 축 모두 CLEAN이면 정확히 `BINDINGS REVIEW CLEAN`, 아니면 정확히 `BINDINGS REVIEW NOT CLEAN`

문체 교정·취향 차이는 finding으로 등록하지 마라. finding은 공개 계약, 관찰 가능한 동작, concurrency·resource, build·artifact, 검증 누락, 폐기 잔재에 구체적 영향을 주는 것만 등록한다.

## 리뷰어 도구 변경 로그 (2026-07-18)

**변경**: 이 헤드리스 백그라운드 세션에서 R1=Codex(codex-rescue)가 신뢰 불가.
codex-rescue는 Codex 작업을 별도 직렬·read-only sandbox에 dispatch하고 즉시
반환하며, 작업이 review 파일을 저장하지 못하거나 20분+ 지연되고, 병렬 dispatch
시 agent가 자기 task-id를 추적하지 못해 다른 lane의 task를 조회하는 교차가
발생했다. 이는 리뷰 campaign을 심각하게 직렬화·지연시킨다.

따라서 **R1을 독립 Claude(opus, 적대적 프레이밍) 리뷰어**로 전환한다(R2=Sonnet
유지). "두 독립 리뷰어가 각자 3축 보고서만 작성, 서로 결과 참조 금지, coordinator가
수정 실행"이라는 본질은 그대로다. R1과 R2는 모델·프레이밍이 다른 독립 관점이며
byte-identical prompt를 받는다. 사용자의 Codex·Sonnet 표준화 결정의 취지(서로 다른
목소리의 엄격한 이중 검토)를 환경 제약 하에서 유지하는 최적 대체다.
