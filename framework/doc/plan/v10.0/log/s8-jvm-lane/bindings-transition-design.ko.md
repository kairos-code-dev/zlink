# S8-JVM lane — Java/Kotlin bindings 10.0.0 전환 설계

RouteMesh 10.0.0: SpotNode/route-bridge/PUB-SUB/push-dispatch → MeshNode + pull dispatch + spot/actor/
stream_session. **Runtime raw-socket 레이어 생존.**

## 아키텍처 특이점
- JNI C++ glue가 아니라 **Java FFI / Project Panama**(`java.lang.foreign`, `MethodHandle` downcall,
  Core 심볼명 문자열 키). "native 레이어" = `runtime/nativeapi/`의 FFI 심볼 테이블
  (`Native.java` 2,375, `NativeSpotSymbols.java` 1,178). C bridge 1개
  `native/src/zlink_java_reqrep_bridge.c`(127).
- **Kotlin은 Service 레이어 없음**: `bindings/kotlin/`은 Java 런타임(`systems.zlink.*`)을 공유하는
  samples만. Service 전환은 전부 `bindings/java/`, Kotlin은 samples 재작성만.

## 규모 (Java)
- contracts service 서브트리 ≈ 2,117줄, runtime service 서브트리 ≈ 5,704줄, FFI 심볼 테이블 포함 ≈ 9,500+줄.
- 최대: `NativeSpotNode.java`(713), `NativeSpot.java`(670), `SpotNodeActorOperations.java`(650),
  `SpotRoutedSupport.java`(582), `NativeSpotSymbols.java`(1,178).

## 개명 (SpotNode→MeshNode)
- `SpotNode.java`(facade) → `MeshNode`, `NativeSpotNode.java`(`zlink_spot_node_*`) → mesh_node,
  `zlink_spot_node_*` FFI 문자열 전면.
- `SpotNodeStatus`·`State`·`PeerEntry`·`PeerFilter` → mesh_node status/peer로 축소.
  `SpotNodePublisher` → mesh_node publisher.

## 삭제
- Route bridge: `NativeSpotRouteBridge.java`(323), `SpotRouteBridge*.java` contracts 3, FFI
  `NativeSpotSymbols.java` L237~269(7 심볼).
- router-direct spot: `runtime/sockets/NativeRouterSpotSupport.java`(213),
  `Native.java` L396~425(`zlink_router_request/reply/send_spot_part`·`router_recv_part`),
  `NativeRouterReceive.java`, C bridge의 `zlink_router_enable_spot_receive` spot-receive 분기.
- subjects/internal-sockets: `SpotNodeSubjectEntry`·`SubjectFilter`·`SocketEntry`·`SocketFilter`,
  enums `SubjectKind`·`SpotDispatchSubjectKind`·`SpotNodeSocketOwner`, FFI `_subjects`(L298)·
  `_internal_sockets`(L303).
- pub bind/rid: `SpotNodeTuning.java` 대부분, FFI `_set_pub_bind`(L123)·`_set_pub_routing_id`(L129)·
  `_set_sub_routing_id`(L133)·`_set_router_bind`(L126).
- dispatch worker(push): `SpotDispatchSupport.java`·`zlink_spot_dispatch_event_handler` → pull.
- spot-level actor 열거: FFI `_spots`(L308)·`_actors`(L312).
- message property: `NativeMessage.java` `MH_MSG_GETS`(:32, :129).

## 재작성 (모델 역전)
- 수신 push→pull: `_actor_recv_part`(L192)·`zlink_spot_recv_part`·`recv_actor_lifecycle`·
  `actor_join_recv`·`reply_spot_part`/`reply_router_part` → ready/claim/receive_batch + `mesh_reply`.
  `SpotDispatchSupport.java`·`NativeSpot.java`·`NativeActor.java`.
- 송신: batch + operation_id. `SpotSendPlane.java`·`SpotRequestPlane.java`.
- 구독: `SpotSubscriptionSupport.java` `subscribe_part`/`set_subscription` → `spot_set/unset_subscription`.
- stream_session: `_actor_bind_remote_session`·`_close_bound_session`·`_forward_bound_session_part` →
  `zlink_stream_session_service_*`. `SpotNodeActorOperations.java`.

## 배선
- `build.gradle:7` version 9.0.4 → 10.0.0(Maven publication + `lib/libzlink.so*` resource copy 구동).
- `native/linux-x64`·`native/linux-x86_64`(+ `src/main/resources/native/...`)의 `libzlink.so` 심링크
  `.so.9`→`.so.10` repoint. `LibraryLoader.java`가 unversioned `libzlink.so`를 `System.load`.

## samples (Java + Kotlin 각 동일 12쌍 재작성 + 1쌍 삭제)
- 삭제: `SpotChannelExample.{java,kt}`.
- 재작성: `SpotPubSubExample`·`SpotRpcExample`·`SpotTimerExample`·`SpotRecvSample`·`SpotRequestAsyncSample` +
  actor 6 + `SampleSupport.java`. Kotlin은 Java 재작성 추종(독립 로직 없음).
- 생존: `DealerRouterRecv`·`PairRecv`·`PubSubRecv`·`MonitorRecv`·`StreamRecv`·`StreamPacketCallback`·
  `RequestReplyAsync`.

## 순서
cpp 패턴 확정 후 미러. 군: 1)FFI 심볼 테이블(`NativeSpotSymbols.java`·`ActorInterop.java`·`Native.java`
targeted) retarget 2)contracts 개명·삭제 3)runtime mesh_node/spot/actor 재작성 + stream_session
4)router-direct·route_bridge 삭제 5)samples(Java+Kotlin) 6)build.gradle 버전·심링크 7)`gradle build`
green → smoke → bindings 리뷰 campaign.
