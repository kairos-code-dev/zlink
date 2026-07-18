# S8 CPP bindings 전환 리뷰 — iteration 3, R1(opus) 판정

독립·적대적 리뷰. 다른 리뷰어·coordinator 해석을 판정 근거로 쓰지 않음. 리뷰어 전용(정적 대조, 파일 미수정).

## 1. Scope 확인
| 항목 | 값 |
|---|---|
| 대상 commit | `f9b6ba50c` (bindings/cpp 비-native = 워킹트리 HEAD `cf7027b26`와 byte-identical; 중간 커밋은 dotnet 게이트/freeze 문서 1건, bindings/cpp diff 0) |
| 파일 수 | 121 (기대치 일치) |
| 시작 hash | `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be` — 기대치 일치 |
| 종료 hash | `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be` — 변동 없음(미수정) |

## 2. iter-2 finding 해소 판정 (C2-0..C2-4) — 전량 RESOLVED

| ID | 내용 | 근거 | 판정 |
|---|---|---|---|
| C2-0 | CMakeLists VERSION 10.0.0 | `CMakeLists.txt:2 project(zlink_cpp VERSION 10.0.0 ...)` | RESOLVED |
| C2-1 | `zlink_errno.h` 삭제 | `git ls-files` 무·소스 참조 0(잔여는 `build/` 산출물). `pimpl_move.hpp`도 삭제, 소스 참조 0 | RESOLVED |
| C2-2 | dangling 전방선언·미호출 헬퍼·dead 멤버 | `actor_*_operation_t`(0), 헬퍼 6종(전량 0), dead `spot_t::impl::last_error` 제거 | RESOLVED |
| C2-3 | `spot_operation_state_t` dead 필드 | `spot_command_t`={request_seq}, `routing_target_t`={first_rid(+cache)}, second_rid 3인조 제거; 잔존 전량 live | RESOLVED |
| C2-4 | 소멸자 close-busy 무신호 | 4개 소멸자 전부 `detail::report_close_on_destroy`(fail-loud fprintf + debug assert, noexcept·계약 불파괴) | RESOLVED |

해소된 iter-1(F1-F11/I2/I3)·iter-2 finding은 새 반례 없어 재개하지 않음.

## 3. 전체 scope 3축 Finding·Evidence·Verdict

### I1 — 계약 일치 (Core 10.0.0 core/include/zlink/service/*.h + socket/api.h)
- **Evidence**: 빌드 include 경로가 `core/include` 우선(CMakeLists:183) → 모든 `zlink_*` 호출은 Core 헤더로 시그니처 강제(빌드 green ⇒ drift 구조적 불가). ABI 상수 정합 확인(spot/actor/mesh_node/dispatch/stream_session 각 `struct_size=sizeof` + `ZLINK_*_ABI_VERSION`, 배열 엔트리 개별 설정). vendored service 헤더 3개(mesh_node.h/dispatch.h/stream_session.h)는 core/include와 `diff` 무(byte-identical). status struct 필드 매핑 완전(spot_status_t 등 전 필드 대응).
- **Finding**: 없음.
- **Verdict**: **CLEAN**.

### I2 — POSD/DDD
- **Evidence**: iter-2 정리로 `spot_operation_state_t` 슬림화·미호출 헬퍼 제거로 응집 개선. `report_close_on_destroy`가 RAII 부모>자식 수명 계약을 단일 지점에 명문화·집행. 신규 god-file/책임 누출/추상화 남용 없음.
- **Finding**: 없음.
- **Verdict**: **CLEAN**.

### I3 — 정리 완결성 (폐기 no-hit + dead code)
- **폐기 no-hit(독립 검증)**: SpotNode / spot_node / route_bridge / subjects / internal_sockets / dispatch_workers / recv_actor_part / msg_gets = **전량 0**. legacy/deprecated/TODO/FIXME/XXX/HACK = 0. → §4.
- **dead code Finding 4건** (모두 `git grep -nw <sym> -- bindings/cpp` 로 tests/ 포함 전-트리 재검증):

#### F3-1 [medium] `resolve_timeout` — iter-2 정리가 새로 만든 고아 inline
- 위치: `src/Runtime/Core/operation_detail.hpp:26`.
- Evidence: iter-2 base `de299e184`에서 `resolve_timeout`의 **유일 호출자**는 같은 파일 line 38의 `native_request_timeout_ms`. iter-2 정리 커밋 `e919e9857`(C2-2)이 `native_request_timeout_ms`를 삭제 → `resolve_timeout` 호출자 0. 현재 `git grep -nw resolve_timeout -- bindings/cpp` = **1(정의뿐)**. `native_timeout_ms`는 22곳에서 계속 사용(정상 유지).
- 의미: 프롬프트가 명시한 "iter-2 수정으로 새로 생긴 잔재 — 제거된 헬퍼가 유일 사용자였던 미사용 inline" 정확 사례.

#### F3-2 [low~medium] native_send.hpp 미호출 3종 + to_send_result cascade
- 위치: `src/Runtime/Native/native_send.hpp:19` `send_single_no_wait_result`, `:41` `submit_single_message_part_restore`, `:63` `submit_single_message_part_no_wait_result`.
- Evidence: 각 심볼 전-트리 grep = **1(정의뿐)**, 호출자 0. 세 함수는 서로도 호출하지 않음.
- cascade: `to_send_result`(`src/Runtime/Native/native_send_result.hpp:14`)의 유일 참조는 `native_send.hpp:84`인데 그 지점은 dead `submit_single_message_part_no_wait_result`(:63) 본문 내부 → 3종 제거 시 to_send_result도 strictly dead.
- 분류: iter-2 C2-2가 [low]로 제거한 "미호출 내부 헬퍼"와 동일 계열(정리 미완).

#### F3-3 [low] `get_string_option` + dead `using` 재-export
- 위치: 정의 `src/Runtime/Native/native_options.hpp:46`, 재-export `src/Runtime/Service/detail.hpp:40` (`using zlink::detail::get_string_option;`).
- Evidence: 전-트리 grep = 2(정의 + using), 실제 호출 0. 래핑 대상 `read_growing_string`은 타 경로에서 사용되어 살아있으나 이 래퍼·재-export만 dead.

#### F3-4 [low] `submit_message_array` + dead `using` 재-export
- 위치: 정의 `src/Runtime/Native/native_message_parts.hpp:297`, 재-export `src/Runtime/Service/detail.hpp:45` (`using zlink::detail::submit_message_array;`).
- Evidence: 전-트리 grep = 2(정의 + using), 실제 호출 0.

- **Verdict**: **NOT CLEAN** (F3-1..F3-4). iteration-3 축 CLEAN=finding 0 미충족.

### 관찰(비-확정 — coordinator 참고, 판정 근거 아님)
- **O1** `submit_result_from_errno`(`include/zlink/Contracts/Errors/errors.hpp:262`): 미호출이나 **공개** 계약 헤더 소재. 형제(config_result_from_errno / handler_result_from_errno)는 내부 사용됨. 공개 표면이라 dead-remove 여부는 count-1 공개 API(from_hex 등)와 같은 정책 판단 영역 — 확정 finding에서 제외, 정리 시 함께 검토 권장.
- **O2** `wait_future_with_progress`(`samples/sample_common.hpp:143`): 미사용 샘플 헬퍼. 경미.
- **O3** vendored C 서비스 헤더 3개(`include/zlink/service/{mesh_node,dispatch,stream_session}.h`): 빌드 시 core/include에 shadow되고 install은 `FILES_MATCHING PATTERN "*.hpp"`로 미설치 → 소스 내 소비 0. **단** (a) core 10.0.0과 byte-identical(계약 drift 없음), (b) out-of-scope contract test `test_cpp_contract_common_header_version`가 `BEFORE PRIVATE include`(CMakeLists:421-423)로 의도적 소비. C2-1(zlink_errno.h)은 **stale v9**였고 이 3개는 **현행**이라 동급 아님 — 확정 finding에서 제외. 장기 drift 위험은 존재하니 core 단일화 정책과의 정합만 coordinator가 확인 권장.

## 4. 폐기 no-hit 판정
독립 grep(`bindings/cpp/{include,src,samples}` minus native/) 결과 8개 폐기 토큰 전량 **0 hit**: SpotNode, spot_node, route_bridge, subjects, internal_sockets, dispatch_workers, recv_actor_part, msg_gets. 삭제 헤더(zlink_errno.h / pimpl_move.hpp) 소스 참조 0. **no-hit ZERO 확인.**

## 5. 결론
iter-2 C2-0..C2-4 전량 해소. I1·I2 CLEAN, no-hit ZERO. 그러나 I3에 dead-code 4건 잔존 — 그중 F3-1 `resolve_timeout`은 iter-2 정리 자체가 유일 호출자를 제거하며 만든 신규 고아로, 이번 이터레이션이 겨냥한 정확한 잔재 유형. iteration-3은 세 축 finding 0을 요구하므로 미충족.

BINDINGS REVIEW NOT CLEAN
