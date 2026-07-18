# S8 NODE bindings 리뷰 진행 기록 — R2 (Claude Sonnet) iteration 1

## Scope 확인 (시작)
- 대상 commit: `db26ce544` (HEAD `d0f8ba563` 의 ancestor 확인됨)
- `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json | grep -vE '/build/|node_modules|prebuilds' | LC_ALL=C sort | xargs sha256sum | sha256sum`
  = `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` — **일치**
- 파일 수: (아래 확인)

## 절차
- Static read/grep only. No build/test/run.
- R1(Codex) 결과 미참조.

## 작업 로그
- [x] Core C API 헤더 확인 (mesh_node/spot/actor/stream_session/dispatch, enum 정의)
- [x] native/src addon 매핑 대조 (addon_exports.cc 전체 vs core export 전체 cross-ref)
- [x] src/ contracts+runtime 대조 (mesh_node/spot/dispatch 핵심 경로)
- [x] dispatch.ts wire enum 대조 → ReceiveKind/OperationKind 불일치 확인(Critical), MeshNodeState도 동일 root-cause family로 추가 발견
- [x] binding_socket.ts spotNodeActorBindRemoteSession(I3) / RouterSocket.sendToSpot(I1, native 미구현 확인) 확인
- [x] samples 대조 (sample_support.ts가 공개 enum을 의도적으로 우회하는 근거 확보, spot_rpc_example.ts가 RouterSocket 아닌 Spot.requestToSpot을 쓴다는 것도 확인)
- [x] 폐기 개념 no-hit grep (8/9 no-hit, msg_gets 1 hit=주석 잔재)
- [x] I1/I2/I3 판정 및 review.ko.md 작성 완료

## 결론
I1 NOT CLEAN(4 findings) / I2 NOT CLEAN(1 finding) / I3 NOT CLEAN(2 findings)
→ BINDINGS REVIEW NOT CLEAN

## Scope 확인 (종료)
- 종료 시 aggregate SHA-256 재확인: `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` — 일치, `git status --short` scope 경로 무변경 확인
