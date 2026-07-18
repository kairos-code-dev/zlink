# S8 JVM bindings 리뷰 iteration-2 — R1(codex) progress

- reviewer: R1 (독립, adversarial). R2(claude-sonnet) 산출물 미열람.
- 대상 commit: `50faf28fd` (검토 워킹트리 = frozen `e86213d3a`, ec842c6a 상태 동결).
- 절차: 정적 대조만. build/실행 안 함(coordinator manifest 증거 사용).

## Scope 확인
- 시작 hash(재계산): `ec842c6a8947840a2edb35983f69bde59cd43198f097fb26da730d57d52fed61` == 기대값. OK.
- 파일 수: 251. OK.
- 종료 hash: 동일(파일 수정 없음). OK.

## 진행 로그
1. prompt.md / iter-1 finding-ledger / iter-2 manifest 통독.
2. Native.java(1543줄) FFI downcall 전수 + removed-identifiers-10.0.0.json(304) 대조.
3. iter-1 JV-F1..JV-F5 소스 대조 해소 판정.
   - JV-F1 router_recv_part: **재검 실패 — over-correction 결함 발견**(아래).
   - JV-F2/F3/F5: 해소 확인.
   - JV-F4: dead NativeLayouts 미러 제거 확인, 단 `zlink_router_handler` 잔존(비계약).
4. 핵심 raw 디스크립터 8종 arity를 Core `socket/api.h`와 대조.
5. router.recv() 실제 실행경로 추적(DealerRouterRecvSample → NativeRouterSocket.recv → recvDirectOnceIntoImpl → routerRecvPart → MH_ROUTER_RECV_PART.invokeExact).
6. `zlink_router_handler` Core/bridge/export-map 부재 확인.

## 제거심볼 게이트
- JVM 바인딩 소스의 모든 `"zlink_*"` 문자열 리터럴(179) ∩ removed FUNC/TYPE(104) = **∅**. OK.
- 단, 문자열 게이트는 "removed 목록"만 잡는다. `zlink_router_handler`는 removed 목록에 없지만 Core에 **애초에 없는** 심볼 → 게이트 사각지대(secondary finding).

## 판정
- I1: **NOT CLEAN** (JV-R1-1 blocker, JV-R1-2 high).
- I2: CLEAN.
- I3: **NOT CLEAN** (JV-R1-2 dead/broken 심볼).
- 최종: **BINDINGS REVIEW NOT CLEAN**.
