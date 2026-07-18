# S8 NODE bindings 리뷰 iteration-3 — R1(codex) progress

정적 대조 전용(build/실행 없음). 다른 리뷰어·coordinator 해석을 판정 근거로 쓰지 않음.

## Scope 확인
- 대상 commit `bc409293a` (HEAD `4f502b1745`의 조상). `git diff bc409293a HEAD -- bindings/node/{src,native/src,samples,binding.gyp,package.json}` = 무변경 → scope 내용 byte 동일.
- 파일 수: 140 (시작·종료 동일).
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `4c772436d48795958da6e8cdf8e071962f716b9d33c391a1874c382892ccfdf6` — prompt 값 일치.

## 절차 로그
1. Core 권위 헤더 로드: `zlink_errno.h`(result enum), `zlink_enum.h`(source/socket/option enum), `dispatch.h`·`mesh_node.h`(mesh wire enum).
2. iter-2 대상 3파일 재대조: `contracts/errors/results.ts`, `contracts/eventing/monitor.ts`, `runtime/native/binding_socket.ts`.
3. result enum 8종 value-by-value 대조(아래 표).
4. no-hit 13토큰 sweep = 전량 0.
5. dead `stream*Actor` 4선언 sweep = 0.
6. native 함수선언 ↔ addon 등록 완전 대조: 함수형 선언 175 == `ZLINK_METHOD` 등록 175, 양방향 차집합 공집합.
7. mesh wire enum(ReceiveKind/OperationKind/MeshDestinationKind/MeshNodeState) Core 대조 = 전값 일치.
8. 나머지 Core 매핑 enum(SocketType/AutoHwmProfile/RidDuplicatePolicy/SubmitRetryMode/Send·RecvFlags/PollEventFlag/MonitorEventType) 대조.
9. 잔여 drift 산문 sweep: `pub/sub|SpotPub|SpotSub` = monitor.ts:5 doc comment 1건.

## iter-1 (NF1–NF7) / iter-2 해소 판정
| finding | 판정 | 근거 |
|---|---|---|
| NF1 wire enum | 해소 | ReceiveKind 1-13, OperationKind 1-11(ActorJoin=7·ActorLookup=5), MeshDestinationKind 1-5, MeshNodeState 1-7 — Core 전값 일치 |
| NF2 Router spot | 해소 | routerSpot*/sendToSpot on Router = 0 hit; sendToSpot는 Spot 표면만 |
| NF3 kind_data | 해소 | dispatch.ts `ReceiveKindData` union + `kindData` 필드 존재 |
| NF4 ready handler | 해소 | meshNodeSetReadyHandler/meshNodeUnsetReadyHandler 선언·등록 |
| NF5 close 결과 | 해소 | iter-2 검증 유지 |
| NF6 count 타입 | 해소 | iter-2 검증 유지 |
| NF7 transfer | 해소 | meshNodeActorTransfer{Prepare,Commit,Activate,Abort} 선언·등록 |
| iter-2 dead stream*Actor | 해소 | binding_socket.ts 4선언 0 hit |
| iter-2 result enum 최신값 | 해소 | RequestResult.Backpressured=113·Recv 207/208·Connect 608·Config 707/708/709 추가(값 일치) |
| iter-2 MonitorSourceKind 값 | 해소 | `{ Socket: 1 }`만 — Core 유일값과 일치 |

## result enum value-by-value (Core `zlink_errno.h` 대조)
- SubmitResult(14): 0,1,2,3,4,5,6,7,8,9,10,11,12,13 = zlink_submit_result_t 전값 일치
- RequestResult(14): 0,101–113 = zlink_request_result_t 전값 일치(Backpressured=113 포함)
- RecvResult(9): 0,201–208 = zlink_recv_result_t 전값 일치(207/208 포함)
- HandlerResult(7): 0,301–306 = 일치
- CloseResult(5): 0,401–404 = 일치
- BindResult(6): 0,501–505 = 일치
- ConnectResult(9): 0,601–608 = 일치(AuthFailed=608 포함)
- ConfigResult(10): 0,701–709 = 일치(707/708/709 포함)

## 신규 finding
- NF3-1 [I3/I1, low] `MonitorSourceKind` JSDoc가 제거된 "spot's pub/sub side" 개념을 계속 기술 → iter-2 NF2-2 잔재(값은 제거, 산문은 미제거).

판정: `BINDINGS REVIEW NOT CLEAN` (I3 finding 1건).
