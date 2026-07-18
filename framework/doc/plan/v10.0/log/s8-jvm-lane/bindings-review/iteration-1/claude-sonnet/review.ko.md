# S8 JVM bindings 전환 리뷰 — iteration 1 — R2 (Claude Sonnet)

## 1. Scope 확인

- 대상 commit: `5c2eb2acc` (HEAD `990f70339`은 이 위의 freeze commit이며 scope 내 diff 없음 — `git diff 5c2eb2acc..HEAD --stat -- bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` → empty)
- 시작 시점: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle | grep -vE 'native/linux|native/darwin|native/win|resources/native|/build/' | LC_ALL=C sort | xargs sha256sum | sha256sum` → **255개** 파일, aggregate SHA-256 = `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12` (manifest 값과 일치, java 236 + kotlin 19)
- 종료 시점: 동일 명령 재실행 → **255개** 파일, 동일 hash(변경 없음, 리뷰 중 scope 파일 미수정을 `git status`로 확인)
- 검토 방법: Core 10.0.0 공개 헤더(`core/include/zlink/{common.h,message/api.h,socket/api.h,service/{common,mesh_node,dispatch,actor,spot,stream_session}.h}`, `core/include/{zlink_enum.h,zlink_errno.h}`) 전량 정독 + JVM FFI 계층(`runtime/nativeapi/{Native,NativeServiceSymbols,ServiceInterop,ServiceLayouts,NativeLayouts,NativeMessage,NativeSymbols}.java`) 전량의 `FunctionDescriptor`를 Core 함수 시그니처와 파라미터 수·타입 단위로 1:1 대조 + 발견된 downcall마다 `runtime/sockets/*.java` → `contracts/sockets/*.java`를 따라가며 공개 API 도달 가능성 확인(죽은 코드 vs 실사용 경로 구분) + core/src 일부 구현(`socket_api.cpp`, `precompiled.hpp`) 교차 확인 + 작업 트리에 이미 스테이징되어 있던 실제 Core 10.0.0 아티팩트 `bindings/java/native/linux-x64/libzlink.so.10.0.0`에 대한 `nm -D`(읽기 전용 심볼 테이블 조회, build/실행 아님)로 헤더 기반 판정을 이중 검증. build/test/run 미실행(정적 대조; 국소 grep/read/nm만 사용).

## 2. I1 — 계약 구현 일치(FFI downcall 시그니처/arity, struct layout, pull-dispatch, join-reply route)

### Finding 1 [I1][blocker] — `zlink_router_recv_part` FFI downcall이 Core 10.0.0과 파라미터 개수가 다르다(레거시 spot_rid out-param 잔존)

- **파일/라인**: `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/Native.java:389-402`(`MH_ROUTER_RECV_PART`, `MH_ROUTER_RECV_PART_CRITICAL` 선언), `Native.java:1535-1567`(`routerRecvPart`/`routerRecvPartNoWaitCritical` 래퍼), `bindings/java/src/main/java/systems/zlink/runtime/sockets/NativeRouterReceiveSupport.java:292,407,487,523-530`(호출부)
- **문제**: Core 10.0.0의 `zlink_router_recv_part`(`core/include/zlink/socket/api.h:271-277`)는 `(router_, source_node_rid_out_, request_seq_out_, part_out_, has_more_out_, flags_)` 6개 파라미터다(과거 버전에 있던 spot_rid out-param은 제거됨 — `framework/doc/plan/v10.0/log/s8-common-raw-layer-drift.ko.md` 항목 2가 명시적으로 경고). 그런데 JVM의 `FunctionDescriptor`는
  ```java
  FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT)
  ```
  으로 ADDRESS 6개 + INT 1개 = **7개 파라미터**를 선언한다. `routerRecvPart` 래퍼도 `(router, sourceNodeRidOut, sourceSpotRidOut, requestSeqOut, partOut, hasMoreOut, flags)` 형태로 `sourceSpotRidOut`이라는 여분의 ADDRESS 인자를 실제로 전달한다(`Native.java:1543-1545`). 즉 JVM은 Core가 이미 제거한 구버전 6-param(spot_rid 포함) 시그니처를 그대로 유지하고 있다.
- **근거**:
  - `core/include/zlink/socket/api.h:271-277` — 현재 공개 C 시그니처(6 파라미터, spot_rid out 없음)
  - `framework/doc/plan/v10.0/log/s8-common-raw-layer-drift.ko.md:12` — "`zlink_router_recv_part` 시그니처 변경(spot_rid out-param 제거) — cpp·node lane이 이미 수정" → JVM lane은 미수정임을 이번 검토로 확인
  - `bindings/java/src/main/java/systems/zlink/runtime/sockets/NativeRouterReceiveSupport.java:280-337`(`recvDirectOnceIntoImpl`) — `sourceSpotRidOut`을 읽어 `Received`의 spot_rid로 전달하는 로직이 살아있음(`ContractAccess.receivedPopulateRoutedSinglePart(target, nodeRidBytes, spotRidBytes, ...)`)
  - `nm -D bindings/java/native/linux-x64/libzlink.so.10.0.0 | grep zlink_router_recv_part` → `T zlink_router_recv_part`(심볼 자체는 존재 — 즉 downcall 바인딩은 성공하고, 잘못된 arity로 "성공적으로" 호출되어 조용히 데이터가 깨지는 것이 문제의 핵심이다. 심볼 부재로 인한 즉각적인 예외가 아니다)
  - 공개 API 도달 경로: `bindings/java/src/main/java/systems/zlink/contracts/sockets/RoutedSocketContracts/RouterSocket.java`의 `recv(Received, RecvFlags)` → `NativeRouterSocket.recv()`(`runtime/sockets/NativeRouterSocket.java:58-70`) → `InternalAccess.routerRecvInto/routerRecv` → `NativeRouterReceiveSupport` → `Native.routerRecvPart`. `DealerRouterRecvSample.kt`(kotlin scope) 등 샘플이 이 경로를 직접 사용한다.
- **영향**: Panama 네이티브 링커는 `FunctionDescriptor`를 그대로 신뢰해 호출 규약을 구성한다. 실제 Core 10.0.0 `libzlink.so`가 6개 인자만 받는 함수에 대해 JVM이 7개 인자(그중 1개는 C 함수가 전혀 모르는 여분의 포인터)를 전달하면, 레지스터/스택 인자 매핑이 한 칸씩 밀려 `request_seq_out_`가 실제로는 `has_more_out_` 슬롯 값을 받는 등 인자 misalignment가 발생한다. 결과값은 정의되지 않으며(garbage request-seq, 잘못된 more-flag, 잘못된 메시지 데이터 또는 크래시), `RouterSocket.recv()`를 호출하는 모든 JVM 애플리케이션(request/reply 서버 측, spot inbound routing 등)이 이 경로를 탄다. `compileJava`는 통과하지만(Java 컴파일러는 C 심볼과 무관), 실제 Core 10.0.0과 통합해 실행하면 첫 ROUTER recv부터 관찰 가능한 손상이 발생한다.
- **수정 제안**: `MH_ROUTER_RECV_PART`/`MH_ROUTER_RECV_PART_CRITICAL`의 `FunctionDescriptor`에서 여분의 ADDRESS 파라미터를 제거해 6-param으로 맞추고, `routerRecvPart`/`routerRecvPartNoWaitCritical`과 `NativeRouterReceiveSupport`에서 `sourceSpotRidOut`/spot_rid 관련 로직을 제거(또는 Core 10.0.0의 대체 경로인 `zlink_spot_*`/`zlink_mesh_node_*` service API로 재설계)한다.

### Finding 2 [I1][blocker] — 레거시 raw STREAM attach/detach 계열이 Core 10.0.0에 없는 심볼로 배선되어 있고, `zlink_stream_detach`는 공개 `Socket.close()` 계약에서 항상 실행된다

- **파일/라인**: `Native.java:133-166`(`MH_STREAM_ATTACH`, `MH_STREAM_ATTACH_RAW`, `MH_STREAM_ATTACH_LEN32BE`, `MH_STREAM_DETACH` 선언), `Native.java:756-799`(`streamAttach`/`streamAttachRaw`/`streamAttachLen32be`/`streamDetach` 래퍼), `bindings/java/src/main/java/systems/zlink/runtime/sockets/SocketCore.java:305-327,368-382`(`attachStreamRaw`/`detachStream`), `bindings/java/src/main/java/systems/zlink/runtime/sockets/NativeStreamSocket.java:96-105`(`close()`)
- **문제**: Core 10.0.0의 raw STREAM 공개 API는 `zlink_stream_packet_handler` 하나뿐이다(`core/include/zlink/socket/api.h:85-86`). 과거의 직접 attach 모델(`zlink_stream_attach`, `zlink_stream_attach_raw`, `zlink_stream_attach_len32be`, `zlink_stream_detach`)은 core 공개 헤더 어디에도 선언되어 있지 않다(`grep -rn "zlink_stream_attach\b\|zlink_stream_attach_raw\|zlink_stream_attach_len32be\|zlink_stream_detach" core/include/` → 0건 전체). core 소스 측에서도 `zlink_stream_attach_raw`/`zlink_stream_detach`의 구현은 `core/src/runtime/utils/precompiled.hpp:43-45`, `core/src/api/socket/socket_api.cpp:68,90`에 `ZLINK_INTERNAL_EXPORT`(비-Windows에서는 빈 매크로, 즉 `ZLINK_EXPORT`의 `visibility("default")`를 받지 못함)로만 남아 있어, core 자신도 이 심볼들을 공개 ABI에서 내린 것으로 보인다. 그럼에도 JVM `Native.java`는 이 4개 심볼 모두를 여전히 downcall로 선언하고 `SocketCore`/`NativeStreamSocket`이 그중 일부를 실사용한다.
- **근거 — reachability 확인**:
  - `zlink_stream_detach`: **실사용, 공개 계약 경로**. `NativeStreamSocket.close()`(공개 `StreamSocket extends Socket`의 `close()` 구현)가 핸들이 유효하면 무조건 `detachStream()`을 호출하고(`NativeStreamSocket.java:96-105`), 이는 `SocketCore.detachStream()`(`SocketCore.java:368-382`) → `Native.streamDetach()`(`Native.java:793-799`) → 부재 심볼 `zlink_stream_detach`로 이어진다. kotlin `StreamRecvSample.kt`(scope 내)의 `ctx.createStreamSocket().use { server -> ... }` 같은 통상적 try-with-resources 패턴이 이 경로를 그대로 탄다.
  - `zlink_stream_attach_raw`: 실사용이지만 **package-private 경로만**(`SocketCore.attachStreamRaw`, `NativeStreamSocket.onPacketNative`) — 공개 `contracts/sockets/StreamSocket.java` 인터페이스는 `onPacket(StreamPacketHandler)`(→ `zlink_stream_packet_handler`, Core에 실존)만 노출하고 `attachRaw`/`onPacketNative`류는 노출하지 않는다. 따라서 이 경로는 현재 공개 API로는 도달 불가능하지만, 컴파일된 채 남아 있는 폐기 레이어다.
  - `zlink_stream_attach`, `zlink_stream_attach_len32be`: **완전 dead**(`streamAttach`/`streamAttachLen32be` 래퍼의 호출부가 `Native.java` 자신 외에는 scope 전체에서 0건).
  - 같은 계열로 확인된 추가 dead downcall: `zlink_stream_send`(`streamSend`, 0 외부 호출), `zlink_stream_send_msg`(`streamSendMessage`, 0), `zlink_stream_bind_actor`/`zlink_stream_unbind_actor`/`zlink_stream_send_bound_actor_part`(각 0) — 전부 core 공개 헤더에 대응 심볼 없음(`zlink_stream_bind_actor`는 서비스 계층의 `zlink_stream_session_bind_actor`와 다른 심볼이며, 후자는 `NativeServiceSymbols.java`에 별도로 올바르게 구현되어 있음 — §4 참조), `zlink_stream_bound_actors`(`streamBoundActors`, 0 외부 호출).
  - `zlink_subscribe_handler`: `SocketCore.onSubscribe`(`SocketCore.java:278-290`) → `NativeSocketRuntime.onSubscribe`(public 메서드, `NativeSocketRuntime.java:536-538`)까지는 연결되어 있으나, 공개 `contracts/sockets/PubSubSocketContracts/SubSocket.java`는 poll 기반 `subscribe(TopicMessage, RecvFlags)`(→ `zlink_subscribe_part`, Core에 실존)만 노출하고 `onSubscribe`/`SubscribeHandler`는 노출하지 않는다 — 이 경로도 현재 공개 계약으로는 도달 불가능한 폐기 잔재.
  - 바이너리 이중 검증: `nm -D bindings/java/native/linux-x64/libzlink.so.10.0.0 | grep -E "zlink_stream_(attach|detach|send_msg|bind_actor|unbind_actor|bound_actors)|zlink_stream_send\b"` → **0건**(export된 STREAM 관련 심볼은 `zlink_stream_packet_handler`와 `zlink_stream_session_*` 계열뿐), 같은 파일에 대해 `zlink_subscribe_handler`/`zlink_router_handler` grep도 **0건**. 헤더 기반 판정과 실제 컴파일된 Core 10.0.0 아티팩트가 정확히 일치한다.
- **영향**: `zlink_stream_detach`는 STREAM 소켓의 가장 기본적인 lifecycle 동작(`close()`)에서 항상 실행되므로, Core 10.0.0 실물 라이브러리에 대해 STREAM 소켓을 한 번이라도 연 뒤 닫으면 `IllegalStateException: Missing native symbol 'zlink_stream_detach'`가 발생한다(`NativeSymbols.missingDowncall` — 심볼 미존재 시 즉시 예외를 던지는 handle을 반환하므로 class-load는 살아남지만 첫 호출에서 반드시 실패). scope 내 `StreamRecvSample.kt`, `StreamPacketCallbackSample.kt` 등 STREAM 샘플이 정상 종료 시 이 경로를 그대로 탄다. 나머지(`attach_raw`/`subscribe_handler` 등)는 현재 공개 API에서는 도달 불가능하지만, Core가 이미 걷어낸 레이어를 컴파일된 채 남겨 둔 것이므로 향후 내부 재배선이나 리플렉션 접근 시 동일하게 실패한다.
- **수정 제안**: `zlink_stream_detach` 호출부(`SocketCore.detachStream`)를 Core 10.0.0에 맞게 재설계(예: `zlink_stream_packet_handler(handle, NULL, NULL)`로 콜백 해제 후 별도 상태만 정리, 또는 `close()`가 이 호출 자체를 생략)한다. `zlink_stream_attach`/`_raw`/`_len32be`/`_detach`/`_send`/`_send_msg`/`_bind_actor`/`_unbind_actor`/`_send_bound_actor_part`/`_bound_actors`/`zlink_subscribe_handler` 관련 downcall·래퍼·`SocketCore` 메서드를 전량 삭제한다.

## 3. I2 — POSD·DDD

### Finding 3 [I2][low] — `zlink_router_handler` downcall이 두 파일에 중복 선언되어 있다

- **파일/라인**: `Native.java:385-388`(`MH_ROUTER_SPOT_HANDLER`, `routerHandler` 래퍼는 `Native.java:1524-1532`에 있으나 scope 전체에서 호출부 0건 — dead), `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/NativeMessage.java:36-39`(`MH_ROUTER_HANDLER`, `routerHandler` 래퍼는 `NativeMessage.java:142-149`이며 `NativeRouterReceiveSupport.java:131`에서 실사용)
- **문제**: 동일한 심볼 `"zlink_router_handler"`, 동일한 `FunctionDescriptor.of(I, A, A, A)`가 두 파일에 독립적으로 선언되어 있다. 하나(`Native.java`)는 죽은 코드이고 다른 하나(`NativeMessage.java`)만 실제로 쓰인다. 이 심볼 자체도 core 공개 헤더에 대응이 없다(`grep -rn "zlink_router_handler" core/include/` → 0건) — I1 Finding 2와 같은 raw-layer 폐기 계열에 속하지만, 중복 선언이라는 별개의 POSD 위반이므로 여기서 별도 등록한다.
- **영향**: 유지보수 시 두 선언이 분기(arity·타입 drift)할 위험이 있고, 죽은 사본이 남아 있어 실제 사용처 파악을 방해한다.
- **수정 제안**: `Native.java`의 `MH_ROUTER_SPOT_HANDLER`/`routerHandler`(dead copy)를 삭제하고, `NativeMessage.java`의 선언 하나로 통일한다(이 심볼 자체의 존치 여부는 I1 Finding 2의 raw-layer 정리 범위에 포함해 재검토).

### 그 외 대조 — findings 아님
- `NativeServiceSymbols.java`(mesh_node/actor/spot/dispatch/stream_session 서비스 계층 downcall 877줄 전량)는 섹션별로 Core 헤더 구조(`--- mesh_node lifecycle ---`, `--- actor ---`, `--- spot ---`, `--- dispatch ---`, `--- stream_session ---`)를 그대로 반영하는 얇은 1:1 번역 계층이며, 불필요한 추가 추상화 없이 바인딩 계층에 적절한 깊이를 유지한다. god-file 징후 없음(877줄이지만 responsibilty가 Core 함수 개수에 정비례하는 반복 패턴).

**Verdict I2: NOT CLEAN**(1건, low)

## 4. I3 — 정리 완결성(폐기 잔재·제거 심볼 FFI downcall·no-hit)

### Finding 4 [I3][high] — `NativeLayouts.java`에 Core 10.0.0에 대응이 전혀 없는 레거시 registry/actor-route wire-format 레이아웃 16개가 scope 전체 0-참조 상태로 잔존

- **파일/라인**: `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/NativeLayouts.java:221-593`
- **문제**: 다음 16개 `MemoryLayout` 상수(및 딸린 `*_OFFSET` 상수들)가 선언되어 있다 — `ACTOR_RECV_INFO_LAYOUT`, `ACTOR_JOIN_INFO_LAYOUT`, `ACTOR_CREATE_RESULT_LAYOUT`, `ACTOR_ROUTE_LAYOUT`, `SPOT_ROUTE_LAYOUT`, `ACTOR_JOIN_RESULT_LAYOUT`, `ACTOR_JOIN_ENTRY_SPOT_RESULT_LAYOUT`, `ACTOR_LOOKUP_RESULT_LAYOUT`, `SPOT_SERVICE_ATTACHMENT_STATS_LAYOUT`, `SPOT_SERVICE_MONITOR_EVENT_LAYOUT`, `REGISTRY_STATUS_LAYOUT`, `REGISTRY_SERVICE_SUMMARY_ENTRY_LAYOUT`, `REGISTRY_SERVICE_SUMMARY_FILTER_LAYOUT`, `MEMBER_PEER_ENTRY_LAYOUT`, `REGISTRY_TOPOLOGY_ENTRY_LAYOUT`, `REGISTRY_TOPOLOGY_FILTER_LAYOUT`. 이들은 pre-10.0.0 시절의 "registry 서비스"(topology/peer/service-summary 엔트리)와 "actor route/recv-info/join-info" 와이어 포맷을 그대로 본뜬 것으로 보이나, 현재 Core 공개 헤더 어디에도 `zlink_registry_*` 심볼이 존재하지 않으며(`grep -rn "zlink_registry_" core/include/` → 0건), `actor_recv_info`/`actor_join_info`/`actor_route_t`/`spot_route_t`/`actor_create_result`/`actor_lookup_result` 등도 Core 10.0.0의 `service/actor.h`/`service/dispatch.h`/`service/mesh_node.h`/`service/spot.h`에 대응 타입이 없다(현재 모델은 `zlink_actor_location_t`/`zlink_actor_control_record_t`/`zlink_actor_join_completion_t`/`zlink_mesh_ready_record_t`/`zlink_mesh_receive_record_t`로 전면 대체됨).
- **근거**:
  - `grep -rl "<각 16개 심볼명>" bindings/java/src/main bindings/java/samples bindings/kotlin/samples 2>/dev/null | grep -v NativeLayouts.java` → **16개 전부 0건**(scope 전체에서 `NativeLayouts.java` 자기 자신 외 참조 없음)
  - `grep -rli "registry\|actorroute\|spotroute\|actorjoininfo\|actorrecvinfo\|actorcreateresult\|actorlookupresult\|spotserviceattachment\|spotservicemonitor" bindings/java/src/main --include=*.java` → `NativeLayouts.java` 단 1개 파일만 매치(대응하는 상위 클래스·contract가 전무)
  - `nm -D bindings/java/native/linux-x64/libzlink.so.10.0.0 | grep -c zlink_registry` → **0**(바이너리 이중 검증, 실제 컴파일된 Core 10.0.0에도 registry 심볼 전무)
- **영향**: 정리(cleanup) 미완료 — 약 190줄의 죽은 레이아웃 정의가 남아 있어, 향후 유지보수자가 이 레이아웃이 여전히 유효한 Core 계약이라고 오인할 위험이 있고 diff 노이즈를 유발한다. 프롬프트가 명시한 9개 no-hit 토큰에는 포함되지 않지만, "폐기 개념 잔재"라는 I3의 취지에 직접 해당한다.
- **수정 제안**: 16개 레이아웃 및 딸린 offset 상수를 전량 삭제한다.

### Finding 5 [I3] — I1 Finding 2에 열거한 dead raw-STREAM downcall 계열의 정리 미완료
(I1 Finding 2와 동일 근본원인 — `zlink_stream_attach`/`zlink_stream_attach_len32be`/`zlink_stream_send`/`zlink_stream_send_msg`/`zlink_stream_bind_actor`/`zlink_stream_unbind_actor`/`zlink_stream_send_bound_actor_part`/`zlink_stream_bound_actors`/`zlink_subscribe_handler`가 scope 전체에서 실질적으로 미사용이거나 공개 API에서 도달 불가능함에도 downcall 선언·래퍼가 남아 있음. 상세 근거는 §2 Finding 2 참조. I1에 이미 등록했으므로 여기서는 중복 카운트하지 않고 교차 참조만 남긴다.)

### 폐기 개념 no-hit 판정(프롬프트 명시 9개 토큰)

| 토큰 | 결과 |
|---|---|
| `SpotNode` | 0 |
| `SpotRouteBridge` | 0 |
| `spot_node` | 0 |
| `route_bridge` | 0 |
| `subjects` | 0 |
| `internal_sockets` | 0 |
| `dispatch_workers` | 0 |
| `recv_actor_part` | 0 |
| `msg_gets` | 0 |

전량 0 — coordinator manifest와 일치. (단, §4 Finding 4의 registry/actor-route 레이아웃 잔재는 이 9개 토큰에 포함되지 않는 별도의 정리 미완료 사례임.)

**Verdict I3: NOT CLEAN**(1건 high + I1 Finding 2와 연계된 정리 미완료)

## 5. 종합

- I1: **NOT CLEAN** — Finding 1(blocker), Finding 2(blocker). 둘 다 raw socket 계층에서 Core 10.0.0이 변경/제거한 심볼을 JVM이 따라가지 못한 root-cause이며, 서로 다른 원인(파라미터 개수 drift vs 심볼 완전 부재)이므로 별도 family로 계수했다.
- I2: **NOT CLEAN** — Finding 3(low, 중복 선언).
- I3: **NOT CLEAN** — Finding 4(high, 죽은 registry/actor-route 레이아웃 16개), Finding 2 계열의 dead raw-stream downcall 정리 미완료.
- 대조적으로 mesh_node/actor/spot/dispatch/stream_session **서비스 계층**(RouteMesh 10.0.0의 핵심 전환 대상)은 `struct_size`/`version` 포함 struct layout, downcall 파라미터 수·타입, pull-dispatch(ReadyBatch/Claim/ReceiveBatch/ReplyToken) 수명, actor-join-reply route(`zlink_actor_join_reply`, 5-param)까지 Core 헤더와 정확히 일치하며 findings 없음 — 전환 자체는 견실하게 수행되었으나, **raw socket 레이어(ROUTER recv, STREAM attach/detach 계열)가 pre-10.0.0 상태로 남아 있어 전환이 미완료**임을 확인했다.
- 프롬프트의 독립 판정 요청 사항(Java `StreamSocket`에 `bindActor`가 없어 샘플이 `StreamSessionService`로 세션 바인딩하는 것): 확인 결과 이는 Java 계약 형태이며 결함이 아니다 — `contracts/sockets/StreamSocket.java`는 원래 `bindActor`류 메서드를 노출하지 않고, 서비스 계층 `zlink_stream_session_bind_actor`(`NativeServiceSymbols.java:185-186`, 5-param, Core 시그니처와 일치)가 별도로 정확히 구현되어 있다. 동의.

BINDINGS REVIEW NOT CLEAN
