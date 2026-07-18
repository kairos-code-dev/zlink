# S8 NODE bindings 리뷰 manifest — iteration 2

| 항목 | 값 |
|---|---|
| iteration | S8-NODE / 2 |
| commit | `7fead5f17` |
| Scope | `bindings/node/{src,native/src,samples,binding.gyp,package.json}` minus build/node_modules/prebuilds |
| 파일 수 | 140 |
| hash | `8a47280f86dff6fcbba089f18c22719fd3ef80c0308f2da0b2993eb98ab1970c` |
| R1/R2 | opus / Sonnet |

## 2. Coordinator 실행 증거
- iter-1 finding(NF1-NF7·NI2·NI3) 전량 수정(finding-ledger).
- addon `npm run build`(node-gyp) green, `tsc -p tsconfig.json`(src) green, `tsc -p tsconfig.tools.json`(samples) green.
- no-hit 9종(SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/msg_gets/routerSpot) 전량 0.

## 3. Session 기록
| | R1 opus | R2 Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
