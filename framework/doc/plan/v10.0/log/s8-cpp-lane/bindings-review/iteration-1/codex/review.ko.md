# S8 CPP bindings 전환 리뷰 — iteration 1 — R1 Codex

## 1. Scope 확인

- 대상 commit: `2f34aacf2`
- 검토 방식: 정적 소스 대조만 수행했다. build, 테스트, sanitizer, package 생성은 수행하지 않았다.
- coordinator 증거: manifest §2의 library + 15 samples compile/link green, rc=0을 확인했다.
- 시작: 129 files (include 46, src 64, samples 18, CMakeLists 1), aggregate SHA-256 `e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023`
- 종료: 129 files (include 46, src 64, samples 18, CMakeLists 1), aggregate SHA-256 `e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023`

## 2. I1 계약 구현 일치

### Findings

1. `[I1][blocker] bindings/cpp/src/Runtime/Service/dispatch.cpp:70 — 성공한 recv_batch 뒤 claim을 release하지 않은 채 C++ 객체만 무효화한다 — Core 계약은 claim release가 남은 mailbox work를 rearm하고 reply token은 release 전까지만 유효하다고 정의하지만, 구현은 ZLINK_RECV_OK이면 _valid=false로 바꾸어 destructor와 명시적 release를 모두 no-op으로 만든다. sample helper도 성공 뒤 release를 호출하지만 이미 no-op이다 — 성공한 recv_batch 뒤에도 claim_t를 valid로 유지하고 실제 zlink_mesh_claim_release 성공 시에만 무효화하라. record/reply 처리 기간 동안 claim을 소유하는 scoped turn 객체로 token 수명까지 묶어라.`

2. `[I1][high] bindings/cpp/samples/sample_common.hpp:268 — pull helper가 최대 32개 record를 받은 뒤 첫 record만 보존하고 나머지를 버린다 — receive_batch_t(32, ...)를 사용하면서 batch.at(0)/retain_message(0)만 반환하고 곧 batch를 폐기한다. Core recv_batch는 한 번에 여러 complete message를 채울 수 있으므로 index 1..N-1의 application record와 completion이 관찰되지 않는다 — batch 전체를 순회해 caller queue에 보존하거나 실제 one-record batch를 사용하고 residue/rearm을 끝까지 처리하라.`

3. `[I1][blocker] bindings/cpp/src/Runtime/Native/native_message_parts.hpp:296 — Core service의 borrowed/read-only parts를 raw-socket용 move/consume helper로 전달한다 — submit_message_array는 message_t를 zlink_msg_t로 move하고 실패 때만 복원한다. 반면 Core service send/request/reply/create 계약은 성공과 실패 모두 caller ownership을 유지한다. mesh_node.cpp:219, spot.cpp:111, actor.cpp:83, stream_session.cpp:191, dispatch.cpp:210이 이 helper를 사용하므로 성공 시 C++ message가 invalid가 되고, const 입력을 소비하지 않는 Core 호출 뒤 native reference도 정리되지 않는다 — service 전용 borrowed-parts adapter를 만들어 const span/vector에서 유효한 native view 또는 명시적 copy를 구성하고 모든 결과에서 caller message를 유지하라.`

4. `[I1][high] bindings/cpp/include/zlink/Contracts/Service/dispatch.hpp:143 — receive_record_t가 kind_data와 kind_data_size를 노출하지 않는다 — Core dispatch record의 kind_data는 Actor lifecycle/join completion/transfer control과 SEND_READY destination을 전달하지만 dispatch_access.hpp:74-98은 해당 필드를 복사하지 않는다. 따라서 record kind는 보여도 필요한 control payload를 읽을 방법이 없다 — Core의 versioned kind_data 모델을 C++ typed variant/value object로 매핑하고 actor control, join completion, transfer control, send-ready data를 batch 수명과 독립된 값으로 복사하라.`

5. `[I1][high] bindings/cpp/include/zlink/Contracts/Service/mesh_node.hpp:79 — outbound application metadata와 publish detail 공개 계약이 C++ 표면에서 빠졌다 — Core의 Node/Channel/Spot/Actor/session send·request와 publish는 metadata view를 받고 publish는 target admission/drop detail을 반환한다. C++ signatures에는 대응 인자가 없고 mesh_node.cpp:221-223, 557-559, spot.cpp:113-115, 190-192, actor.cpp:137-139, stream_session.cpp:193-195가 metadata/detail에 항상 nullptr를 전달한다 — 불변 byte view를 모든 대응 API에 추가하고 publish detail value object를 반환하거나 output으로 제공하라.`

6. `[I1][high] bindings/cpp/include/zlink/Contracts/Service/actor.hpp:31 — Actor transfer fence API와 모델이 전부 누락됐다 — Core actor.h:214-226의 prepare/commit/activate/abort 및 transfer token/result 모델에 대응하는 C++ 선언과 Runtime 호출이 없고, public surface에는 record_kind의 transfer_control 이름만 남았다 — transfer value objects와 RAII token을 정의하고 네 상태 전이 API 및 dispatch control record를 동일 계약으로 노출하라.`

7. `[I1][medium] bindings/cpp/include/zlink/Contracts/Service/mesh_node.hpp:106 — Core의 option/query 표면 일부를 바인딩하지 않았다 — core mesh_node.h:210-247의 publisher set/get option, MeshNode set/get option, peer_channels와 spot.h:118의 publish option getter에 대응하는 public method가 없다. 특히 mesh publisher는 NODROP을 변경할 수 없고 MeshNode mailbox budget/HWM과 peer channel weight snapshot을 사용할 수 없다 — typed option methods와 peer channel snapshot을 추가하고 Spot/publisher의 NODROP get/set을 대칭으로 제공하라.`

8. `[I1][high] bindings/cpp/samples/sample_common.hpp:319 — 9개 RouteMesh sample이 필수 routing_id 없이 MeshNode를 start한다 — Core mesh-node 계약은 start 전에 routing ID, bind endpoint, ChannelName을 모두 요구한다. helper는 bind와 channel만 설정한 뒤 start하며, actor/spot sample 9개가 이 helper를 공유한다 — sample별 고유 routing_id를 생성해 set_routing_id()를 start 전에 호출하고 반환값은 status에서 검증하라.`

9. `[I1][high] bindings/cpp/src/Runtime/Service/mesh_node.cpp:485 — ready handler 교체와 trampoline이 callback 동시성 및 예외 경계를 보호하지 않는다 — set_ready_handler는 기존 callback이 실행 중일 수 있는데 같은 std::function을 먼저 대입하고 나서 Core 등록을 변경한다. 등록 실패 시 Core handler와 C++ callable도 불일치하며, trampoline은 사용자 예외를 C callback 경계 밖으로 그대로 전파한다 — stable shared callback state와 동기화를 두고 Core unregister 완료 후 state를 교체하며, trampoline에서 예외를 포착해 정의된 domain/error 정책으로 변환하라.`

10. `[I1][high] bindings/cpp/src/Runtime/Service/mesh_node.cpp:100 — RAII destructor와 move assignment가 close 실패를 무시하고 native ownership을 잃는다 — Core는 child handle이 남으면 MeshNode destroy를 ZLINK_CLOSE_BUSY로 거부하고 포인터를 유지한다. wrapper는 결과를 버린 뒤 impl을 파괴하거나 덮어쓰므로 node가 누수된다. publisher, Spot, stream-session service도 destroy 결과를 버리는 같은 패턴을 사용한다 — 부모·자식 수명을 shared control block으로 묶거나 close 성공 전에는 ownership을 유지하라. 명시적 close는 close_result/예외를 전달하고 move assignment도 기존 resource 종료 실패 시 덮어쓰지 않아야 한다.`

11. `[I1][medium] bindings/cpp/src/Runtime/Service/mesh_node.cpp:425 — remote_actor_ref가 255 bytes를 넘는 Actor ID를 조용히 잘라 다른 ActorRef를 만든다 — Core 계약은 Actor ID가 1..255 bytes인 값이며 초과 입력은 동일 값이 아니다. 구현은 min(size, max)로 truncate한다 — 빈 값과 255-byte 초과를 invalid_argument로 거부하고 UTF-8 byte 길이 계약을 그대로 보존하라.`

### Evidence

- Core dispatch 계약은 reply token 수명을 claim release까지로 정하고 release가 남은 work를 rearm한다고 명시한다(`core/doc/spec/core/service/02-dispatch.md:275-281`, `:323-329`).
- Core service parts는 borrowed/read-only이며 caller가 모든 결과에서 ownership을 유지한다(`core/doc/spec/core/service/01-mesh-node.md:361-363`, `02-dispatch.md:318-319`, `04-actor.md:182-183`, `05-stream-session.md:126`).
- Core start prerequisite는 routing ID + bind endpoint + 최소 한 ChannelName이다(`core/doc/spec/core/service/01-mesh-node.md:164-172`).
- Core 10.0.0 함수 대 Runtime/Service 호출 대조에서 actor transfer 4개, peer_channels, publisher set/get option, Spot get publish option이 호출되지 않는다. MeshNode set/get option도 public C++ method가 없다.

### Verdict

NOT CLEAN

## 3. I2 POSD·DDD 리팩터링

### Findings

1. `[I2][high] bindings/cpp/samples/sample_common.hpp:261 — claim·batch·record·reply-token을 하나의 processing-turn 경계로 캡슐화하지 않아 수명 규칙과 다중-record 처리를 caller helper에 떠넘긴다 — 실제 helper가 token을 claim에서 분리하고 첫 record만 보존했으며, library claim은 반대로 너무 일찍 무효화한다. 이는 I1 finding 1·2와 같은 root-cause family이고 깊은 모듈·복잡성 하향·lifecycle aggregate 경계를 위반한다 — ready drain부터 claim release까지를 소유하는 deep dispatch-turn abstraction을 제공하고, 그 안에서 모든 record 보존과 reply-token 유효성을 강제하라.`

2. `[I2][medium] bindings/cpp/src/Runtime/Service/spot_state.hpp:26 — 하나의 spot_operation_state_t가 raw socket send/request/reply, received_t, 제거된 Spot/SpotNode/Actor/session command를 모두 혼합한다 — 실제 submit switch는 raw_*와 received_*만 사용하지만 enum과 state에는 publish/send_channel/send_to_spot/request_to_actor/bound_session_send 등 죽은 variant와 spot_node_t 포인터가 남아 있다. raw transport와 RouteMesh service bounded context가 한 state pool/reset 지식을 공유해 변경 증폭과 인지 부하를 만든다 — raw socket operation state와 service dispatch state를 책임별로 분리하고 사용되지 않는 variant/field/friend를 제거하라.`

3. `[I2][high] bindings/cpp/src/Runtime/Service/mesh_node.cpp:100 — MeshNode aggregate가 publisher·Spot·stream session child의 lifetime invariant를 소유하지 않는다 — Core가 parent destroy를 child 존재 시 거부하는데 C++ parent는 child를 추적하지 않고 실패도 버린다. 이는 I1 finding 10과 같은 root-cause family이며 lifecycle 책임을 호출 순서에 의존시키는 시간적 분해다 — 공통 control block에서 parent/child ownership과 close 상태를 관리해 잘못된 파괴 순서를 API 정의로 불가능하게 하라.`

### Evidence

- POSD 기준의 핵심 위반은 shallow wrapper, information leakage, special-general mixture, temporal decomposition, caller로 이동한 lifecycle 복잡성이다.
- DDD 기준에서 claim은 owner/domain/generation과 reply-token 수명을 함께 보장해야 하는 lifecycle aggregate인데 현재 wrapper와 sample helper에 규칙이 분산돼 있다.
- `spot_state.hpp:26-55`, `:111-167`, `:289-346`은 raw transport와 제거된 service command가 같은 enum, union-like state, pool reset 정책을 공유함을 보여 준다.

### Verdict

NOT CLEAN

## 4. I3 정리 완결성

### Findings

1. `[I3][high] bindings/cpp/include/zlink.h:8 — Core 10.0.0 전환 뒤 구 C header surface와 내부 SpotNode scaffolding이 대량 잔존한다 — zlink.h는 아직 9.0.4이고 stale actor.h/spot.h/common.h를 include한다. service/spot.h에는 정의가 없는 SpotNode, route_bridge, pub bind/rid, subjects/internal_sockets, spot/actor 열거 선언이 남았고 zlink_enum.h와 option_ids.hpp에는 dispatch_workers가 남았다. C++ public/internal 파일 8개에도 spot_node 식별자가 107회 남아 no-hit gate를 실패한다 — binding-owned 구 C header 복제본을 삭제하거나 Core 10.0.0 header와 단일 소스로 동기화하고, active C++ code의 spot_node forward/friend/state/option/event 식별자와 죽은 operation variant를 제거하라.`

### Evidence

- `bindings/cpp/include/zlink.h:8-10`은 version 9.0.4, `:19-23`은 stale service header include를 기록한다.
- `bindings/cpp/include/zlink/service/spot.h:35-255`에 SpotNode와 분리 router/pub bind·pub/sub routing ID가, `:257-314`에 route_bridge가, `:367-448`에 subjects/internal_sockets 및 spot-level actor 열거가 남아 있다.
- `zlink_spot_node_new`, `zlink_spot_route_bridge_new`, `zlink_spot_node_subjects`, `zlink_spot_node_internal_sockets`는 scope의 stale header 선언 외에 Core/src 또는 bindings/cpp/src 정의가 없다.
- CMake build include 순서는 Core include가 먼저라 coordinator compile/link가 이 stale 복제 header를 가렸고(`bindings/cpp/CMakeLists.txt:181-189`), install은 `*.hpp`만 선택한다(`:225-228`). 따라서 compile/link green은 scope cleanup 완료 증거가 아니다.

### Verdict

NOT CLEAN

## 5. 폐기 개념 no-hit 판정

공통 scope에 `rg -i -n`, `-g '!**/native/**'`를 적용했다.

| 개념 | 판정 | scoped grep 근거 |
|---|---|---|
| SpotNode / `spot_node` | HIT | 8 files, 107 hits. `include/zlink/service/spot.h:35`, `include/zlink_enum.h:48`, `src/Runtime/Service/spot_state.hpp:22` 등 |
| `route_bridge` | HIT | 1 file, 15 hits. `include/zlink/service/spot.h:257-314` |
| `subjects` | HIT | 1 hit. `include/zlink/service/spot.h:431` |
| `internal_sockets` | HIT | 1 hit. `include/zlink/service/spot.h:436` |
| pub/sub rid | HIT | 2 hits. `include/zlink/service/spot.h:228`, `:231` |
| `dispatch_workers` | HIT | 2 files, 4 hits. `include/zlink_enum.h:199-200`, `src/Runtime/Options/option_ids.hpp:97-98` |
| `recv_actor_part` | NO-HIT | 0 files, 0 hits |
| `msg_gets` / `zlink_msg_gets` | NO-HIT | 0 files, 0 hits |

BINDINGS REVIEW NOT CLEAN
