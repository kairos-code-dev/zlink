# S8 CPP bindings 전환 리뷰 — iteration 2 — R2 (Claude Sonnet)

## 1. Scope 확인

- 대상 commit: `de299e184`
- 검토 방식: 정적 소스 대조만 수행. build, 테스트, sanitizer, package 생성은 수행하지 않았다.
  coordinator 증거(manifest: 라이브러리 + 15 samples compile+link green, no-hit ZERO)만 실행
  증거로 사용했다.
- 시작: 123 files, aggregate SHA-256
  `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` (일치)
- 종료: 123 files, aggregate SHA-256
  `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` (일치, 변경 없음)
- HEAD(`680ea049f`)와 review 대상 `de299e184` 사이에 scope 4경로(`bindings/cpp/include`,
  `bindings/cpp/src`, `bindings/cpp/samples`, `bindings/cpp/CMakeLists.txt`)의 변경이
  없음을 `git log de299e184..HEAD -- <scope>`로 확인했고, `git status --porcelain`으로
  scope 경로에 미커밋 수정이 없음도 확인했다. 따라서 현재 checkout을 `de299e184` 스냅샷과
  동일하게 취급해 정적 대조에 사용했다.

## 2. iteration-1 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| F1 (service parts borrowed) | **해소** | `native_message_parts.hpp:308-371`에 `with_borrowed_native_parts`/`submit_borrowed_message_array` 신설(non-owning zlink_msg_t view, NULL free-fn, caller message 항상 보존). `mesh_node.cpp`/`spot.cpp`/`actor.cpp`/`stream_session.cpp`/`dispatch.cpp`의 모든 service send/request/reply/create/publish/transfer 호출부(22곳)가 `submit_borrowed_message_array`로 전환됨을 grep으로 확인. 구 `submit_message_array`(move 기반)는 raw-socket 경로 전용으로만 남음. |
| F2 (claim/reply-token 수명) | **해소** | `dispatch.cpp:70-79` `claim_t::recv_batch`가 더 이상 성공 시 `_valid=false`로 만들지 않음. `release()`(`:39-49`)만 `ZLINK_CLOSE_OK`일 때 무효화, 실패 시 재시도 가능하도록 valid 유지. 코드 주석이 rearm 의미를 명시. |
| F3 (batch 다중 record 유실) | **해소** | `sample_common.hpp:263-283` `mesh_drain_claim`이 `for (size_t i = 0; i < count; ++i)`로 batch 전체를 순회해 owned copy를 보존한 뒤에만 claim을 release. |
| F4 (kind_data 미노출) | **해소** | `dispatch.hpp:185-316`에 `send_ready_data_t`/`join_completion_t`/`actor_control_t`/`transfer_control_t` typed value + `receive_record_t::kind_data` raw bytes 추가. `dispatch_access.hpp:102-159` `decode_kind_data`가 kind별로 batch 수명과 독립된 값으로 복사. |
| F5 (outbound metadata·publish detail) | **해소** | `mesh_node.hpp`의 send/request/publish 전 API가 `mesh_metadata_t metadata_ = {}` 인자를 받고, `publish()`는 `publish_detail_t *detail_out_` 반환. `detail.hpp:75-87` `store_publish_detail`이 native 7필드(struct_size/version 제외 전부)를 손실 없이 복사함을 core 헤더(`mesh_node.h:83-93`)와 대조해 확인. |
| F6 (actor transfer fence API) | **해소** | `actor.hpp:37-126`에 `actor_transfer_role_t`/`phase_t`/`id_t`/`prepare_t`/`prepare_result_t` + RAII `actor_transfer_token_t`(commit/activate/abort) + `actor_transfer_prepare()` 전체 구현. `actor.cpp:231-359`가 Core `zlink_mesh_node_actor_transfer_prepare/commit/activate/abort`를 전부 호출. |
| F7 (옵션/쿼리 표면) | **해소** | `mesh_node.hpp:120-140,212-214` — `peer_channels()`, publisher `set_nodrop()/nodrop()`(spot과 대칭), `set_router_hwm(_profile)()`, mailbox budget getter/setter 전부 노출. `mesh_node.cpp:384-484` 구현 확인. |
| F8 (샘플 routing_id) | **해소** | `sample_common.hpp:349-355` `mesh_start_single_node`가 고유 rid 생성 후 `node_.set_routing_id(...)` 호출. |
| F9 (ready-handler 동시성/예외) | **해소** | `mesh_node.cpp:622-641` `set_ready_handler`가 먼저 Core unregister(`nullptr` 등록) 후 callable을 교체하고 재등록, 실패 시 state 일치 복원. `mesh_node.cpp:65-82` trampoline이 `try/catch(...)`로 사용자 예외를 흡수해 정책대로 처리(swallow + 기존 ready mask 반환). |
| F10 (RAII close 실패 무시) | **부분 해소** — 아래 I1 finding으로 재기재 | move assignment는 `mesh_node.cpp:120-133`, `spot.cpp:82-92`, `mesh_node.cpp:685-693`(publisher), `stream_session.cpp:70-82`에서 전부 `close()` 시도 후 `swap`으로 수정되어 "결과를 버리고 덮어쓰기"는 사라졌다. 그러나 **소멸자 자체**(`mesh_node.cpp:109-113`, `spot.cpp:74-78`, `mesh_node.cpp:674-678`, `stream_session.cpp:59-63`)는 `2f34aacf2`(iteration-1 스냅샷) 대비 **byte-identical**하게 `(void) zlink_*_destroy(...)`로 결과를 계속 버린다. 원 finding이 지목한 두 결함(① 결과 버림, ② impl 파괴/덮어씀) 중 ②(move-assign 경로)만 고쳐졌고 ①(소멸자 경로)은 4개 타입 전부 그대로다. |
| F11 (actor-id 255B 초과) | **해소** | `mesh_node.cpp:565-568` `remote_actor_ref`가 empty 또는 `ZLINK_ACTOR_ID_MAX` 초과 시 `std::invalid_argument`로 거부(더 이상 truncate 안 함). |
| I2-1 (dispatch-turn 추상화) | **해소로 판단(재오픈 안 함)** | 전용 `turn` 클래스는 library에 신설되지 않았지만(`dispatch.hpp`/`dispatch_access.hpp`에 turn 관련 타입 0건), F2/F3의 근본 결함(claim 조기 무효화, batch 부분 유실)이 `claim_t`의 RAII 수명 규칙과 `receive_batch_t`의 전체 인덱스 접근 API로 해소되어 원래 finding이 지적한 관찰 가능한 오동작이 재현되지 않는다. 새 반례 없음 — 재오픈하지 않는다. |
| I2-2 (spot_operation_state_t 혼합) | **해소** | `spot_state.hpp:25-41` `spot_operation_kind_t`가 `raw_*`/`received_*` 8종만 남고 publish/send_channel/send_to_spot/request_to_actor/bound_session_send 등 죽은 service variant, `spot_node_t*` 필드 전부 제거됨을 전체 구조체(`:43-`) 확인. |
| I2-3 (MeshNode child lifetime 미소유) | **부분 해소** — F10과 동일 root-cause, 아래 I2 finding 참조 | |
| I3-1 (구 v9 헤더·dead spot_node) | **해소** | `zlink.h`/`zlink/common.h`/`zlink/service/actor.h`/`zlink/service/common.h`/`zlink/service/spot.h`/`zlink_enum.h` 6개 파일 전부 삭제 확인(`ls` 결과 "removed"). `option_ids.hpp`의 `spot_node_option_id`, `spot_state.hpp`의 죽은 `spot_node_t` fwd-decl·`actor_command_t::node` 전부 제거. §4 no-hit 테이블 전 항목 통과로 재확인. |

## 3. 전체 scope 재검토 (3축)

### I1 — 계약 구현 일치

**Finding 1** `[I1][medium]` `bindings/cpp/src/Runtime/Service/mesh_node.cpp:109-113` (`mesh_node_t::~mesh_node_t`), 동일 패턴 `spot.cpp:74-78`(`spot_t`), `mesh_node.cpp:674-678`(`mesh_node_publisher_t`), `stream_session.cpp:59-63`(`stream_session_service_t`) — 4개 RAII 서비스 타입의 소멸자가 `zlink_*_destroy()`의 반환값을 `(void)`로 전부 버린다. Core 계약상 child(publisher/spot/session) 또는 in-flight callback이 남아 있으면 destroy는 `ZLINK_CLOSE_BUSY`를 반환하고 네이티브 핸들을 유지한다(`mesh_node.hpp:71-74` 자체 문서 주석). 소멸자가 이 결과를 무시하고 그대로 리턴하면, 바로 다음 줄에서 `_impl`(unique_ptr) 자체가 파괴되어 그 핸들 포인터를 담고 있던 마지막 C++ 참조가 사라진다 — 이후 어떤 코드도 그 네이티브 자원을 다시 destroy할 수 없다(영구 누수). 이 코드는 iteration-1 스냅샷(`2f34aacf2`)의 소멸자와 byte-identical하다(git diff로 확인) — F10이 지목한 두 결함 중 move-assignment 쪽(swap 도입)만 고쳐지고 소멸자 쪽은 그대로다.
  - 재현 조건: 정상적인 중첩 선언 순서(부모를 자식보다 먼저 선언 → 역순 파괴로 자식이 먼저 닫힘)에서는 발현되지 않는다. 15개 샘플 전부가 `mesh_node_t`를 spot/publisher/session보다 먼저 선언해 이 경로를 피해간다(`spot_pubsub_example.cpp:22,28-29` 등). 하지만 라이브러리는 이 순서를 API로 강제하지 않으며, 자식을 컨테이너에 담아 독립적으로 관리하거나 declaration을 반대로 하면 즉시 재현된다. 실행 시 어떤 예외·로그·assert도 없이 조용히 자원이 새어나간다.
  - 근거: `mesh_node.cpp:109-113,674-678`, `spot.cpp:74-78`, `stream_session.cpp:59-63`, `git show 2f34aacf2:bindings/cpp/src/Runtime/Service/mesh_node.cpp`(소멸자 동일 코드 확인), `mesh_node.hpp:71-74`(close_result_t::busy 문서화 주석).
  - 수정 제안: (a) 명시적 `close()`를 호출하지 않고 destruct되는 경우에도 최소한 assert/디버그 로그로 신호를 남기거나, (b) 원 finding이 제시한 대로 parent/child를 shared control block으로 묶어 마지막 참조가 사라지기 전까지 실제 native destroy를 지연시켜 이 경로 자체를 봉쇄하라. 최소 조치로는 소멸자가 busy를 만나면 예외를 던지는 대신 (destructor라 예외 불가) `std::abort`/구조적 assert로 계약 위반을 fail-fast 하는 것도 대안이다 — 현재처럼 완전한 침묵은 피해야 한다.

**나머지**: mesh_node.cpp/hpp, spot.cpp/hpp, actor.cpp/hpp, stream_session.cpp/hpp, dispatch.cpp/hpp, dispatch_access.hpp, native_message_parts.hpp, detail.hpp의 나머지 API(peer/channel/actor/spot 메시징 전체, transfer fence, 옵션 접근자, publish detail)를 Core C API(`core/include/zlink/service/*.h`) 시그니처와 1:1 대조했고, 인자 매핑·borrowed ownership·timeout 변환·operation_id 왕복·errno 변환에서 추가 불일치를 찾지 못했다.

**Verdict: NOT CLEAN**

### I2 — POSD·DDD

**Finding 1** `[I2][medium]` `bindings/cpp/src/Runtime/Service/mesh_node.cpp` 전체(및 spot.cpp/stream_session.cpp) — I1 finding 1과 동일 root-cause: `mesh_node_t` aggregate가 여전히 자신이 만들어낸 child(`spot_t`/`mesh_node_publisher_t`/`stream_session_service_t`)의 lifetime을 추적하지 않는다. Core는 child가 열려 있으면 부모 destroy를 거부하는 invariant를 갖지만, 이 invariant는 C++ 표면에서 "올바른 선언 순서를 지키는 관례"에만 의존하고 API로는 위반이 가능하다(잘못된 순서로 선언하거나 child를 heap에 독립 저장하면 즉시 깨진다). 원 I2-3가 요구한 "잘못된 파괴 순서를 API로 불가능화"는 아직 달성되지 않았다.
  - 근거: I1 finding 1과 동일 위치. 어떤 타입도 parent→child 역참조나 shared ownership을 갖지 않음(`grep child|control_block|shared_ptr` 0건 in Service 소스).
  - 수정 제안: I1 finding 1과 동일.

**Verdict: NOT CLEAN**

### I3 — 정리 완결성

**Finding 1** `[I3][high]` `bindings/cpp/include/zlink_errno.h` — Core의 `core/include/zlink_errno.h`와 diff한 결과, iteration-1이 삭제 대상으로 지목했던 6개 구 vendor 헤더와 동일한 패턴의 **stale 복제본**이 하나 더 남아 있었다(원 finding 목록에는 파일명이 명시되지 않았지만, iteration-1 리뷰 본문 말미가 "core의 신규 errno... 반영되지 않음"이라고 이미 언급한 문제의 실체). 8개 신규 errno가 이 복제본에 빠져 있다: `EALREADY`/`EDEADLK`/`ESHUTDOWN`(HAUSNUMERO+20/21/22), `ZLINK_REQUEST_BACKPRESSURED=113`, `ZLINK_RECV_BUFFER_TOO_SMALL=207`, `ZLINK_RECV_INVALID_STATE=208`, `ZLINK_CONNECT_AUTH_FAILED=608`, `ZLINK_CONFIG_CONFLICT=707`/`ZLINK_CONFIG_BUFFER_TOO_SMALL=708`/`ZLINK_CONFIG_BUSY=709`. `bindings/cpp/src`·`bindings/cpp/include`·`bindings/cpp/samples` 어디서도 이 파일을 직접 include하지 않음을 확인했다(전부 `<zlink.h>` → core `common.h` → core `zlink_errno.h` 경로로, CMake include 순서가 core를 우선하므로 실제 컴파일에서는 이 복제본이 소비되지 않는다) — I3-1이 지목한 다른 6개 stale header와 정확히 같은 "컴파일은 가려지지만 scope에 잔존" 상황이다.
  - 근거: `diff bindings/cpp/include/zlink_errno.h core/include/zlink_errno.h`(8개 신규 코드 누락 확인), `grep -rn "zlink_errno.h" bindings/cpp/{include,src,samples}` 0건.
  - 수정 제안: I3-1과 동일 정책 적용 — 삭제(bindings는 core/include를 직접 사용)하거나 core와 재동기화.

**Finding 2** `[I3][medium]` `bindings/cpp/CMakeLists.txt:2` — `project(zlink_cpp VERSION 9.0.4 LANGUAGES CXX)`가 여전히 pre-10.0.0 버전을 선언한다. `core/include/zlink/common.h:8,11`은 `ZLINK_VERSION_MAJOR 10`/`MINOR 0`이다. 이 값은 단순 표기 문제가 아니라 `write_basic_package_version_file(... VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)`(`CMakeLists.txt` 하단)에 그대로 흘러들어가 생성되는 `zlink_cppConfigVersion.cmake`가 major=9로 호환성을 광고한다 — `find_package(zlink_cpp 10.0.0)`로 이 바인딩을 소비하려는 하위 프로젝트가 버전 불일치로 실패할 수 있는 실질적 packaging 결함이다. iteration-1 I3-1이 지목한 동일 계열의 "9.0.4 잔존" 패턴(구 헤더의 `ZLINK_VERSION_MAJOR 9`)이 build 표면에 그대로 남아 있다.
  - 근거: `bindings/cpp/CMakeLists.txt:2`, `core/include/zlink/common.h:7-13`, `git log -- bindings/cpp/CMakeLists.txt`(S8 rewrite 커밋 `8a91e3269`도 이 줄을 건드리지 않음).
  - 수정 제안: `project(zlink_cpp VERSION 10.0.0 ...)`로 core와 동기화(또는 core 버전을 단일 소스로 참조하는 빌드 규칙 도입).

**Verdict: NOT CLEAN**

## 4. 폐기 개념 no-hit 판정 (전체 123-file scope, scoped grep, `native/` 제외)

| 개념 | 결과 | 근거 |
|---|---|---|
| SpotNode / `spot_node` | no-hit | 0 files, 0 hits |
| `route_bridge` | no-hit | 0 hits |
| `subjects` | no-hit | 0 hits |
| `internal_sockets` | no-hit | 0 hits |
| pub/sub 별도 routing_id (`set_pub_routing_id`/`set_sub_routing_id`) | no-hit | 0 hits |
| `dispatch_workers` | no-hit | 0 hits |
| `recv_actor_part` | no-hit | 0 hits |
| `msg_gets` | no-hit | 0 hits |

8개 게이트 전부 통과. (§3 I3 finding 2건은 이 8개 키워드 게이트에 포함되지 않는 별도의 잔존
패턴 — errno 코드 누락과 build version 문자열이라 grep 키워드 매칭 대상이 아니다.)

## 5. 최종 판정

I1 NOT CLEAN(1 finding, medium), I2 NOT CLEAN(1 finding, medium — I1과 동일 root-cause),
I3 NOT CLEAN(2 finding: high 1건, medium 1건).

iteration-1의 11개 F-finding + 3개 I2 + 1개 I3 root-cause 중 F10/I2-3만 부분 해소(소멸자
경로 미수정)로 남았고, 나머지 전부(F1-F9, F11, I2-1, I2-2, I3-1)는 실제로 해소되었음을
소스 대조로 확인했다. no-hit gate 8/8 통과. 다만 fresh 재검토에서 I3-1과 동일 계열의
잔존 2건(zlink_errno.h stale 복제본, CMakeLists.txt version 9.0.4)을 새로 발견했다.

BINDINGS REVIEW NOT CLEAN
