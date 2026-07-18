# S8 NODE bindings 전환 리뷰 manifest — iteration 1

| 항목 | 값 |
|---|---|
| Stage/iteration | S8-NODE bindings / 1 |
| 대상 commit | `db26ce544` |
| Scope | `bindings/node/{src,native/src,samples,binding.gyp,package.json}` minus build/node_modules/prebuilds |
| 파일 수 | 139 |
| aggregate SHA-256 | `b5e4ffac147a6cc50f11416b456d2ebb093d5b8bcc47f0ea1bf6eb232c1b7af8` |
| R1/R2 | Codex / Claude Sonnet |

## 2. Coordinator 실행 증거
- N-API addon node-gyp green(libzlink.so.10 링크·dlopen), `tsc -p tsconfig.json` src green(0), `tsc -p tsconfig.tools.json` samples green(0).
- no-hit: SpotNode 1(binding_socket.ts:151), 나머지 route_bridge/createSpotNode 등 0(부분).

## 3. 알려진 관찰(리뷰어 독립 판정)
- `OperationKind`/`ReceiveKind` TS enum(dispatch.ts)이 addon이 전달하는 raw C enum 값(kind 1-13/operation 1-11)과 불일치 가능(node samples 변환 에이전트 보고).
- `binding_socket.ts:151` `spotNodeActorBindRemoteSession`(제거 심볼) 잔존.
- raw `RouterSocket.sendToSpot/requestToSpot/replyToSpot`가 제거된 `zlink_router_*_spot_part`에 배선(런타임 dead).
- raw `zlink_subscribe_handler` 제거 드리프트(공통 `log/s8-common-raw-layer-drift.ko.md`).
- tests/perf는 scope 밖(별도 트랙).

## 4. Session 기록
| | R1 Codex | R2 Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
