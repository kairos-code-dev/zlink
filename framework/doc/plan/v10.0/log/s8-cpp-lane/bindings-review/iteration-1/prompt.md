# RouteMesh 10.0.0 S8 CPP bindings 전환 리뷰 — iteration 1 공통 prompt

너는 S8 CPP bindings 전환 리뷰 iteration 1의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `2f34aacf2` (`s8-cpp(bindings): convert 15 samples to pull-dispatch + fix 2 library defects`)
- Scope: 검토 checkout에서 `git ls-files bindings/cpp/include bindings/cpp/src bindings/cpp/samples bindings/cpp/CMakeLists.txt` 중 `native/` 제외
- Scope 파일 수: 129 (include 46, src 64, samples 18, CMakeLists 1)
- Scope aggregate SHA-256 (각 파일 sha256sum을 `LC_ALL=C sort` 후 다시 sha256sum): `e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 대상과 목적

C++ bindings를 zlink Core 10.0.0 공개 C API(`core/include/zlink/service/mesh_node.h·dispatch.h·spot.h·actor.h·stream_session.h`, `core/include/zlink/socket/api.h`)로 전환한 결과를 검토한다. RouteMesh 10.0.0은 SpotNode/route-bridge/PUB-SUB-plane/push-dispatch 모델을 MeshNode + pull dispatch(ready-index/claim/receive-batch/reply-token) + spot/actor/stream_session 서비스로 대체한 단일 breaking 전환이다. Runtime raw-socket 레이어(raw PUB/SUB·dealer·router·pair·stream)는 존속한다. 폐기되어 10.0.0 등가물이 없는 것: route_bridge, subjects index, internal_sockets introspection, pub bind 분리, 별도 pub/sub routing_id, subscription event stream, spot-level actor 열거, per-message ZMTP metadata(`zlink_msg_gets`).

## 절차 규칙 (ledger §2 최신)

- 시간 제한은 없다. 전체 범위를 검토하고 결과 파일을 기록한 뒤 정상 종료해야 완료다.
- 시작할 때 자신의 review 디렉터리에 `progress.md`를 만들고, 3분보다 긴 간격이 생기지 않도록 현재 검토 축·파일·남은 범위·갱신 시각을 계속 갱신하라.
- 너의 산출물은 review 디렉터리(`codex/` 또는 `claude-sonnet/`)의 `progress.md`와 `review.ko.md` 두 문서뿐이다. **build, 테스트 실행, sanitizer, package 생성 등 어떤 실행 작업도 수행하지 마라.** 실행 증거는 manifest §2에 기록된 coordinator의 결과(라이브러리+15 samples compile+link green, rc=0)만 사용한다. 판정은 소스 정적 대조로만 하되, 필요한 국소 소스 대조는 자유다.
- 이번은 iteration 1이다: 각 축의 `CLEAN`은 해당 축 finding 0건을 뜻한다(blocker/high/medium/low 모두 0). low도 기록하되 CLEAN을 막는다.
- 재지적 규칙: 같은 근본 원인은 하나의 root-cause family로 묶어 보고하라.

## 검토 축 (3축)

- **I1 계약 구현 일치**: Core 10.0.0 C API와 C++ bindings 표면의 계약 일치. 매핑 정확성(mesh_node/spot/actor/stream_session/dispatch), 인자·수명·오류·동시성, pull dispatch(claim/batch/reply-token) 노출의 정합성, 자원 수명(RAII batch/claim), 관찰 가능한 동작 불일치. Core 계약상 필수 설정(예: start 전 routing_id/bind/channel)이 표면에 노출되는지.
- **I2 POSD·DDD 리팩터링**: 깊은 모듈·정보 은닉·복잡성 하향 이동, MeshNode·Spot·Actor·session·dispatch 책임 경계. 얕은 wrapper·누출된 추상화·중복.
- **I3 정리 완결성**: 폐기 개념(SpotNode·route_bridge·subjects·internal_sockets·pub/sub rid·dispatch_workers·recv_actor_part·msg_gets)의 잔재가 공개 계약·구현·samples·build·주석에 남아 있지 않은가. 죽은 code·선언·정의 없는 선언·build target·alias·forwarder. `spot_node_t` 같은 구개념 식별자가 내부(src)에 잔존하는지 scoped grep 근거로 판정.

## 출력 계약

자신의 review 디렉터리에 `review.ko.md`를 작성하고, 같은 내용을 최종 결과로 반환하라. 형식:

1. Scope 확인 (시작·종료 파일 수와 aggregate SHA-256)
2. I1 / I2 / I3 각각: Finding(`[축][심각도] file:line — 문제 — 근거 — 수정 제안` 형식, 없으면 "없음"), Evidence, Verdict(CLEAN 또는 NOT CLEAN)
3. 폐기 개념 no-hit 판정 (SpotNode/spot_node/route_bridge/subjects/internal_sockets/pub-sub rid/dispatch_workers/recv_actor_part/msg_gets 각각 scoped grep 근거)
4. 마지막 줄: 세 축 모두 CLEAN이면 정확히 `BINDINGS REVIEW CLEAN`, 아니면 정확히 `BINDINGS REVIEW NOT CLEAN`

문체 교정·취향 차이는 finding으로 등록하지 마라. finding은 공개 계약, 관찰 가능한 동작, concurrency·resource, build·artifact, 검증 누락, 폐기 잔재에 구체적 영향을 주는 것만 등록한다.
