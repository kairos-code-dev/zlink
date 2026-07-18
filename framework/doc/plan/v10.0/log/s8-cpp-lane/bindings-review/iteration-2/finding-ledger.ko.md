# S8 CPP bindings 리뷰 iteration-2 — 병합 finding ledger

두 리뷰어(R1 opus, R2 Sonnet) iteration-2(snapshot `de299e184`, 123파일 `c0cfcd3d`). 둘 다
`BINDINGS REVIEW NOT CLEAN`. **iteration-1 finding F1-F11/I2-1/I3-1은 두 리뷰어 모두 전량 RESOLVED
확인, no-hit 8/8 통과, I1 CLEAN(R1).** 잔여는 전부 dead-code/dangling-decl + 소멸자 신호.

## I3 정리 완결성 (주로 dead code)

### C2-1. `zlink_errno.h` 구 v9 중복 [high]
`bindings/cpp/include/zlink_errno.h`가 구버전 vendored 중복(빌드는 `core/include/zlink_errno.h` 우선
채택으로 shadow, 신규 코드 8종 결여: EALREADY/EDEADLK/ESHUTDOWN, ZLINK_REQUEST_BACKPRESSURED,
ZLINK_RECV_BUFFER_TOO_SMALL/INVALID_STATE, ZLINK_CONNECT_AUTH_FAILED, ZLINK_CONFIG_CONFLICT/
BUFFER_TOO_SMALL/BUSY). iteration-1에서 삭제한 6개 구 헤더와 동일 패턴, 이 파일만 누락. → 삭제
(core/include가 정본).

### C2-2. dangling 전방선언·미호출 헬퍼·dead 멤버 [low]
- `routing_id.hpp:103-112` 정의 없는 `actor_*_operation_t` 전방선언 10개(scope 전체 occurrence=1).
- 미호출 내부 헬퍼 6개: `invalidate_claim`, `make_spot_request_progress`, `move_assign_pimpl_and_close`,
  `native_request_timeout_ms`, `set_spot_spot_send_context`, second-rid 헬퍼 2개.
- dead `spot_t::impl::last_error` 멤버.
→ 전부 제거.

## I2 POSD·DDD

### C2-3. `spot_operation_state_t` dead 필드 (I2-2 부분) [low~medium]
`spot_command_t`의 `spot/topic/channel_name/target`(live는 `request_seq`뿐)와 `routing_target_t`의
`second_rid` 3인조(자기완결 dead island, 접근 헬퍼 2개 미호출). → 미사용 필드/헬퍼 제거, 필요한
raw 경로만 유지.

## I1/I2 (R2)

### C2-4. RAII 소멸자 CLOSE_BUSY 무신호 누수 [medium]
`mesh_node_t`/`spot_t`/`mesh_node_publisher_t`/`stream_session_service_t` 소멸자가 pre-fix와 동일:
Core가 child 존재 시 `ZLINK_CLOSE_BUSY`로 destroy 거부·핸들 유지하는데, 소멸자는 결과를 버리고
핸들을 누수(무신호). iteration-1은 명시 `close()`·move-assign만 close-busy 존중. 샘플은 역순 소멸로
안전하나 API가 강제 안 함. → **부모/자식 수명 결속**(공통 control block으로 parent가 live children
존재 시 안전 처리) 또는, 그 재설계가 과하면 소멸자에서 close-busy를 명시 신호(assert/기록)로 처리해
"무신호 누수"를 제거하되 계약을 깨지 않는 방식. R1은 I1 CLEAN 판정(계약 결함 아님)이나 R2가 medium
지적 — 두 리뷰어 clean을 위해 해소.

## 처리 방침
coordinator 격리 수정. dead 제거는 빌드 green 유지하며 신중히(전방선언·헬퍼·필드가 실제 미참조인지
재확인). C2-4는 최소 침습적이되 리뷰어가 수용할 신호/수명 처리. 라이브러리+15 samples green,
no-hit 유지, CMakeLists VERSION 10.0.0. 완료 후 iteration-3.

### 추가: C2-0. CMakeLists VERSION [medium]
`bindings/cpp/CMakeLists.txt:2` `project(zlink_cpp VERSION 9.0.4 ...)` → `10.0.0`
(`write_basic_package_version_file(... SameMajorVersion)`에 흘러 `find_package(zlink_cpp 10.0.0)` 호환
깨짐). → 10.0.0.
