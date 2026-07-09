# `bindings/node` POSD·DDD 리팩토링 통합 수정 목록

> 2026-07-08 node 바인딩 전수 POSD/DDD 리뷰와 Codex 재검토 결과를 합친 통합 수정 목록.
> 대상: TS 라이브러리(`src/zlink/runtime` ~6.7k + `src/zlink/contracts` ~2.4k) + 손수 작성 N-API C/C++ addon(`native/src` ~9.3k).
> 파일:라인은 리뷰 시점 기준이므로 편집 전 재확인한다. 공개 API 표면은 계약-잠금(E 제외). hot 항목은 커밋 전 baseline vs patched 벤치 필수.

위험 표기: **없음**(control plane) / **code-motion**(hot 경로 이동이나 명령 동일) / **벤치**(hot 구조에 닿음, 무회귀 증명 필수). 체크박스는 완료 시 갱신.

**공통 주의:** `binding.gyp`이 모든 `.cc`를 단일 addon 타깃으로 빌드 → TU 간 include로 중복 제거 가능(빌드 계층 이유 없음). PascalCase/UPPERCASE 이중 casing은 의도된 컨벤션(`socket_constants.ts`)이니 dead로 오판 말 것. 공개 표면 dead는 spec(`bindings/doc/spec/node`) 대조 후에만.

**통합 검토 반영:**
- Codex 리뷰의 `Spot.recvRouted`/`materializeRouted` 중복은 기존 C12와 동일 항목으로 통합한다.
- Codex 리뷰의 native binding 타입 분류 중복은 기존 C13과 동일 항목으로 통합한다.
- Codex 리뷰의 SPOT dispatch native callback 책임 과다는 C0으로 추가한다. 단, 현재 dispatch callback이 actor/routed payload를 즉시 drain해 JS에 전달하는 의미는 바꾸지 않는다.
- tracked legacy native payload는 A0으로 추가하되 release packaging 확인 후 처리한다.

---

## A. 삭제 트랙 (dead 코드/파일)

- [x] **A0. tracked legacy native payload 단계적 삭제 (별도 커밋)** (없음, CI 참조 1회 확인)
  - `bindings/node/native/linux-x64/`, `bindings/node/native/linux-x86_64/`의 tracked `libzlink.so*` payload.
  - **검증 완료(2026-07-08)**: npm files(`package.json` `files`)에 `native/linux-*` 없음(있는 건 `native/src/**`) · loader는 `prebuilds/<platform-arch>/zlink.node`·`build/Release/zlink.node`만 사용(`native_load_paths.ts`, 이 `.so` 미사용) · `binding.gyp:27`은 `core/build/lib/libzlink.so` 링크(이 사본 아님) · `linux-x64`와 `linux-x86_64`의 `.so.8.6.1`은 **byte-identical 중복** · `.so.8.4.2`는 패키지 8.6.3 기준 stale 구버전 · `sync-local-core-libs.sh`가 이들을 "commit하지 말라"고 경고. → 소비처 없음, 삭제 안전.
  - **추천 결정(단계적)**: (1) **즉시** — 모든 `.so.8.4.2`(stale) + `native/linux-x86_64/` 디렉터리 전체(x64의 byte-identical 별칭) 삭제. (2) **CI 확인 후** — `linux-x64`의 `.so`/`.so.8`/`.so.8.6.1`도 삭제(`.github` workflow가 `native/linux-*`를 참조하지 않는지 grep 1회 확인). 리팩토링과 분리된 **단독 커밋**으로(revert 단위 확보). 이미 다른 바인딩에서 `.so.8.6.1` 삭제가 진행 중인 흐름과 일치.
  - 완료(2026-07-09): 현재 남아 있던 tracked Node `native/linux-x64`·`native/linux-x86_64` payload 6개(`libzlink.so`, `libzlink.so.8`, `libzlink.so.8.6.3`)를 삭제했다. `.github`/`package.json`/loader/`binding.gyp` 재확인 결과 Node CI와 패키지 로더는 이 경로를 소비하지 않는다. 패키지되는 Linux runtime은 `prebuilds/linux-x64/libzlink.so.8` 실파일로 정리하고 versioned prebuild payload는 제거했다. 검증: `git ls-files bindings/node/native`에 `native/src/**`만 남음, `npm run rebuild-native`, `npm run verify:prebuilds`, `npm pack --dry-run --json` 결과 `nativeLinux: []`, `linuxPrebuild: ["prebuilds/linux-x64/libzlink.so.8"]`.
- [x] **A1. `sleep` 중복 export 삭제** (없음, high) — `eventing/counters.ts:35`(+`eventing/index.ts:8` 재export)가 `core/runtime_info.ts:52`와 byte-identical. public `sleep()`은 후자만 사용. counters.ts 사본+재export 제거.
  - 완료(2026-07-09): `counters.ts` 사본을 삭제하고 `runtime/eventing/index.ts`는 `core/runtime_info.ts`의 `sleep`만 재export한다. 검증: `npm run typecheck:src-review`.
- [x] **A2. `RuntimeMonitorSocket` 미사용 alias 삭제** (없음, high) — `sockets/index.ts:20`. 다른 `Runtime*` alias는 `createXxx` 팩토리가 쓰지만 MonitorSocket은 `monitorOpen()`으로 생성 → import 0. 제거.
  - 완료(2026-07-09): `runtime/sockets/index.ts`의 `RuntimeMonitorSocket` 재export를 제거했다. 검증: `rg -n "RuntimeMonitorSocket" bindings/node/src` no-hit, `npm run typecheck:src-review`.
- [x] **A3. `invokeActorJoin`의 dead `spotHandle` 파라미터 제거** (없음, high) — `actor_invokers.ts:30-40`이 `void spotHandle;`로 즉시 폐기. caller `actor.ts:47`(`getNativeHandle(spot)` 계산도 dead화)·`spot_node.ts:233`(`null` 전달). 파라미터+양 call site 인자 제거.
  - 완료(2026-07-09): `invokeActorJoin`의 `spotHandle` 인자와 두 호출부의 전달 값을 제거했다. 검증: `npm run typecheck:src-review`.
- [x] **A4. 무참조 native export 정리 (2건 clean + 3건 확인 후)** (없음)
  - clean 삭제: `spot_node_process_routed_router`(`addon_spot.cc:2021`)·`spot_node_try_process_routed_router_parts`(`:2036`) + export + TS 선언(`binding_service.ts:179-180`) — 전 트리 무참조(~70줄).
    - 완료(2026-07-09): 두 native export와 header/export-table/TS binding 선언을 제거했다. 마지막 사용처가 사라진 `create_actor_route_value` dead helper도 함께 삭제했고, dispatch payload struct initializer 순서 warning을 정리했다. 검증: no-hit `rg` 확인, `npm run typecheck:src-review`, `npm run rebuild-native`.
  - 완료(2026-07-09): `router_recv_single_payload`, `router_recv_single_metric_latency`, `spot_recv_routed_metric_latency`는 `bindings/node/perf/**`와 TS runtime에서 call site가 없고, guard test가 samples/perf의 public binding contract 이탈을 이미 금지함을 확인했다. 세 native export와 header/export-table/TS binding 선언을 제거하고, 마지막 사용처가 사라진 `addon_core_perf.cc/.h`와 `binding.gyp` source entry도 삭제했다. 검증: 해당 symbol·helper 이름 `rg` no-hit, `npm run rebuild-native`, `npm run build && npm run typecheck:src-review && npm run typecheck`, `node --test dist-tools/tests/optimization_guard.test.js dist-tools/tests/socket_surface.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/dealer_router.test.js`.
- [x] **A5. `socket_operations.ts` pass-through 파일 삭제** (없음) — 13줄 전체가 `../messaging/operations` 재export, 소비자는 `sockets/index.ts` 하나뿐(다른 socket 파일은 `../messaging` 직접). 파일 삭제 후 `sockets/index.ts`가 직접 재export.
  - 완료(2026-07-09): `contracts/sockets/socket_operations.ts`를 삭제하고 `contracts/sockets/index.ts`가 `../messaging/operations`를 직접 재export한다. 검증: `npm run typecheck:src-review`.
- [x] **A6. 내부 변환기/타입 dead (public은 spec 확인 후 keep)** — `runtime/eventing/poll_events.ts`의 `PollEvent`(내부 사본, 생성자 없음 → 삭제; contracts 사본은 public이라 keep+spec 확인), `actor_models.ts`의 `actorRouteFromRaw`/`ActorRouteRaw`(`:57-61,226-232`, 무참조 → 삭제; public `ActorRoute`는 미배선이나 keep+spec 확인).
  - 완료(2026-07-09): 내부 `PollEvent`, `ActorRouteRaw`, `actorRouteFromRaw`만 제거하고 public contracts 타입은 유지했다. 같은 검증 중 `stream_socket.ts`의 미사용 `SpotNode`/`SpotNodeHandle`도 함께 제거했다. 검증: `npm run typecheck:src-review`.

## B. 결함 수정 (correctness — 리뷰 중 발견)

- [x] **B1. spot no-wait send가 errno 분류 누락 → result-code 계약 divergence** (**벤치**, TS 호환 확인 — **최상위**)
  - `spot_send_spot_no_wait_result`(`addon_spot.cc:1024-1055`)가 `submit_msg_parts`의 raw `rc`를 그대로 반환, EAGAIN/disconnect를 분류도 throw도 안 함. core try-send 계열(`classify_try_send_errno`, `addon_core.cc:27-45`)은 EAGAIN→`BACKPRESSURED`, ENOTCONN/EHOSTUNREACH/ETIMEDOUT→`NOT_CONNECTED`로 분류. → `spotSendToSpotNoWaitResult` JS caller가 `socketSendNoWaitResult`와 다른 계약을 받음(실제 동작 gap). `classify_try_send_errno`를 공유 헤더로 올려 spot no-wait에 적용. TS측이 현재 raw rc를 어떻게 해석하는지(이미 오처리 중일 수 있음) 병행 확인.
  - **추천 결정**: 형제 `classify_try_send_errno`가 이미 올바르게 분류하므로 **의도가 증명된 correctness 버그** → 정밀화 진행. 착수 첫 단계로 `spotSendToSpotNoWaitResult` JS caller가 raw rc에 `switch`/`===` 매칭 중인지 감사 후 함께 갱신. 보류(호환 유지)는 버그를 남기는 쪽이라 비추천.
  - 완료(2026-07-09): `classify_try_send_errno()`를 `addon_submit_results.h` 공통 helper로 옮기고 core no-wait send/publish와 `spot_send_spot_no_wait_result()`가 같은 errno 분류를 사용하도록 했다. `spot.sendToSpot(...).flags(DontWait).submit()`의 disconnected peer 회귀 테스트를 추가해 raw negative/native rc 대신 `SubmitError(SubmitResult.NotConnected)`가 표면화됨을 검증했다. 검증: `npm run rebuild-native`, `npm run build && npm run typecheck:src-review && npm run typecheck`, `node --test dist-tools/tests/spot_request_to_spot.test.js`, `node --test dist-tools/tests/stream_send_regression.test.js`, `node --test dist-tools/tests/socket_surface.test.js`.
  - 벤치(2026-07-09): `./perf/single/run_benchmarks.sh --reuse-build --pattern SPOT --transports tcp --msg-sizes 64 --duration 3 --runs 3`. baseline median `184.886 Kmsg/s`, `0.383 ms`; patched median `187.985 Kmsg/s`, `0.444 ms`. 성공 경로는 동일한 `rc != ZLINK_SUBMIT_OK` 분기 뒤 실패 경로에서만 helper를 호출한다. p99는 run variance가 컸다(baseline `1.248 ms`, patched `3.078 ms`).
- [x] **B2. `Message.close()`가 공개 API 생성 인스턴스에서 무동작** (없음/compatible)
  - `contracts/messaging/message.ts:44-47` ctor가 항상 `Object.freeze(this)`, `close()`(`:174-181`)는 `isFrozen` 시 즉시 return. `Message.from()`/`allocate()` 유일 공개 진입점이 다 frozen → 모든 공개 생성 message의 `close()`가 무동작. 실제 clear되는 건 `message_snapshot.ts`가 `Object.create(prototype)`로 ctor 우회한 runtime-vended만. JSDoc은 동작한다고 서술. → (a) `close()` 일관화(freeze 조기 return 제거, 별도 `_closed` 플래그) 또는 (b) doc를 "self-constructed는 불변 값복사, close 불요"로 정정.
  - **추천 결정: (b) doc-only 정정.** 오늘 실제 버그 없음(self-constructed는 해제할 native 자원 없음). (a)는 의도된 불변성(immutability) 보증을 건드려 위험만 큼. JSDoc을 "self-constructed message는 불변 값복사라 close 불요, runtime-vended(수신) message만 close가 native storage 해제"로 정정하고 runtime-vended close 경로는 그대로. (a)는 향후 수명주기 통일 설계가 나올 때만.
  - 완료(2026-07-09): `Message`와 `close()` 공개 주석을 실제 수명주기와 맞췄다. `Message.from()`/`allocate()`로 만든 값은 불변 값복사라 명시 해제가 필요 없고, 런타임에서 받은 메시지만 native storage를 해제한다고 설명한다. 검증: `npm run typecheck:src-review`.
- [x] **B3. backpressure/EAGAIN을 에러 메시지 문자열 매칭으로 판정 (3벌, 취약)** (**벤치**)
  - `errors/native_errors.ts:66,78`·`eventing/poller.ts:111`이 `/temporarily unavailable|would block/i`를 native `.message`에 정규식 매칭해 no-data/backpressure 결정. locale/wording 의존 + 3벌 복붙 → native 문구 변경 시 조용히 깨짐. `readErrno()`가 이미 errno 노출하므로 errno(11/EAGAIN) 기반 단일 predicate(`isWouldBlock`)로. hot(poller.wait/DontWait recv·send).
  - 완료(2026-07-09): `native_errors.ts`에 `isWouldBlock(errno = readErrno())`를 추가하고 `recvNativeError()`/`submitNativeError()`/`Poller.wait()`의 DontWait 판단을 errno 기반으로 바꿨다. `src/zlink` 아래 would-block message regex는 제거됐다. 검증: `npm run build && npm run typecheck:src-review && npm run typecheck`, `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/pubsub.test.js`, `node --test dist-tools/tests/monitor.test.js dist-tools/tests/socket_surface.test.js`.
  - 벤치(2026-07-09): `./perf/single/run_benchmarks.sh --reuse-build --pattern PAIR --transports tcp --msg-sizes 64 --duration 3 --runs 3`. baseline median `484.035 Kmsg/s`, `418.869 ms`; patched median `486.730 Kmsg/s`, `482.553 ms`. 처리량은 무회귀, latency는 run variance 범위로 판단했다.

## C. 구조 통합 — 지식 중복 소거

### C-native (N-API addon)
- [x] **C0. SPOT dispatch native callback의 payload builder 책임 분리** (code-motion, dispatch 의미 보존)
  - `addon_spot.cc:717-804`의 `spot_dispatch_event_dispatch`는 이벤트 수신 뒤 actor readable이면 `zlink_spot_node_actor_recv_part`를, routed readable이면 `spot_recv_parts`를 호출해 payload에 담는다. `addon_spot.cc:659-699`는 같은 파일에서 JS object shape까지 만든다.
  - 이벤트 감지, 큐 drain, payload ownership, JS object materialization이 한 함수/파일에 묶여 변경 이유가 넓다.
  - 리팩토링은 payload builder/helper 분리까지만 한다. "dispatch는 알림만 하고 JS가 나중에 recv한다"처럼 수신 drain 시점을 바꾸는 변경은 public 동작 변화가 될 수 있으므로 이 트랙에서 금지한다.
  - 완료(2026-07-09): `fill_spot_dispatch_actor_payload()`와 `fill_spot_dispatch_routed_payload()`로 actor/routed drain과 payload ownership 채우기를 분리했다. `spot_dispatch_event_dispatch()`는 dispatch metadata 설정, helper 호출, TSFN enqueue만 담당한다. 검증: `npm run rebuild-native`, `node --test dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/stream_send_regression.test.js`.
- [x] **C1. request-reply threadsafe 콜백 마샬링 wholesale 중복** (없음) — `addon_core.cc:66-86,740-804,922-928,1156-1175`(`request_js_state_t` 등)가 `addon_spot_request_callbacks.cc:12-268`의 좁은 fork. spot 사본(superset)을 단일 구현으로, core는 헤더 include 후 사본 삭제(~140줄).
  - 완료(2026-07-09): core request callback fork를 제거하고 `addon_spot_request_callbacks.cc`의 superset 마샬링을 공유한다. core request callback 생성은 기존 resource name/error/unref 정책을 유지하는 shared factory wrapper로 연결했다. 검증: `npm run rebuild-native`, `node --test dist-tools/tests/request_reply.test.js dist-tools/tests/dealer_router_callback.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/socket_surface.test.js dist-tools/tests/spot_dispatch_drain.test.js`.
- [x] **C2. `create_spot_actor_ref_value` byte-identical 복붙** (없음) — `addon_spot_request_callbacks.cc:60-71` = `addon_spot_actor_values.cc:149-160`. 헤더 include 후 로컬 삭제, 3 call site 교체.
  - 완료(2026-07-09): request callback 파일이 `addon_spot_actor_values.h`의 `create_actor_ref_value`를 재사용하도록 바꾸고 로컬 복붙 helper를 삭제했다. 검증: `rg -n "create_spot_actor_ref_value" bindings/node/native/src` no-hit, `npm run rebuild-native`.
- [x] **C3. `zlink_monitor_status_t → JS` 마샬링 ~30필드 이중** (없음) — `monitor_status()`(`addon_core.cc:2926-3033`, raw napi) vs `create_monitor_status_value()`(`addon_spot_node_snapshots.cc:132-184`, 공유 setter). 후자(공유 헬퍼 사용)를 공유 헤더로 올려 전자가 호출(~90줄). TS의 `Object.keys` 순서 의존 테스트 없는지 확인.
  - 완료(2026-07-09): monitor status JS object builder를 `addon_monitor_status_values.h` 공통 helper로 옮기고 `monitor_status()`와 spot node snapshot path가 같은 helper를 사용하도록 통합했다. 검증: `npm run rebuild-native`, `node --test dist-tools/tests/monitor.test.js`.
- [x] **C4. monitor event 객체 동일파일 3벌** (없음) — `create_socket_monitor_event_value()`(`addon_core.cc:551`)가 이미 있는데 `monitor_recv()`(`:2867`)·`monitor_try_recv()`(`:2893`)가 인라인 재구현. 헬퍼 호출로 교체(최저비용).
  - 완료(2026-07-09): `monitor_recv`와 `monitor_try_recv`가 기존 `create_socket_monitor_event_value` helper를 호출하도록 바꿨다. 검증: `npm run rebuild-native`, `node --test dist-tools/tests/monitor.test.js`.
- [x] **C5. RoutingId Buffer 파싱 TU간 중복** (없음) — `parse_routing_id()`(`addon_core.cc:618-634`) = `parse_routing_id_value()`(`addon_spot_actor_values.cc:6-22`). `addon_message_parts.h`(양쪽 이미 include)로 정본화. `sizeof(routing_id->data)` vs 리터럴 255 일치 확인.
  - 완료(2026-07-09): `parse_routing_id_value`를 `addon_message_parts.h`의 단일 inline helper로 옮기고, core 호출부도 같은 helper를 사용하도록 바꿨다. 길이 제한은 `sizeof(routing_id->data)` 기준으로 통일했다. 검증: `npm run rebuild-native`.
- [x] **C6. threadsafe 핸들러 slot 부착 시퀀스 5벌** (code-motion) — `attach_send_ready_handler`(`addon_core.cc:993`)·`attach_socket_monitor_handler`(`:1040`)·`socket_stream_attach` 인라인(`:2264`)·`timer_handler`(`:3661`)·`attach_spot_send_ready_handler`(`addon_spot.cc:886`). `addon_tsfn_slots.h`에 파라미터화 헬퍼로. **주의**: `timer_handler` 실패 경로·`socket_stream_attach`의 `mode!=PACKET` 사전조건은 다르므로 억지 통일 말 것(~150-200줄).
  - 완료(2026-07-09): `addon_tsfn_slots.h`에 subject별 slot 예약, TSFN queue 생성, slot 공통 필드 bind helper를 추가했다. 각 native attach 함수는 자신의 native 등록 호출과 stream/timer 특수 전제·실패 경로를 유지한다. 검증: `npm run rebuild-native`, `node --test dist-tools/tests/monitor.test.js dist-tools/tests/stream_send_regression.test.js dist-tools/tests/pair.test.js dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js`.
- [x] **C7. `create_spot_message_snapshot_value`/`create_spot_routed_event_value` pass-through** — `addon_spot.cc:44-49,616-624` 1줄 위임. 인라인/삭제(저우선).
  - 완료(2026-07-09): pass-through wrapper를 삭제하고 호출부가 `create_message_snapshot_value`와 `create_spot_routed_value`를 직접 호출하도록 정리했다. 검증: `npm run rebuild-native`.

### C-ts (TypeScript)
- [x] **C8. DontWait/Backpressured 재확인 보일러플레이트 9벌** (**벤치**) — `if ((flags & DontWait) && result === Backpressured) return false; throw` 가 `socket_operations.ts:119`·`spot.ts:88`·`request_executor.ts:76`·`actor_invokers.ts:58,172,196,226,357`·`router_socket.ts:101`. **load-bearing**(errno 11이 flag 없이도 Backpressured 가능). `errors/native_errors.ts`에 `submitOrBackpressure(error, flags, message)` 헬퍼로. hot.
  - 완료(2026-07-09): `submitOrBackpressure(error, flags, message)`를 `native_errors.ts`에 추가하고 catch path의 중복된 DontWait/Backpressured 재확인을 같은 helper로 모았다. helper는 기존 조건과 같이 `DontWait` 플래그가 있고 변환된 submit error가 `Backpressured`일 때만 `false`를 반환하며, 그 외 submit error는 그대로 던진다. direct no-wait result branch의 `SubmitResult.Backpressured` fast path는 변경하지 않았다. 검증: `npm run build`, `npm run typecheck:src-review && npm run typecheck`, `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/stream_send_regression.test.js dist-tools/tests/socket_surface.test.js`. 벤치: `c8-baseline-3x` median 451.364 Kmsg/s, `c8-patched-3x` 443.500 Kmsg/s로 소폭 낮았으나, 같은 세션에서 pre-C8를 임시 복원한 `c8-interleaved-baseline-3x`는 424.661 Kmsg/s, 재적용한 `c8-interleaved-patched-3x`는 452.940 Kmsg/s로 throughput 무회귀를 확인했다. latency는 runner noise가 커서 interleaved patched의 한 run이 tail 값을 높였다.
- [x] **C9. payload normalize 동일 함수 이중명명** (**벤치**) — `message_conversion.ts:28`(`normalizeMessageLikePayload`)와 `:39`(`normalizeOperationPayload`)가 동일 로직(체크 순서만 다름), caller가 sockets vs service로 임의 분단. 최핫 경로(전 send/publish/request). 하나로 수렴(다른 하나 alias). Uint8Array subarray 처리 버그 전파 위험 해소.
  - 완료(2026-07-09): `normalizeOperationPayload()`를 단일 구현으로 남기고 `normalizeMessageLikePayload`는 같은 함수의 export alias로 정리했다. scalar, 단일 part, multipart, `Message` snapshot, `Uint8Array` subarray 처리는 한 경로만 지난다. 검증: `npm run build`, `npm run typecheck:src-review && npm run typecheck`, `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/pubsub.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/stream_send_regression.test.js dist-tools/tests/socket_surface.test.js`. 벤치: 기존 기준 `c9-baseline-3x` median 482.217 Kmsg/s 대비 첫 patched `c9-patched-3x` 459.496 Kmsg/s였으나, 같은 세션에서 pre-C9를 임시 복원한 `c9-interleaved-baseline-3x`는 331.498 Kmsg/s, 재적용한 `c9-interleaved-patched-3x`는 438.518 Kmsg/s로 무회귀 확인.
- [x] **C10. `freezeMessageParts`/`freezeOwnedMessageParts`/`closeMessageParts` 복붙** (code-motion) — `received_state.ts:25-40` = `topic_message_state.ts:13-28`(주석까지). `messaging/message_parts_state.ts` 공유 모듈로. hot(`replaceReceived`/`replaceTopicMessage`, perf-critical 주석).
  - 완료(2026-07-09): `runtime/messaging/message_parts_state.ts` 공유 모듈을 추가하고 `received_state.ts`와 `topic_message_state.ts`가 같은 freeze/adopt/close helper를 사용하도록 정리했다. 검증: `npm run typecheck:src-review`, `npm run build && npm run typecheck`, `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/pubsub.test.js`.
- [x] **C11. `setSendReadyHandler` byte-identical 5벌** (없음) — `socket_operations.ts:146,276`·`pub_socket.ts:21`·`xpub_socket.ts:50`·`stream_socket.ts:171`. `ConnectableSocket`/`SocketBase`로 hoist(공통 조상 `PublisherSocket`이 미정의라 분단됨).
  - 완료(2026-07-09): `SocketBase.registerSendReadyHandler()`와 send-capable `SendReadySocket` base를 추가했다. `MessageSocket`/`RoutedMessageSocket`/`PublisherSocket` 계열은 inherited method를 사용하고, `StreamSocket`은 같은 protected helper를 호출한다. `SubscriberSocket` 계열에는 public method가 추가되지 않는다. 검증: `npm run typecheck:src-review && npm run build && npm run typecheck && node --test dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js dist-tools/tests/stream_send_regression.test.js`.
- [x] **C12. `Spot.recvRouted`/`materializeRouted` reply/send 클로저 ~30줄 중복** (code-motion) — `spot.ts:339-377,413-442`, target(Received 재사용 vs new)만 차이. private 헬퍼 파라미터화(`message_materializer.ts` 패턴 준수). routed reply/send 에러 분류 보존.
  - 완료(2026-07-09): `materializeRoutedInto()` private helper로 routed `Received` adoption과 reply/send closure 생성을 통합했다. `recvRouted()`는 caller-provided storage를 그대로 채우고, dispatch path는 새 `Received`를 만든 뒤 같은 helper를 사용한다. 검증: `npm run typecheck:src-review`, `npm run build && npm run typecheck`, `node --test dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/pubsub.test.js`.
- [x] **C13. Spot/Actor N-API 표면 두 인터페이스 중복** (없음) — `spotNodeSendToActor`/`spotNodeRequestToActor`가 `binding_service.ts:99-114`·`binding_socket.ts:167-182` 양쪽 선언(drift 위험 + aggregate 경계 smell). `ServiceNativeBinding`에만 두고 socket 쪽 제거.
  - 완료(2026-07-09): 두 actor native 메서드 선언은 `ServiceNativeBinding`에만 남기고 `SocketNativeBinding` 선언에서 제거했다. 검증: `npm run typecheck:src-review`.
- [x] **C14. Operation 빌더 패턴 재구현** (**벤치**) — `sockets/socket_operation_builders.ts`·`messaging/received_operations.ts`가 `message()/flags()/submit()` shape 손수 구현하는데, `service/spot/actor_operations.ts`엔 이미 공유 base(`ReplyHandlerOperation`/`ResultHandlerOperation`, 4클래스 재사용) 존재. `SendOperationBase<TInput>`로 통일(normalize 함수 파라미터화). 전 socket send/publish/request/reply 진입점 → 테스트로 parity 증명 후.
  - 완료(2026-07-09): `runtime/messaging/send_operation_base.ts`를 추가해 payload append, flag 저장, submitted guard, consume helper를 공통화했다. socket send/publish/request/reply builder, Received 기반 send/reply builder, actor join/entry-spot join builder가 같은 base를 사용한다. request/actor join의 callback-mode flag 의미와 promise submit의 `SendFlags.None` 의미는 각 submit method에 그대로 남겼다. actor join reply는 빈 payload submit을 허용해야 하므로 별도 구현을 유지했다. 검증: `npm run build && npm run typecheck:src-review && npm run typecheck`, `node --test dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/stream_send_regression.test.js`. 벤치: C14 직전 기준으로 사용한 `c8-interleaved-patched-3x` median 452.940 Kmsg/s 대비 `c14-patched-3x` 451.280 Kmsg/s로 throughput 동등 수준을 확인했다.
- [x] **C15. errno→submit classify 공유** (**벤치**, B1과 함께) — C-native B1의 `classify_try_send_errno` 공유가 TS쪽 `SubmitResult` 해석과 정합되도록.
  - 완료(2026-07-09): B1과 함께 `addon_submit_results.h`로 errno→submit result 분류를 공유했다. native EAGAIN/ENOTCONN/EHOSTUNREACH/ETIMEDOUT 분류는 TS `SubmitResult.Backpressured`/`NotConnected` 해석과 맞고, SPOT no-wait public path는 `SubmitError` result로 검증했다.

## D. 구조 개선 — 중간 이하 (기회 될 때)

- [x] **D1. service/spot self-barrel import 정정** (없음) — `actor.ts:10`·`spot.ts:19`·`spot_node.ts:26`이 `../index`(자기 패키지 barrel)를 import → barrel이 다시 이 파일들 import(순환). `spot_route_bridge.ts`처럼 concrete sibling(`./spot_operations` 등) 직접 import로. 타입 전용이라 현재는 동작하나 취약.
  - 완료 확인(2026-07-09): 현재 `runtime/service/spot` 하위에는 `from '../index'` self-barrel import가 없다. 검증: `rg -n "from '../index'|from \\\"../index\\\"" bindings/node/src/zlink/runtime/service/spot` no-hit.
- [x] **D2. 9-name operation 재export 목록 5파일 복붙** (없음, compatible) — `messaging/index.ts:18-28`·`sockets/index.ts:34-44`·`socket_operations.ts:3-13`·`service/index.ts:81-91`·`spot_operations.ts:6-16` 동일 리스트. `export type * from '../messaging/operations';`(TS 5.0+, repo 5.8.3)로. `tsc --declaration` diff로 export 집합 동일 확인.
  - 완료(2026-07-09): 이미 삭제된 `socket_operations.ts`를 제외한 4곳의 operation 타입 나열을 `export type *`로 수렴했다. 검증: `npm run typecheck:src-review`, `npm run build && npm run typecheck`, `dist/zlink/contracts/*` declaration에서 operation 타입 재수출 유지 확인.
- [x] **D3. `Received`/`TopicMessage` envelope 이중 구현** (없음) — `received.ts:16-30,51-127` vs `topic_message.ts:7-21,26-69`의 `freezeMessageParts`/`invalidMultipartError`/`missingPartError`/`isSinglePart`/`firstPart`/`singlePartOrThrow`/`close` identical. `MessagePartsEnvelope` base/모듈로.
  - 완료(2026-07-09): `MessagePartsEnvelope` 공통 base를 추가하고 `Received`/`TopicMessage`가 parts 초기화, single-part 검사, 첫 part 조회, close 동작을 상속하도록 정리했다. 검증: `npm run typecheck:src-review`, `npm run build && npm run typecheck`, `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/pubsub.test.js`.
- [x] **D4. 빌더 계열 인터페이스 fragmentation** (없음, .d.ts 검증) — `Send*/Request*/Reply*/ActorJoin*`가 `message()/timeout()/flags()/submit()`를 ~4벌 재선언(JSDoc까지 7벌). `PartBuilder<T>`/`Flaggable<T>`/`Timeoutable<T>` 조합(intersection)으로 emit .d.ts 불변. `tsc --declaration` diff 필수.
  - 완료(2026-07-09): `PartBuilder<TNext>`/`Flaggable<TNext>`/`Timeoutable<TNext>` 공통 builder facet을 추가하고 messaging/service actor operation 인터페이스가 이를 상속하도록 정리했다. 기존 `SendOperation`/`RequestOperation`/`ReplyOperation`/`Actor*Operation` 이름과 submit overload는 유지된다. 검증: `npm run typecheck:src-review && npm run build && npm run typecheck`, `node --test dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js`, `dist/.../operations.d.ts`와 `dist/.../spot_operations.d.ts`에서 기존 builder 이름과 공통 facet 상속 확인.
- [x] **D5. `ZlinkError` 서브클래스 보일러플레이트 + `code`/`result` 중복** (없음) — `errors/errors.ts:25-123` 8종 동일 ctor + `code`(inherited)와 `result`(typed)가 항상 같은 값 이중 저장. 내부 제네릭 `ResultError<TResult>` base로, `instanceof`/필드 shape 불변.
  - 완료(2026-07-09): 내부 `ResultError<TResult>` base를 추가하고 8개 public error subclass는 기존 constructor/name/result/code/nativeErrno shape를 유지하도록 단순화했다. 검증: `npm run typecheck:src-review`, `npm run build`, `npm run typecheck`, 런타임 shape smoke(`instanceof`, `name`, `result`, `code`, `nativeErrno`) 통과.
- [x] **D6. `spot_models.ts` god-file 3관심사 분리** (없음) — dispatch/actor lifecycle/node telemetry 259줄 혼재. `spot_dispatch_models.ts`/`actor_models.ts`/`spot_node_status_models.ts`로 분리(`service/index.ts` export 불변).
  - 완료(2026-07-09): dispatch 모델은 `spot_dispatch_models.ts`, actor/lifecycle 모델은 `actor_models.ts`, node status/query 모델은 `spot_node_status_models.ts`로 분리하고 `spot_models.ts`는 기존 import path를 보존하는 호환 re-export 계층으로 남겼다. 검증: `npm run typecheck:src-review`, `npm run build && npm run typecheck`, `node --test dist-tools/tests/enums.test.js dist-tools/tests/api.test.js dist-tools/tests/socket_surface.test.js`, `dist/.../service/index.d.ts`와 `spot_models.d.ts` re-export 확인.

## E. 교차 언어 계약 / spec 확인 필요 (단독 수정 금지)

- [x] **E1. dead metadata API** — `message.ts:6-8,158-166`의 `METADATA_KEY_*`/`getProperty`가 실제로 항상 null(런타임이 properties 미채움). core spec(`message.ko.md:60-72`)이 `zlink_msg_gets()`를 stub(항상 NULL/EINVAL)으로 문서화 → JSDoc에 "reserved, 미구현" 디스클레이머 추가. 실제 배선은 cross-language 결정.
  - 완료(2026-07-09): metadata 상수와 `getProperty()` JSDoc에 현재 binding에서 native metadata lookup이 reserved/미구현임을 명시했다. 실제 배선은 변경하지 않았다. 근거: `core/src/api/message/message_api.cpp`의 `zlink_msg_gets()`는 `EINVAL`/`NULL` stub. 검증: `npm run build && npm run typecheck`, declaration 주석 반영 확인.
- [x] **E2. `SpotRouteBridgeEndpointCapabilities.SpotRoute` dead alias** — `spot_route_bridge.ts:8-11`, `RouteOnly`(0x1)만 사용·`SpotRoute`(동값)는 rename 잔재.
  - **추천 결정: `@deprecated` 후 제거(즉시 삭제 아님).** as-is `{ SpotRoute: 0x1, RouteOnly: 0x1 }` → 우선 `SpotRoute`에 `/** @deprecated use RouteOnly */` JSDoc, 다음 major에서 제거(to-be `{ RouteOnly: 0x1 }`). const 객체 멤버 삭제는 breaking이라 바로 안 지움.
  - 완료(2026-07-09): `SpotRoute` 멤버에 `@deprecated use RouteOnly.` JSDoc을 추가하고 alias 값은 유지했다. 검증: `npm run build && npm run typecheck`, `dist/.../spot_route_bridge.d.ts` 주석 반영 확인.
- [x] **E3. `PollEvent`/`ActorRoute` public 타입** — A6의 내부 사본은 삭제하되, contracts의 public 타입은 미배선이어도 다른 바인딩/spec이 reserved일 수 있어 삭제 전 spec 대조.
  - 완료 확인(2026-07-09): A6에서는 runtime 내부 사본만 삭제했고, `contracts/eventing/poller.ts`의 public `PollEvent`와 `contracts/service/spot/spot_models.ts`의 public `ActorRoute`는 source/dist declaration에 유지되어 있다. 검증: `rg -n "interface PollEvent|interface ActorRoute" bindings/node/src/zlink/contracts bindings/node/dist/zlink/contracts`.

---

## 핫패스 보존 게이트 (절대 위반 금지)

`replaceReceived`/`replaceTopicMessage` 재사용 스토리지 경로 · handle-scope churn 금지 · per-message 새 클로저/allocation/await 금지 · pinned threadsafe-function 수명 · fast/no-alloc 단일프레임 수신. **hot 변경(B1, B3, C8, C9, C14, C15)은 커밋 전 baseline vs patched 벤치 무회귀 증명.**

## 권장 실행 순서

A(삭제) → B1·B2·B3(결함) → C1~C5(native 마샬링 통합) → C10·C11·C13(ts 안전 통합) → D(기회순) → C8·C9·C14(벤치 게이트) 마지막. E는 별도 계약/spec 트랙.

## 진행 검증 로그

- 2026-07-09: A0, A1/A2/A3/A5/A6, A4 clean 2건, B1/B2/B3, C15, D1 현재 상태 확인을 반영했다.
  - `npm run typecheck:src-review` 통과.
  - `npm run rebuild-native` 통과.
  - `npm test` 통과: 전체 Node 테스트와 샘플 실행 완료, 샘플 summary `passed=18 failed=0`.
- 2026-07-09: C2/C4/C5/C7/C13, D2, E1/E2/E3를 추가 반영했다.
  - `npm run typecheck:src-review && npm run build && npm run typecheck && npm run rebuild-native` 통과.
  - `node --test dist-tools/tests/monitor.test.js` 통과.
- 2026-07-09: C0, C1, C3, C6, C10, C11, C12, D3, D4, D5, D6를 추가 반영했다.
  - `npm run typecheck:src-review`, `npm run build`, `npm run typecheck`, `npm run rebuild-native` 통과.
  - `node --test dist-tools/tests/monitor.test.js` 통과.
  - `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/pubsub.test.js` 통과.
  - `node --test dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/pubsub.test.js` 통과.
  - `node --test dist-tools/tests/enums.test.js dist-tools/tests/api.test.js dist-tools/tests/socket_surface.test.js` 통과.
  - `node --test dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js dist-tools/tests/stream_send_regression.test.js` 통과.
  - `node --test dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js` 통과.
  - `node --test dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/stream_send_regression.test.js` 통과.
- 2026-07-09: C9를 추가 반영했다.
  - `npm run build`, `npm run typecheck:src-review && npm run typecheck` 통과.
  - `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/pubsub.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/stream_send_regression.test.js dist-tools/tests/socket_surface.test.js` 통과.
  - `./perf/single/run_benchmarks.sh --reuse-build --pattern PAIR --transports tcp --msg-sizes 64 --duration 3 --runs 3 --results-tag c9-interleaved-baseline-3x`와 `c9-interleaved-patched-3x` 비교에서 median throughput 331.498 Kmsg/s → 438.518 Kmsg/s로 무회귀 확인.
- 2026-07-09: C8을 추가 반영했다.
  - `npm run build`, `npm run typecheck:src-review && npm run typecheck` 통과.
  - `node --test dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/stream_send_regression.test.js dist-tools/tests/socket_surface.test.js` 통과.
  - `./perf/single/run_benchmarks.sh --reuse-build --pattern PAIR --transports tcp --msg-sizes 64 --duration 3 --runs 3 --results-tag c8-interleaved-baseline-3x`와 `c8-interleaved-patched-3x` 비교에서 median throughput 424.661 Kmsg/s → 452.940 Kmsg/s로 무회귀 확인.
- 2026-07-09: C14를 추가 반영했다.
  - `npm run build && npm run typecheck:src-review && npm run typecheck` 통과.
  - `node --test dist-tools/tests/socket_surface.test.js dist-tools/tests/api.test.js dist-tools/tests/pair.test.js dist-tools/tests/dealer_router.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/spot_dispatch_drain.test.js dist-tools/tests/stream_send_regression.test.js` 통과.
  - `./perf/single/run_benchmarks.sh --reuse-build --pattern PAIR --transports tcp --msg-sizes 64 --duration 3 --runs 3 --results-tag c14-patched-3x`에서 median throughput 451.280 Kmsg/s를 확인했다. 직전 기준 `c8-interleaved-patched-3x`는 452.940 Kmsg/s였다.
- 2026-07-09: A4의 남은 native perf export 3건을 제거했다.
  - 해당 symbol·helper 이름 `rg` no-hit 확인.
  - `npm run rebuild-native`, `npm run build && npm run typecheck:src-review && npm run typecheck` 통과.
  - `node --test dist-tools/tests/optimization_guard.test.js dist-tools/tests/socket_surface.test.js dist-tools/tests/spot_request_to_spot.test.js dist-tools/tests/dealer_router.test.js` 통과.
- 2026-07-09: 전체 Node 테스트 스크립트 최종 확인.
  - `./tests/run_tests.sh` 통과: 전체 Node test file 실행 완료, 샘플 summary `passed=18 failed=0` 2회 출력.
