# RouteMesh 10.0.0 S8 DOTNET bindings 전환 리뷰 — iteration 1 공통 prompt

너는 S8 DOTNET(.NET/C#) bindings 전환 리뷰 iteration 1의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `29151802f` (`s8-dotnet(bindings): convert 12 samples to MeshNode/pull-dispatch`)
- Scope: 검토 checkout에서 `git ls-files bindings/dotnet/src bindings/dotnet/samples` 중 `native/`·`/obj/`·`/bin/` 제외
- Scope 파일 수: 206 (src 157, samples 49)
- Scope aggregate SHA-256 (각 파일 sha256sum을 `LC_ALL=C sort` 후 다시 sha256sum): `c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 대상과 목적

.NET(C#) bindings를 zlink Core 10.0.0 공개 C API(`core/include/zlink/service/mesh_node.h·dispatch.h·spot.h·actor.h·stream_session.h`, `core/include/zlink/socket/api.h`)로 전환한 결과를 검토한다. dotnet은 framework parity의 **참조 lane**이므로, 여기서 확정된 pull dispatch·stream_session·direct-method 표면이 다른 언어의 기준이 된다. RouteMesh 10.0.0은 SpotNode/route-bridge/PUB-SUB-plane/push-dispatch 모델을 MeshNode + pull dispatch(ready-index/claim/receive-batch/reply-token) + spot/actor/stream_session 서비스로 대체한 단일 breaking 전환이다. Runtime raw-socket 레이어(raw PUB/SUB·dealer·router·pair·stream)는 존속한다. 폐기되어 10.0.0 등가물이 없는 것: route_bridge, subjects index, internal_sockets introspection, pub bind 분리, 별도 pub/sub routing_id, subscription event stream, spot-level actor 열거, per-message ZMTP metadata(`zlink_msg_gets`).

## 절차 규칙 (ledger §2 최신)

- 시간 제한은 없다. 전체 범위를 검토하고 결과 파일을 기록한 뒤 정상 종료해야 완료다.
- 시작할 때 자신의 review 디렉터리(`codex/` 또는 `claude-sonnet/`)에 `progress.md`를 만들고, 3분보다 긴 간격이 생기지 않도록 현재 검토 축·파일·남은 범위·갱신 시각을 계속 갱신하라.
- 너의 산출물은 review 디렉터리의 `progress.md`와 `review.ko.md` 두 문서뿐이다. **build, 테스트 실행, sanitizer, package 생성 등 어떤 실행 작업도 수행하지 마라.** 실행 증거는 manifest에 기록된 coordinator의 결과(라이브러리 `dotnet build` green, 18 sample 프로젝트 green)만 사용한다. 판정은 소스 정적 대조로만 하되, 필요한 국소 소스 대조는 자유다.
- 이번은 iteration 1이다: 각 축의 `CLEAN`은 해당 축 finding 0건을 뜻한다(blocker/high/medium/low 모두 0). low도 기록하되 CLEAN을 막는다.
- 재지적 규칙: 같은 근본 원인은 하나의 root-cause family로 묶어 보고하라.
- 참고(판정은 독립적으로): coordinator는 samples 전환 중 "join-admission이 ISpot/IMeshNode에 편의 메서드 없이 ready-index drain + MeshReceiveRecord.Reply로만 도달 가능"하다는 관찰을 남겼다. 이것이 I1/I2 finding인지 스스로 판단하라. 단, `tests/Zlink.Tests`는 이번 scope에서 제외한다(별도 test 변환 트랙).

## 검토 축 (3축)

- **I1 계약 구현 일치**: Core 10.0.0 C API와 C# bindings 표면·P/Invoke의 계약 일치. 매핑 정확성(mesh_node/spot/actor/stream_session/dispatch), 인자·수명·오류·동시성, pull dispatch(claim/batch/reply-token, IDisposable 수명) 노출의 정합성, marshalling(struct_size/version, ownership), Core 계약상 필수 설정(예: start 전 routing_id/bind/channel) 노출. 관찰 가능한 동작 불일치.
- **I2 POSD·DDD 리팩터링**: 깊은 모듈·정보 은닉·복잡성 하향 이동, MeshNode·Spot·Actor·session·dispatch 책임 경계. 얕은 wrapper·누출된 추상화·중복.
- **I3 정리 완결성**: 폐기 개념(SpotNode·route_bridge·subjects·internal_sockets·pub/sub rid·dispatch_workers·recv_actor_part·msg_gets)의 잔재가 공개 계약·구현·P/Invoke·samples·주석에 남아 있지 않은가. 죽은 code·선언·정의 없는 dead P/Invoke·alias·forwarder. scoped grep 근거로 판정.

## 출력 계약

자신의 review 디렉터리에 `review.ko.md`를 작성하고, 같은 내용을 최종 결과로 반환하라. 형식:

1. Scope 확인 (시작·종료 파일 수와 aggregate SHA-256)
2. I1 / I2 / I3 각각: Finding(`[축][심각도] file:line — 문제 — 근거 — 수정 제안` 형식, 없으면 "없음"), Evidence, Verdict(CLEAN 또는 NOT CLEAN)
3. 폐기 개념 no-hit 판정 (SpotNode/RouteBridge/spot_node/subjects/internal_sockets/pub-sub rid/dispatch_workers/recv_actor_part/msg_gets 각각 scoped grep 근거)
4. 마지막 줄: 세 축 모두 CLEAN이면 정확히 `BINDINGS REVIEW CLEAN`, 아니면 정확히 `BINDINGS REVIEW NOT CLEAN`

문체 교정·취향 차이는 finding으로 등록하지 마라. finding은 공개 계약, 관찰 가능한 동작, concurrency·resource, build·artifact, 검증 누락, 폐기 잔재에 구체적 영향을 주는 것만 등록한다.
