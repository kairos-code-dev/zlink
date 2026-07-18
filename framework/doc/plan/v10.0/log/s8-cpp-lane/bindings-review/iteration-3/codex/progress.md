# S8 CPP bindings 리뷰 iteration-3 — R1(opus) progress

리뷰어 전용. build/실행/수정 없음(정적 대조 + 로컬 grep/read).

## 0. Scope 확인
- 대상 commit: `f9b6ba50c` (bindings/cpp 비-native 파일은 현재 워킹트리 HEAD `cf7027b26`와 byte-identical — 그 사이 커밋은 dotnet 게이트/freeze 문서 커밋 1개뿐, bindings/cpp diff 없음).
- Scope 파일 수: **121** (기대치 일치).
- 시작 aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be` == 기대치 **일치**.
- 종료 재계산: `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be` — **변동 없음**(파일 미수정 확인).

## 1. iter-2 finding 해소 검증 (C2-0..C2-4) — 전량 RESOLVED
- **C2-0** CMakeLists VERSION: `project(zlink_cpp VERSION 10.0.0 LANGUAGES CXX)` 확인. RESOLVED.
- **C2-1** `zlink_errno.h`: 삭제됨(`git ls-files` 무, 인클루드 참조 0). `pimpl_move.hpp`도 삭제(잔여 참조는 `build/` 산출물뿐, 소스 0). RESOLVED.
- **C2-2** dangling 전방선언·미호출 헬퍼·dead 멤버:
  - `actor_*_operation_t` 전방선언 10개 → 0 hit. RESOLVED.
  - 헬퍼 6개(invalidate_claim / make_spot_request_progress / move_assign_pimpl_and_close / native_request_timeout_ms / set_spot_spot_send_context / second-rid) → 전량 0 hit. RESOLVED.
  - dead `spot_t::impl::last_error` → 제거(현 `last_error`는 정상 오류 패턴·native status 필드). RESOLVED.
- **C2-3** `spot_operation_state_t` dead 필드: `spot_command_t`={request_seq}만, `routing_target_t`={first_rid+native cache}만, second_rid 3인조 제거. 잔존 필드/헬퍼 전량 live(router/reply/request 경로 참조). RESOLVED.
- **C2-4** 소멸자 close-busy 무신호: 4개 소멸자(mesh_node_t / spot_t / mesh_node_publisher_t / stream_session_service_t) 전부 `detail::report_close_on_destroy` 경유 → non-ok시 fail-loud fprintf + debug assert. 계약 불파괴 신호 처리. RESOLVED.

## 2. 전체 scope 3축 재검토
### I1 계약(Core 10.0.0 core/include/zlink/service/*.h + socket/api.h)
- 라이브러리는 `-I core/include` 우선 → 모든 `zlink_*` 호출 시그니처는 Core 헤더로 컴파일 강제(빌드 green). 시그니처 drift 구조적 불가.
- ABI 상수 정합: 각 struct family가 `struct_size=sizeof(..)` + `ZLINK_{SPOT,ACTOR,MESH_NODE,MESH_DISPATCH,STREAM_SESSION}_ABI_VERSION` 사용. 배열 엔트리도 개별 struct_size/version 설정.
- vendored C 헤더 3개(service/{mesh_node,dispatch,stream_session}.h) = core/include와 **byte-identical**(diff 무). 상태 struct 필드 매핑 완전(spot_status 등 전 필드 대응).
- **I1 = CLEAN**(계약 drift 없음).

### I2 POSD/DDD
- iter-2 정리로 응집도 개선. `report_close_on_destroy`가 RAII 소유권 계약(부모>자식 수명)을 한 곳에 문서화·집행. 신규 god-file/책임 누출 없음.
- **I2 = CLEAN**.

### I3 정리 완결성(폐기 no-hit + dead code)
- 폐기 no-hit 독립 검증: SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/msg_gets = **전량 0**. legacy/deprecated/TODO/FIXME/XXX/HACK = 0.
- **그러나 dead code 발견 → I3 NOT CLEAN.** (§review 상세)
  - **F3-1 [medium]** `resolve_timeout`(operation_detail.hpp:26): iter-2 C2-2가 유일 호출자 `native_request_timeout_ms`를 제거하면서 **새로 고아화**. 프롬프트가 명시적으로 경고한 "제거된 헬퍼가 유일 사용자였던 미사용 inline" 정확 사례.
  - **F3-2 [low~medium]** native_send.hpp 미호출 3종(send_single_no_wait_result:19 / submit_single_message_part_restore:41 / submit_single_message_part_no_wait_result:63) + 이들에서만 도달 가능한 to_send_result(native_send_result.hpp:14) cascade.
  - **F3-3 [low]** get_string_option(native_options.hpp:46) + dead `using` 재-export(detail.hpp:40).
  - **F3-4 [low]** submit_message_array(native_message_parts.hpp:297) + dead `using` 재-export(detail.hpp:45).
- 관찰(비-확정): submit_result_from_errno(공개 Contracts/Errors 헤더·미사용), wait_future_with_progress(sample_common.hpp·미사용), vendored 3헤더(shadow+hpp-only install이나 현행·contract test 소비).

## 3. 검증 방법
- iter-2 finding: 심볼별 grep occurrence + diff/ls-files.
- dead code: read-only 서브에이전트 전-트리 fan-out 후보 → R1이 각 후보 `git grep -nw ... -- bindings/cpp`(tests/ 포함)로 재검증. resolve_timeout은 iter-2 base(de299e184) 대비 유일 호출자 추적으로 고아화 인과 확정.

## 4. 판정
- iter-2 C2-0..C2-4 전량 해소. I1/I2 CLEAN. **I3 dead-code 잔존(F3-1..F3-4) → iteration-3 finding 0 미충족.**
- 결과: **BINDINGS REVIEW NOT CLEAN**.
