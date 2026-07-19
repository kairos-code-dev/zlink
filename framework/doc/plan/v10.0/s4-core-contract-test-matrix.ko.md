# S4 Core 계약-테스트 matrix

## 0. 문서 상태와 사용 방법

이 문서는 S4의 정식 spec 절과 source·header·test 대응을 고정하는 검증 matrix다. 공개 계약은
`core/doc/spec/` 정식 문서가 소유하며 이 문서는 계약을 다시 정의하지 않는다. 현재 진행 상태와 완료
증거는 [`RouteMesh 10.0.0 실행 진행표`](./route-mesh-10.0.0-execution-ledger.ko.md)의 S4 행만 소유한다.
대상 독자는 S4 Core 구현 담당자와 S5 reviewer다. 이 문서는 "각 정식 spec 절을 어떤 검증으로
확인하는가?"에 답하며 별도 진행표로 사용하지 않는다.

기준 spec revision은 S1 승인 기준선이다. 최초 전달된 기준선(`6cd163bf…`) 이후 S1 리뷰 수정이 반영되어
사용자 승인 아래 2026-07-17 04:36 checkout으로 기준선을 재동결했다. spec이 다시 갱신되면 같은 절차로
기준선을 재동결하고 이 표의 수치를 함께 갱신한다.

- 대상: `core/doc/spec/README.{ko.md,md}`와 `core/doc/spec/core/` 아래 Markdown 52개
- 집계 SHA-256: `5bd7451d4b2d073b9a5e8d58120a396e0be63f8742c49605e341b554fdec0824`
- 공개 표면 target: 함수 196(유지 107, signature 변경 유지 1 포함 + 신규 89), 제거 함수 76.
  신규 identifier 468개의 kind별 수량과 hash는
  [`s1-core-public-api-inventory.ko.md`](./s1-core-public-api-inventory.ko.md) §11과 일치해야 한다
  (FUNC 89·TYPE 31·ENUM_TYPE 16·ENUMERATOR 100·FIELD 220·MACRO 12, 집계
  `2778708021ddee185cfba2c09f065f1a0c1156231b433e0d2148cb5e0a657b98`).

각 행의 마지막 열은 S4 시작 시점의 red gate 분류다. 실행 중 red·green 전환과 결과는 중앙 실행
진행표에 기록하며 이 matrix의 값을 현재 상태처럼 갱신하지 않는다. 테스트가 없는 계약은 완료로
처리하지 않는다.

## 1. 검증 축

| 축 | 내용 | 실행 형태 |
|---|---|---|
| V1 | 공개 header compile, ABI 값, exported symbol | header-only compile test, 정적 assert, `nm` 대조 스크립트 |
| V2 | 성공·오류·errno·ownership·lifetime | unit/contract test |
| V3 | multipart, backpressure, timeout, shutdown | contract/integration test |
| V4 | callback 재진입, claim, thread safety | contract/stress test |
| V5 | MeshNode·Spot·Actor·STREAM 및 peer failure | integration/topology test |
| V6 | 기존 raw socket 회귀 | 기존 test suite 유지 통과 |
| V7 | 삭제 API·bridge·죽은 code/file no-hit | scoped grep manifest + build |
| V8 | 성능 baseline 회귀 | `bindings/c/perf/run_benchmarks_multi.sh` S0 §8.4 case |
| V9 | bindings에서 사용할 C ABI smoke | 설치 header+so만 사용하는 clean C consumer |

## 2. 목표 배치 (구현 예정 위치)

구현 전 계획이므로 실제 파일명은 구현 중 확정하고 이 표를 갱신한다.

| 영역 | Header | Runtime source | API source | Test |
|---|---|---|---|---|
| MeshNode lifecycle·peer | `core/include/zlink/service/mesh_node.h`(신설) | `core/src/runtime/services/mesh/` (신설; 기존 `spot/node`·`data_plane` 대체) | `core/src/api/mesh/` (신설) | `core/tests/unittest/unittest_mesh_node_contract.cpp`, `core/tests/integration/test_mesh_node_lifecycle.cpp`, `test_mesh_peer_admission.cpp` |
| Dispatch(ready·claim·batch·reply) | `core/include/zlink/service/dispatch.h`(신설) | `core/src/runtime/services/mesh/dispatch/` | `core/src/api/mesh/` | `unittest_mesh_dispatch_contract.cpp`, `test_mesh_claim_batch.cpp`, `test_mesh_reply_token.cpp` |
| Spot | `core/include/zlink/service/spot.h`(전면 교체) | `core/src/runtime/services/mesh/spot/` | `core/src/api/mesh/` | `test_mesh_spot_messaging.cpp`, `test_mesh_spot_subscription.cpp` |
| Actor·transfer | `core/include/zlink/service/actor.h`(전면 교체) | `core/src/runtime/services/mesh/actor/` | `core/src/api/mesh/` | `test_mesh_actor_lifecycle.cpp`, `test_mesh_actor_transfer.cpp` |
| STREAM session | `core/include/zlink/service/stream_session.h`(신설) | `core/src/runtime/services/mesh/stream_session/` | `core/src/api/mesh/` | `test_stream_session_service.cpp` |
| Monitoring | `core/include/zlink/eventing/api.h`(증분) | `core/src/api/monitoring/` | 동일 | `test_mesh_monitor.cpp` + 기존 monitoring suite |
| Polling | `core/include/zlink/eventing/api.h` | `core/src/api/monitoring/poller_*` | 동일 | `test_mesh_poller_source.cpp` + 기존 `test_spot_poller` 대체 |
| errno·version | `core/include/zlink_errno.h`, `zlink/common.h` | `core/src/api/core/` | 동일 | `unittest_result_enum_mapping.cpp`(확장), `unittest_public_contract_headers.cpp`(확장) |
| Raw socket 유지 | `core/include/zlink/socket/api.h`(service 잔재 제거) | `core/src/runtime/sockets/`, `api/socket/` | 동일 | 기존 integration suite |

## 3. Spec 절별 대응

### 3.1 service/01-mesh-node

| Spec 절 | 계약 요지 | 검증 축 | Test(예정) | 상태 |
|---|---|---|---|---|
| §1 | MeshName 하나·ROUTER bind 하나·process당 unique, 복수 ChannelName | V2 | 중복 MeshName `EEXIST`, 복수 mesh 독립 | red 예정 |
| §2 | 상수·enum 숫자·versioned 구조체 ABI | V1 | header 정적 assert + ABI 값 test | red 예정 |
| §3 | new/set_bind/start/shutdown/destroy, child handle 수명, readiness 계산 | V2·V3·V5 | lifecycle 상태표 test, child-busy `EBUSY`, shutdown timeout | red 예정 |
| §4 | membership CREATED-only, weight 0..100 runtime 변경 | V2 | add/weight 시점·중복 `EEXIST`·start 뒤 `EBUSY` | red 예정 |
| §5 | peer intent·admission(MeshName/RID/generation/trust), MIXED 병합, drain 교체 | V2·V5 | admission matrix test(충돌 `EEXIST`/`ESTALE`/`EACCES`), 중복 endpoint 병합 | red 예정 |
| §6 | node/channel send·request, 선택+submit 원자, local membership 선택, FIFO | V2·V3·V5 | not-connected/`ENOENT`, round-robin 분포, local single-node, FIFO 검증 | red 예정 |
| §6.1 | Node claim record kind 4종, completion/SEND_READY는 infra claim | V2·V4 | record kind·domain 분리 test | red 예정 |
| §7 | publisher target snapshot, 대상별 ROUTER backpressure와 detail count | V2·V3 | 부분 전달 수치, `DONTWAIT`·timeout, `ENOENT` no-target | red 예정 |
| §8 | metadata frame 형식·1024 상한·검증, wire envelope 비노출 | V2·V3 | malformed matrix(빈 key/dup key/trailing/UTF-8/상한), local=remote 동일 계약 | red 예정 |
| §9 | option 지원표, HWM profile, mailbox budget, `EMSGSIZE`/`EINVAL`/`ENOBUFS` | V2 | option matrix test(handle별 `ENOTSUP` 포함) | red 예정 |
| §10 | status/peers/peer_channels snapshot, `ENOBUFS` 규약 | V2 | count-only 조회, capacity 부족 재시도 | red 예정 |
| §11 | thread-safe 축, `EDEADLK` 재진입, 오류 우선순위 | V2·V4 | 검증 순서 test(draining 우선), shutdown/destroy 재진입 | red 예정 |

### 3.2 service/02-dispatch

| Spec 절 | 계약 요지 | 검증 축 | Test(예정) | 상태 |
|---|---|---|---|---|
| §1 | record·claim·token 타입 ABI, kind_data 대응표 | V1·V2 | 타입 크기·값 assert, kind_data 종류 test | red 예정 |
| §2 | ready handler mask 계약, NULL 해제, 재진입 `EDEADLK`, poller 배타 `EBUSY` | V2·V4 | handler 등록/해제/재진입, 반환 mask rearm | red 예정 |
| §3 | ready batch drain, domain별 claim, take_claim 1회, release thread-safe·post-destroy | V2·V4 | non-empty `EBUSY`, 이중 take `ESTALE`, destroy 뒤 release | red 예정 |
| §4 | receive batch complete multipart, BUFFER_TOO_SMALL+required, retain, 동시 사용 `EBUSY` | V2·V3 | capacity 경계, reset/retain 수명, 멀티스레드 `EBUSY` | red 예정 |
| §5 | reply token one-shot, `EALREADY`/`ESTALE`/`ESHUTDOWN`, borrowed input | V2·V3 | 이중 reply, timeout 뒤 도착 폐기, join token 오용 `EINVAL` | red 예정 |
| §6 | claim release rearm, infra 독립 진행, revoke 뒤 recv `ESHUTDOWN` | V3·V4 | lost-wakeup 0 검증, shutdown deadline revoke | red 예정 |

### 3.3 service/03-spot

| Spec 절 | 계약 요지 | 검증 축 | Test(예정) | 상태 |
|---|---|---|---|---|
| §2·§3 | facade 수명, entry Spot, lookup/get_or_new, generation 증가 | V2 | facade/logical 분리, `ENOENT`, created flag, `ESHUTDOWN` | red 예정 |
| §4 | channel send/request(소스 Spot envelope) | V2·V3 | MeshNode channel 계약과 동일 matrix | red 예정 |
| §5 | direct send/request, generation 0 `EINVAL`, address 인코딩, `ESTALE`/`ENOENT` completion | V2·V5 | generation matrix, remote 부재 completion | red 예정 |
| §6 | Spot publish=publisher 계약+source 기록 | V2·V3 | ROUTER backpressure·topic 검증 공유 test | red 예정 |
| §7 | local subscription exact/prefix, idempotent, 원자적 전환 | V2·V4 | match matrix, 동시 set/publish 원자성 | red 예정 |
| §8 | Spot record kind·control lane, 단일 application claim | V2·V4 | domain 분리, 순차 turn | red 예정 |
| §9 | Spot timer generation 격리, handler 상호 배제 | V2·V4 | 종료 뒤 tick 미전달, timer/claim 비동시 | red 예정 |
| §10 | thread safety·`EDEADLK` | V4 | destroy 동시성 | red 예정 |

### 3.4 service/04-actor

| Spec 절 | 계약 요지 | 검증 축 | Test(예정) | 상태 |
|---|---|---|---|---|
| §1 | ActorRef·location·control record ABI, epoch 독립성 | V1·V2 | 타입 assert, 같은 RID 재생성 시 location 불변 | red 예정 |
| §2 | actor_new transaction·rollback·재시도 generation, lookup local/remote, destroy drain | V2·V3 | conflict/backpressure/timeout rollback, `ESTALE` | red 예정 |
| §3 | join snapshot 검증, one-shot join reply, epoch CAS, ACCEPTED만 commit | V2·V5 | generation 0/`ESTALE`, reject detail, token 재사용 backpressure 재시도 | red 예정 |
| §4 | 4개 메시징 API, mailbox 직접 enqueue, FIFO | V2·V3 | source 검증, completion 라우팅(Node vs Actor infra) | red 예정 |
| §5 | Actor claim record kind 제한, 단일 application claim | V2·V4 | kind·domain matrix | red 예정 |
| §6 | transfer prepare/commit/activate/abort 전체 fence protocol | V2·V3·V5 | source/target role matrix, idempotent retry, `ESTALE`/`EALREADY`/epoch+1, allowance backpressure, data-plane 실패 phase record | red 예정 |
| §7 | shutdown·thread safety | V4·V5 | DRAINING 뒤 `ESHUTDOWN`, claim 보존 | red 예정 |

Transfer data plane의 peer 재전송·ACK는 두 MeshNode 프로세스(또는 두 node in-process)와 deterministic
장애 주입으로 검증한다. S4-15A의 "deterministic fake location authority"는 test 코드가 framework
authority 역할을 대신하는 fixture로 구현한다.

### 3.5 service/05-stream-session

| Spec 절 | 계약 요지 | 검증 축 | Test(예정) | 상태 |
|---|---|---|---|---|
| §1·§2 | service handle 1:1:1, lifecycle, destroy 순서 `EBUSY` | V2 | 중복 등록 `EEXIST`, busy destroy | red 예정 |
| §3 | bind/unbind CAS·idempotent, bindings query `ENOBUFS` | V2 | generation CAS matrix | red 예정 |
| §4·§5 | session→Actor, Actor→bound session, FIFO, close CAS | V2·V3 | binding 부재/`ENOTCONN`/backpressure, FIFO | red 예정 |
| §6 | 이동 barrier, allowance, disconnect terminal | V3·V5 | transfer 연동(3.4 §6과 공동 test) | red 예정 |
| §7 | 직렬화·`EDEADLK`·`ESHUTDOWN` | V4 | callback 내 shutdown | red 예정 |

### 3.6 공통 계약 (context·message·errors·errno·events·polling·monitoring·utilities)

| Spec | 계약 요지 | 검증 축 | Test | 상태 |
|---|---|---|---|---|
| service/README | versioned 구조체 공통 규칙(struct_size/version 검사, 실패 result표) | V1·V2 | 공통 helper test로 전 구조체 순회 | red 예정 |
| 01-context | 기존 계약 유지 | V6 | 기존 `test_ctx_*` 통과 | 기존 green 유지 |
| 02-message | `zlink_msg_gets` 제거, 나머지 유지 | V6·V7 | msg_gets no-hit + 기존 msg suite | red 예정 |
| 03-errors | 신규 result 값(`ZLINK_SUBMIT_NOT_ADMITTED`=13, `ZLINK_REQUEST_BACKPRESSURED`=113 등)과 HAUSNUMERO errno, version 10.0.0 | V1·V2 | enum 값 정적 assert, `zlink_version()` | red 예정 |
| 04-errno-map | result×errno 대응 전체 | V2 | 각 계약 test에서 result와 errno 동시 검증(별도 mapping unit 포함) | red 예정 |
| 05-events | family 경계·level-trigger 의미 | V2·V4 | dispatch·monitor test에 포함 | red 예정 |
| 06-polling | MESH_NODE poller source, POLLIN/POLLOUT 의미, handler 배타, POLLCOMPLETION 유지 | V2·V4·V6 | poller source matrix, 배타성 `EBUSY` | red 예정 |
| 07-monitoring §1 | raw socket monitor 유지 | V6 | 기존 monitoring suite | 기존 green 유지 |
| 07-monitoring §2~5 | MeshNode monitor open/handler/recv/status/close, event mask, overflow aggregate | V2·V4 | event kind matrix, single consumer, child reference | red 예정 |
| 08-utilities | timer/thread/stopwatch/proxy 유지, `zlink_spot_timer_new` 새 Spot 계약 | V2·V6 | 기존 utility suite + Spot timer 신규 test | red 예정 |

### 3.7 socket 계약 (유지·정리)

| Spec | 10.0.0 변화 | 검증 축 | Test | 상태 |
|---|---|---|---|---|
| socket/README | 공통 옵션·수신 모델 유지, MeshNode 공용 함수(`zlink_set_option` 등) handle 확장 | V2·V6 | 기존 suite + MeshNode handle 분기 test | red 예정 |
| 01-pair | 변화 없음 | V6 | 기존 `test_pair_*` | 기존 green 유지 |
| 02-pub·04-xpub | service branch 제거, raw 계약 유지 | V6·V7 | 기존 pubsub suite + service handle 거부 test | red 예정 |
| 03-sub·05-xsub | `zlink_subscribe_part` 부족 buffer 무소비 재시도(CI-08) | V2·V6 | required-length·no-consume retry test | red 예정 |
| 06-dealer | 변화 없음 | V6 | 기존 dealer suite | 기존 green 유지 |
| 07-router | `zlink_router_recv_part`에서 `source_spot_rid_out_` 제거, spot ingress 3함수 제거 | V1·V2·V6·V7 | signature compile test, 기존 router suite 갱신 | red 예정 |
| 08-stream | actor binding 함수 제거(STREAM session service로), NOTIFY·3 수신 모드 유지 | V2·V6·V7 | 기존 stream suite 갱신 | red 예정 |

## 4. 삭제 no-hit manifest

검색 범위는 `core/`(src·include·tests·CMake·packaging), 검색 제외는 `framework/doc/plan/v10.0/`과
review log다. S7에서 bindings까지 확장한다.

| Family | 검색 문자열 | 제거 산출물 |
|---|---|---|
| SpotNode 이름 | `spot_node`, `SpotNode`, `zlink_spot_node_` | `core/src/api/spot/core/service_spot_node_api.cpp`, `runtime/services/spot/node/` 전체 |
| mode·PUB/SUB plane | `ZLINK_SPOT_NODE_MODE_`, `mesh_pub`, `mesh_xsub`, `set_pub_bind`, `set_pub_routing_id`, `set_sub_routing_id` | `runtime/services/spot/data_plane/` 및 `pubsub/`의 물리 plane 부분 |
| bridge | `zlink_spot_route_bridge_`, `RouteBridge` | `api/spot/core/service_spot_route_bridge_*`(5파일), `spot.h` 선언, `test_spot_route_bridge_api.cpp` |
| service part API | `_spot_part`, `zlink_spot_send_spot_part` 등 §5 대체표의 57개 | `api/spot/request_reply/` part 계열, `api/actor/spot/` part 계열 |
| 이전 dispatch | `zlink_spot_dispatch_event_handler`, `SpotDispatchHandler`, `spot_dispatch_worker` | `runtime/services/spot/dispatch/spot_dispatch_worker_pool.*`, `api/spot/dispatch/` |
| remote subject | `zlink_spot_node_subjects`, `spot_subject_`, remote subscription protocol | `runtime/services/spot/pubsub/spot_subject_*`, `unittest_spot_subject_access.cpp` |
| 이전 completion | channel dealer bridge, per-request service callback, reply drain | `api/spot/request_reply/service_spot_request_reply_{callback_api,channel_bridge}.cpp`, `zlink_spot_drain_*` |
| 예약 API | `zlink_msg_gets` | `message/api.h`, `message_api.cpp`, `test_zmp_metadata.cpp` 사용처 |
| headerless export 30 | §9 export 표의 internal 30개 | `ZLINK_EXPORT` 제거 또는 internal linkage 전환 |

no-hit 판정 명령(경로·문자열은 실행 시 기록):

```bash
git grep -nI -e '<string>' -- core/ ':(exclude)framework/doc/plan/v10.0'
```

## 5. ABI·export·package 검증 명령

```bash
# V1: exported symbol = formal FUNC 196 + 정확히 0개 초과
nm -D --defined-only core/build/lib/libzlink.so | awk '$3 ~ /^zlink_/ {print $3}' | LC_ALL=C sort -u
# V1: 역방향 inventory validator (s1-core-public-api-inventory.ko.md §11 스크립트, header 대조로 확장)
# V8: S0 §8.4 대표 성능 (S4 비교는 --runs 3 --duration 3으로 상향해 분산 완화)
PERF_MULTI_RUN_COOLDOWN_MS=0 PERF_MULTI_TRANSPORT_TRANSITION_MS=0 PERF_MULTI_PATTERN_TRANSITION_MS=0 \
  ./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern ROUTER_ROUTER_REQREP,SPOT_REQREP,SPOT_SENDSEND \
  --transports tcp --msg-sizes 64,1024 --duration 3 --clients 100 --runs 3 \
  --results-tag v10_s4_gate
# V9: clean C consumer (설치 prefix만 참조)
cmake --install core/build --prefix <staging> && <staging에서 smoke build·실행>
```

V8 판정: ROUTER_ROUTER_REQREP·SPOT_REQREP·SPOT_SENDSEND의 10.0.0 대응 topology 수치가 S0 §8.4 기준
대비 throughput 90% 이상, p99 120% 이하. 단 S0의 SPOT 패턴은 제거되는 topology이므로 동일 wire-역할의
MeshNode 패턴을 신설해 대응시키고, 서로 다른 topology 수치를 직접 비교하지 않는다는 S0 단서를 따른다.
perf harness의 패턴 전환은 S4-27·S4-28에서 확정해 이 표를 갱신한다.

## 6. Red-test 실행 절차

1. `core/tests/unittest/unittest_public_contract_surface.cpp`(신설)가 신규 468 identifier의 컴파일
   가능성과 제거 76 함수·전용 타입의 부재를 검사한다. 시작 시점에는 실패(red)여야 한다.
2. 계약별 contract test를 spec 절 단위로 추가하며, 구현 전 커밋에서 red임을 확인하고 구현 후 green
   결과를 이 문서와 ledger 증거에 기록한다.
3. 삭제 no-hit은 §4 manifest의 각 문자열에 대해 red(현재 hit 존재) → green(no-hit) 순서로 기록한다.

## 7. 자동 검증 불가 항목과 대체 증거

| 항목 | 사유 | 대체 증거 |
|---|---|---|
| 1천·1만 peer benchmark(S4-27) | 단일 WSL 호스트의 fd·메모리 한계에서 1만 실 peer 프로세스 불가 | in-process 다중 MeshNode topology bench(`core/tests/bench/`)로 connection·lookup·multicast 시간 기록 |
| GitHub Actions RC workflow(S4-22D) | push 전 로컬에서 workflow 실행 불가 | workflow 정적 검사 + S6에서 실제 run URL 기록 |
| 모든 platform 조건부 ABI | Linux 단일 환경 | Linux x86-64 결과 + header 조건부 분기의 compile-only 검사 |

## 8. 실행 상태 참조

S4의 현재 구현 상태, red·green 전환, 남은 작업과 실행 증거는
[`RouteMesh 10.0.0 실행 진행표 §8`](./route-mesh-10.0.0-execution-ledger.ko.md#8-s4--core-구현제거-코드-정리와-정식-spec-일치)의
각 S4 행에서만 확인하고 갱신한다. 이 matrix는 검증 축, 예상 test 배치와 자동 검증 불가 항목만 소유한다.
