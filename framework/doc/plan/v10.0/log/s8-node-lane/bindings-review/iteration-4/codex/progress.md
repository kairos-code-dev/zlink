# S8 NODE bindings review — iteration 4 — R1 (opus/codex lane) progress

## Scope 확인
- commit `006d34f97` (freeze `7a68f7973`)
- 파일 수: 140 (일치)
- aggregate SHA-256: `967205f1af41bf948f105b576167f21536d3a59094cb06ed675c9f4674fa3963` (일치)
- 시작=종료 hash 동일. 파일 수정 없음(정적 대조만).

## iter-3 finding 해소 판정
- R2(NF3-1) option_mapping.ts: phantom `0x3035/0x3036` 제거·`AUTO_HWM_MSG_UNIT_BYTES=0x3034` 추가 확인. **해소.**
- R1 monitor.ts: `MonitorSourceKind` JSDoc = "Core 10.0.0 defines only the socket source", `Socket:1`만. spot pub/sub 서술 없음. **해소.**

## I1 계약 일치
- option id 테이블(SocketOption): Core `zlink_option_t`(0x3001~0x3039) + router/dealer/pub/sub/stream option enum 전 범위 정확 일치.
- SocketType 0x1001~0x1008 + ANY=0: 정확 일치.
- MonitorEventType 1<<0~1<<15: 정확 일치.
- PollEventFlag(1/2/4/8/32): Core `ZLINK_POLL*` 일치(POLLITEMS_DFLT=16는 이벤트 플래그 아님, 정당히 제외).
- RidDuplicatePolicy(Reject0/Handover1)·SubmitRetryMode(Off0/LocalFailure1): 정확 일치.
- native decl==registration: C++ `ZLINK_METHOD` 등록 175 ↔ TS `*NativeBinding` 선언 175 완전 bijection, 양방향 차집합 0.
- pull dispatch: mesh_node completion이 pull-dispatch 파이프라인으로 전달됨을 계약·runtime에서 일관 서술.

## I2 / I3
- 하위 조사(Explore) + 직접 대조 병행.

## Coordinator 증거(재실행 안 함)
- manifest: addon node-gyp green, tsc src+samples green, no-hit 0.
