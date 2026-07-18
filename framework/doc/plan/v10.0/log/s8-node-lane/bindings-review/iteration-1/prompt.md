# RouteMesh 10.0.0 S8 NODE bindings 전환 리뷰 — iteration 1 공통 prompt

너는 S8 NODE(Node.js/TS·N-API) bindings 전환 리뷰 iteration 1의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot
- 대상 commit: `db26ce544`
- Scope: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json` 중 `/build/`·`node_modules`·`prebuilds` 제외
- Scope 파일 수: 139 (src 101, native/src 17, samples 19, binding.gyp 1, package.json 1)
- Scope aggregate SHA-256(`LC_ALL=C sort` 후 재sha256sum): `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8`
- 시작·종료에 파일 수·hash 확인·기록, 검토 중 어떤 파일도 수정 금지.

## 대상과 목적
Node.js bindings(N-API addon `native/src/` + TS `src/`)를 zlink Core 10.0.0 공개 C API(`core/include/zlink/service/*.h`, `core/include/zlink/socket/api.h`)로 전환한 결과 검토. Runtime raw-socket 레이어는 존속하되 일부 raw 심볼도 드리프트했다(주의). 폐기 개념: SpotNode/route_bridge/subjects/internal_sockets/pub·sub rid/dispatch_workers/recv_actor_part/msg_gets.

## 절차 규칙
- 시간 제한 없음. 산출물은 자신의 review 디렉터리(`codex/`|`claude-sonnet/`)의 `progress.md`·`review.ko.md` 두 문서뿐. **build/테스트/실행 금지**(정적 대조만; 국소 grep/read 자유). 실행 증거는 manifest의 coordinator 결과(addon node-gyp green, tsc src+samples green)만 사용.
- iteration 1: 각 축 `CLEAN`=해당 축 finding 0.
- 같은 근본원인은 root-cause family로 묶어라.

## 검토 축(3축)
- **I1 계약 구현 일치**: Core C API와 N-API addon·TS 표면의 계약 일치. 매핑 정확성(mesh_node/spot/actor/stream_session/dispatch), **wire enum 값 일치**(record kind/operation kind), 인자·수명·오류, pull dispatch(claim/batch/reply-token) 정합, reply route(actor join엔 actorJoinReply) 정합. raw 계층 심볼 드리프트(예: `zlink_subscribe_handler` 제거, `zlink_router_*_spot_part` 제거)로 인한 파손 경로.
- **I2 POSD·DDD**: 깊은 모듈·정보 은닉·책임 경계, 얕은 wrapper·누출·중복.
- **I3 정리 완결성**: 폐기 개념 잔재(contracts·runtime·addon·samples·export table), 죽은 코드·정의 없는 export·제거 심볼 참조. scoped grep 근거.

## 출력 계약
`review.ko.md`에 작성·반환:
1. Scope 확인(시작·종료 파일수·hash)
2. I1/I2/I3 각각: Finding(`[축][심각도] file:line — 문제 — 근거 — 수정 제안`, 없으면 "없음"), Evidence, Verdict(CLEAN|NOT CLEAN)
3. 폐기 개념 no-hit 판정(SpotNode/spot_node/route_bridge/subjects/internal_sockets/pub-sub rid/dispatch_workers/recv_actor_part/msg_gets)
4. 마지막 줄: 세 축 CLEAN이면 정확히 `BINDINGS REVIEW CLEAN`, 아니면 `BINDINGS REVIEW NOT CLEAN`

문체·취향은 finding 아님.
