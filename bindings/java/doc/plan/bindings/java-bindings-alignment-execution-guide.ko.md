# Java Bindings Core Alignment 실행 가이드

> 상태: 진행중
> 기준 문서: `bindings/java/plan/bindings/java-bindings-core-api-alignment-plan.ko.md`
> 대상 범위: `bindings/java/`, `doc/bindings/`, `bindings/java/plan/bindings/`
> 목적: Java bindings를 최신 `core` public surface와 Java 스타일 API 철학에 맞춰 끝까지 정렬하는 실행 순서와 완료 판정 기준 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 메인 플랜 문서의 내용을 실제 코드 변경 순서와 완료 판정 기준으로
고정하는 실행 문서다.

이 문서는 새 설계를 제안하지 않는다.
설계 authority는 아래 메인 플랜 문서 하나로 고정한다.

- [`java-bindings-core-api-alignment-plan.ko.md`](./java-bindings-core-api-alignment-plan.ko.md)
  - 목적 / 상태 / 설계 원칙:
    [`1. 목표`](./java-bindings-core-api-alignment-plan.ko.md#1-목표),
    [`2. 현재 상태 요약`](./java-bindings-core-api-alignment-plan.ko.md#2-현재-상태-요약),
    [`3. 설계 원칙`](./java-bindings-core-api-alignment-plan.ko.md#3-설계-원칙)
  - 고정 결정 / public API:
    [`3.1 범위 고정 결정`](./java-bindings-core-api-alignment-plan.ko.md#31-범위-고정-결정),
    [`4. 공개 API 재정렬 방향`](./java-bindings-core-api-alignment-plan.ko.md#4-공개-api-재정렬-방향),
    [`4.5.1 Java 스타일 API 결정`](./java-bindings-core-api-alignment-plan.ko.md#451-java-스타일-api-결정),
    [`4.5.3 canonical message API 초안`](./java-bindings-core-api-alignment-plan.ko.md#453-canonical-message-api-초안),
    [`4.5.5 성능 계약`](./java-bindings-core-api-alignment-plan.ko.md#455-성능-계약)
  - 단계별 구현 / 검증:
    [`5. 단계별 실행 계획`](./java-bindings-core-api-alignment-plan.ko.md#5-단계별-실행-계획),
    [`6. 파일 단위 작업 범위`](./java-bindings-core-api-alignment-plan.ko.md#6-파일-단위-작업-범위),
    [`9. 최종 완료 기준`](./java-bindings-core-api-alignment-plan.ko.md#9-최종-완료-기준),
    [`부록 B. 구현 착수 전 체크리스트`](./java-bindings-core-api-alignment-plan.ko.md#부록-b-구현-착수-전-체크리스트)

실행 중 설계 판단이 필요해 보이면 먼저 메인 플랜을 갱신하고, 그 다음 이 guide를
맞춘 뒤 코드를 수정한다. 코드와 실행 가이드만 바꿔서 설계 불일치를 남기지 않는다.

## 2. 실행 authority

단일 설계 authority:

- [`java-bindings-core-api-alignment-plan.ko.md`](./java-bindings-core-api-alignment-plan.ko.md)

이 가이드는 아래 내용을 메인 플랜에서 그대로 따른다.

- `Socket` 은 raw 계층에서 `send` / `recv` 만 canonical API로 가진다
- payload와 변환 책임은 `Message` 가 담당한다
- `Message.copyOf*` / `Message.wrap*` 로 copy/borrow 경계를 명시한다
- direct `ByteBuffer`, native `MemorySegment`, direct Netty `ByteBuf` fast path 는 유지한다
- `Received` 는 raw recv 결과와 lifecycle aggregate를 동시에 담당한다
- 검증 자산은 `samples`, `contract tests` 두 층으로 나눈다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 플랜이다.
- 자동 실행이 필요하면 [`run_java_bindings_alignment_execution.sh`](./run_java_bindings_alignment_execution.sh)
  를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
- 공통 supervisor는 guide / master plan / logs / gate label만 주입받는 제너릭
  루프이고, bindings 전용 정책은 이 guide와 메인 플랜이 결정한다.
- 실행 wrapper 자체는 별도 `lock`을 두지 않는다.
  같은 작업을 병렬 실행해야 하면 `--logs-dir` 또는 `--gate-label`을 분리해서
  상태 파일 충돌을 피한다.

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 메인 플랜만으로는 해결할 수 없는 Java public API 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/java/`, `doc/bindings/`, `bindings/java/plan/bindings/`만으로 해결할 수 없는 blocker

위 경우가 아니면:

1. 첫 미완료 slice를 잡는다.
2. 코드 수정과 sample/contract 정리를 같이 한다.
3. 관련 검증을 끝낸다.
4. 이 guide 상태를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

이 가이드는 commit / push를 기본 규칙으로 강제하지 않는다.
commit / push는 사용자 지시가 있을 때만 수행한다.

병렬 실행 규칙:

- 기본값으로 두 개 이상 동시에 돌리지 않는다.
- 병렬 실행이 필요하면 실행 단위마다 `--logs-dir`을 분리한다.
- gate status 파일 충돌을 피하려면 `--gate-label`도 함께 분리한다.
- wrapper는 실행 자체를 막지 않는다.

## 4. 기본 실행 명령

이 섹션은 `현재 즉시 가능한 smoke` 와 `Slice 5 완료 후 최종 검증 흐름` 을 구분한다.

현재 즉시 가능한 smoke:

```bash
cd bindings/java && ./gradlew test --no-daemon

./bindings/java/plan/bindings/run_java_bindings_alignment_execution.sh --max-iterations 0
```

위 두 명령은 현재 저장소 상태에서도 실행 가능해야 한다.

현재 baseline 진단 명령:

```bash
cd bindings/java && ./gradlew integrationTest --no-daemon
```

주의:

- 2026-03-26 현재 `integrationTest` 는
  `dev.kairoscode.zlink.integration.contract.*` 만 실행하도록 좁혔다.
- 즉 기존 ported integration 묶음은 더 이상 gate 대상이 아니며,
  Slice 5에서 `contract/sample/delete` 구조로 실제 삭제/이동을 계속해야 한다.
- 따라서 현재 `integrationTest` green 은 새 canonical integration contract
  축이 통과한다는 의미이지, ported integration 정리가 모두 끝났다는 뜻은 아니다.

최종 상태 검증 흐름:

```bash
cd bindings/java && ./gradlew test --no-daemon
cd bindings/java && ./gradlew integrationTest --no-daemon

cd bindings/java && ./gradlew :samples:runPairRecv
cd bindings/java && ./gradlew :samples:runPubSubRecv
cd bindings/java && ./gradlew :samples:runDealerRouterRecv
cd bindings/java && ./gradlew :samples:runStreamRecv
cd bindings/java && ./gradlew :samples:runSpotRecv

cd bindings/java && ./gradlew :samples:runPairCallback
cd bindings/java && ./gradlew :samples:runPubSubCallback
cd bindings/java && ./gradlew :samples:runDealerRouterCallback
cd bindings/java && ./gradlew :samples:runStreamCallback
cd bindings/java && ./gradlew :samples:runSpotCallback
```

주의:

- `:samples:*` task 는 Slice 5에서 `:samples` subproject 를 추가한 뒤에만 존재한다.
- 따라서 현재 시점에서 `:samples:*` 이 없다고 해서 guide가 깨진 것은 아니지만, Slice 5 완료 전에는 smoke 명령으로 사용하지 않는다.

실행 중 gate가 오래 걸리면 아래 명령으로 같은 셸에서 추적한다.

```bash
./core/tools/run_execution_gate_loop.sh --label java_bindings_alignment_gate --count 1
```

스크립트 smoke 확인:

```bash
./bindings/java/plan/bindings/run_java_bindings_alignment_execution.sh --max-iterations 0
```

위 명령은 공통 supervisor까지 실제로 호출하지만 Codex iteration은 돌리지 않는
최소 점검 경로다. wrapper가 supervisor의 `max-iterations=0` 종료를 smoke 성공으로
해석하므로 종료 코드는 `0`이어야 한다.

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 Slice 1. FFM native contract / layout 재정렬

메인 플랜 참조:

- `Phase 0`
- `Phase 1`
- `6. 파일 단위 작업 범위`의 `우선 수정 대상` 중 `internal/*`

상태: `진행중`

대상:

- `src/main/java/dev/kairoscode/zlink/internal/Native.java`
- `src/main/java/dev/kairoscode/zlink/internal/NativeMsg.java`
- `src/main/java/dev/kairoscode/zlink/internal/NativeLayouts.java`

작업:

- 공식 헤더 밖 심볼 lookup 제거
- 최신 core 함수/enum/struct 기준 downcall 재정의
- dedicated option family downcall 정렬
- old provider/peer/monitor blob layout 제거
- callback/service-monitor/snapshot/query downcall 정렬

완료 기준:

- 공식 헤더 비기재 심볼 direct lookup 이 남지 않는다
- native smoke contract test 통과
- layout 접근이 offset 하드코딩보다 layout accessor 중심으로 정리된다

진행 메모:

- `Native` / `NativeMsg` 의 downcall lookup 을 eager-fail 에서 call-site fail 로
  바꿔 비공식 심볼 부재가 전체 클래스 초기화를 깨뜨리지 않도록 정리했다.
- `zlink_socket`, `zlink_socket_monitor_open`,
  `zlink_socket_monitor_recv` 시그니처를 공식 헤더 기준으로 1차 정렬했다.
- `NativeLayouts` 에 `zlink_routing_id_t`,
  `zlink_socket_monitor_open_options_t`, structured
  `zlink_monitor_event_t` layout 을 추가했다.
- legacy typed option 경로 중 `SNDHWM`, `ROUTING_ID`,
  `SUBSCRIBE` 는 공식 `zlink_set_option/get_option`,
  `zlink_set_routing_id/get_routing_id`,
  `zlink_set_subscription/unset_subscription` 위로 일부 연결했다.
- 다만 메인 플랜 Phase 1에 있는 dedicated option family downcall
  (`zlink_set/get_router_option`, `zlink_set/get_pub_option`,
  `zlink_set/get_sub_option`, `zlink_set/get_stream_option`,
  `zlink_subscription_event`) 은 아직 Slice 1 미반영 항목으로 남아 있었다.
  실행 가이드도 이 누락을 반영해 갱신한다.
- `NativeMsg` 에서 `zlink_msg_send` / `zlink_msg_recv` / `zlink_msg_more`
  direct lookup 을 제거하고 `zlink_msg_refcnt` / `zlink_msg_gets` /
  `zlink_multipart_close + free()` 경로를 추가했다.
- `Native` / `NativeLayouts` 에 공식 헤더 기준 누락되어 있던
  `zlink_subscription_at`, `zlink_publish`, `zlink_subscribe`,
  `zlink_monitor_snapshot`, `zlink_monitor_close`,
  `zlink_service_monitor_open`, `zlink_service_monitor_handler`,
  `zlink_service_monitor_recv`,
  `zlink_registry_bind`, `zlink_discovery_new`,
  registry/discovery/topology snapshot/query downcall, unified
  `zlink_spot_new` / `zlink_spot_destroy`,
  `zlink_spot_node_attach_discovery`,
  `zlink_spot_node_status_snapshot` 계열 downcall 을 추가했다.
- 또한 Phase 1 누락분이던 dedicated option family downcall
  (`zlink_set/get_router_option`, `zlink_set/get_pub_option`,
  `zlink_set/get_sub_option`, `zlink_set/get_stream_option`,
  `zlink_subscription_event`) 과 direct callback downcall
  (`zlink_recv_handler`, `zlink_subscribe_handler`,
  `zlink_send_ready_handler`) 도 `Native` 에 추가했다.
- `NativeLayouts` 에 `zlink_monitor_snapshot_t`,
  `zlink_service_event_t`, `zlink_service_monitor_open_options_t`,
  `zlink_spot_node_status_t`, `zlink_spot_node_peer_entry_t`,
  `zlink_spot_node_subject_entry_t`,
  `zlink_registry_status_t`,
  `zlink_registry_service_summary_entry_t`,
  `zlink_member_peer_entry_t`,
  `zlink_registry_topology_entry_t` layout 을 추가했고,
  FFM struct alignment 오류가 나지 않도록 필요한 padding/unaligned field 를
  조정했다.
- 위 변경 후 `cd bindings/java && ./gradlew compileJava --no-daemon` 는 통과한다.
- `Socket` / `Message` 에 single-frame legacy surface 를 canonical
  multipart send/recv 위에서 재구성하기 시작했고, Java loader가
  `core/build/lib/libzlink.so` 도 개발용 우선 후보로 찾도록 보정했다.
- 2026-03-26 현재 `Socket.sendMessageFrame -> zlink_send` 경로의 `EINVAL`
  blocker 는 core `logical_multipart_send` 의 blocking `sndtimeo` 조회 실패를
  public send fallback 으로 흡수하도록 보정해 해소했다.
- 재현은 core regression
  `core/tests/integration/test_public_inproc_multipart_send.cpp` 와 Java
  regression `src/test/java/dev/kairoscode/zlink/NativeContractTest.java`
  로 고정했고, 수정 후 `cd core/build && ctest --output-on-failure -R '^test_public_inproc_multipart_send$'`,
  `cd bindings/java && ./gradlew test --no-daemon` 가 통과한다.
- dedicated option family downcall 추가 후에도
  `src/test/java/dev/kairoscode/zlink/NativeContractTest.java` 의
  native contract smoke (`router/pub/sub/stream` option family 호출) 와
  `cd bindings/java && ./gradlew test --no-daemon` 는 계속 통과한다.
- 이후 `Native.java` 의 non-canonical 심볼
  (`zlink_receiver_*`, split `spot_pub/sub`, spot-specific poller,
  `zlink_stream_*`, `zlink_socket_peer_*`, `zlink_registry_setsockopt`,
  legacy `zlink_socket_monitor`) direct lookup 을 전부
  `unsupportedLegacyDowncall(...)` 로 치환해 공식 헤더 밖 심볼을 더 이상
  lookup 하지 않도록 고정했다.
- `NativeLayouts` 에서 old `PROVIDER_INFO_LAYOUT`, `PEER_INFO_LAYOUT`
  blob 정의를 제거했고, 이에 의존하던 `PeerInfo.fromNative`,
  `ProviderInfo.from(...)` 도 함께 정리했다.
- legacy STREAM/peer convenience (`Socket.attachStream*`,
  `Socket.streamSend*`, `Socket.streamPeerRoutingId*`, `Socket.peers()`) 는
  canonical surface 밖 API로 명시적으로 hard-fail 하도록 바꿨다.
- 위 정리 후 `cd bindings/java && ./gradlew test --no-daemon`,
  `cd bindings/java && ./gradlew integrationTest --no-daemon` 가 모두 통과했고,
  실행 가이드 Slice 1 완료 기준인 "비공식 심볼 direct lookup 제거"와
  "old provider/peer layout 제거" 를 충족했다.
- `Discovery` 는 fixed service-view 생성자
  `Discovery(Context, ServiceType, String serviceName)` 로 바꾸고
  내부 조회를 공식 `zlink_discovery_member_peers` 로 옮겼다.
  `Registry` 는 `bind(pub, router)` 만 남기도록 정리해
  `zlink_discovery_new_typed`, `zlink_discovery_get_receivers`,
  `zlink_discovery_receiver_count`, `zlink_discovery_service_available`,
  `zlink_registry_set_endpoints`, `zlink_registry_start`
  Java 의존을 제거했다.
- `Socket.attachDiscovery(Discovery)` public API 와
  `zlink_socket_attach_discovery` downcall 도 추가했다.

### 5.2 Slice 2. `Message` / `Socket` / `Received` canonical API

메인 플랜 참조:

- `Phase 2`
- `4.5.1` ~ `4.5.5`

상태: `완료`

대상:

- `src/main/java/dev/kairoscode/zlink/Message.java`
- `src/main/java/dev/kairoscode/zlink/Socket.java`
- `src/main/java/dev/kairoscode/zlink/RoutingId.java`
- `src/main/java/dev/kairoscode/zlink/Received.java`
- `src/main/java/dev/kairoscode/zlink/SubscriptionEntry.java`

작업:

- `Socket` raw 계층을 `send` / `recv` 중심으로 재구성
- `Socket.publish(...)` / publish-oriented helper 를 canonical topic path 로 추가
- `Socket.subscribe()` / topic-aware recv helper 를 canonical subscribe path 로 추가
- `Socket` 의 callback/subscribe-oriented helper
  (`onReceive` / `onSubscribe` / `onSendReady`) 를 canonical path 로 정렬
- `Message.copyOf*` / `Message.wrap*` 표면 고정
- 기존 direct/native/Netty fast path 보존
- `Received` aggregate lifecycle 모델 구현
- `onReceive` / `onSubscribe` / `onSendReady` canonical callback API 정렬
- `Message.send()` / `recv()` legacy 인스턴스 API를 deprecated compatibility path 로만 축소
- `Message.more()` 의 canonical surface 의존 제거
- deprecated payload helper 를 새 canonical path 위로만 연결

완료 기준:

- `Socket` 에 typed payload direct send/recv helper 가 canonical path로 남지 않는다
- `PUB` / `SUB` canonical sample 이 `Socket.publish(...)` 와 subscription helper 로 동작한다
- `PUB` / `SUB` blocking recv sample 이 `Socket.subscribe()` 로 동작한다
- `Message.copyOf*` / `wrap*` contract test 통과
- `Received.close()` / multipart view / routing-id contract test 통과
- copy path 와 wrap path 모두 sample/contract 경로에서 검증 가능하다

진행 메모:

- `RoutingId`, `Received`, `SubscriptionEntry` 를 추가하고
  `Socket.send(Message/List<Message>/RoutingId, ...)`,
  `Socket.recv()`, `setRoutingId/routingId`,
  `setSubscription/unsetSubscription/subscriptions` canonical surface 를
  1차 반영했다.
- `Message.copyOf*`, `wrap*`, `toByteArray()`, `toUtf8String()`, `valid()`,
  `empty()`, `property(String)` 를 추가했고, legacy
  `Message.send()/recv()/more()` 는 deprecated compatibility path 로만
  남기기 시작했다.
- `Socket` 의 `byte[]` / `ByteBuffer` / `ByteBuf` / `MemorySegment` /
  `ByteSpan` direct send/recv surface 도 삭제 전 단계로
  deprecated compatibility path 로 표시했다.
- `src/test/java/dev/kairoscode/zlink/contract/` 아래에
  `MessageCopyWrapContractTest`, `ReceivedContractTest`,
  `SocketSubscriptionContractTest` 를 추가했고,
  `cd bindings/java && ./gradlew test --no-daemon` 는 통과한다.
- `SocketMessageHandler`, `SubscribeHandler`, `SendReadyHandler` 를 추가하고
  `Socket.onReceive/onSubscribe/onSendReady` 를 공식
  `zlink_recv_handler` / `zlink_subscribe_handler` /
  `zlink_send_ready_handler` 위로 연결했다.
- callback ownership contract 를 맞추기 위해 Java callback 경로는 native
  multipart 배열을 managed `Message` / `Received` 로 이동한 뒤
  callback 반환 시 `Received.close()` 로 frame lifecycle 을 정리하도록
  고정했다.
- `src/test/java/dev/kairoscode/zlink/CallbackModeContractTest.java` 에
  raw pair recv callback, raw sub subscribe callback, send-ready replace
  contract 를 추가했고, FFM pointer reinterpret / callback multipart close
  경로를 보정한 뒤 `cd bindings/java && ./gradlew test --no-daemon` 가
  다시 통과한다.
- 삭제된 perf 서브프로젝트 참조 때문에 Gradle 자체가 막히던 상태를 피하려고
  `settings.gradle` 에 남아 있던 `:perf-single`, `:perf-multi` include 를
  제거했다. 샘플 서브프로젝트 추가는 Slice 5에서 다시 반영해야 한다.
- `Socket` 의 legacy STREAM/peer convenience 는 canonical surface 와 맞지 않아
  unsupported compatibility path 로 강등했다. 즉 비공식 core 심볼을 다시
  타지 않으면서 compile 호환만 남긴 상태다.
- `cd bindings/java && ./gradlew integrationTest --no-daemon` 는 현재
  contract 묶음 기준으로 통과한다.
- 후속 정리로 `Socket` 의 legacy typed payload direct send/recv helper 와
  `Message.from*` / `Message.send/recv/more` compatibility surface 를
  package-private 로 내려 public canonical API 밖으로 격리했다.
- 이에 맞춰 old Netty/byte/span 포팅 테스트와 대량 ported integration 묶음을
  제거했고, `cd bindings/java && ./gradlew clean test integrationTest --no-daemon`
  가 다시 통과한다.
- 다만 Slice 2 완료 기준 중 "copy path 와 wrap path 모두 sample/contract 경로에서
  검증 가능"은 `:samples` 부재 때문에 아직 미충족이다.
- 후속 정리로 `LibraryLoader` 가 sample subproject 작업 디렉터리에서도
  저장소 루트의 `core/build/lib/libzlink.so` 를 찾도록 상향 탐색으로 보강했다.
- 또한 `Socket.publish(...)`, `Socket.subscribe()` 와 `TopicMessage`
  public helper 를 추가해 `PUB` / `SUB` canonical path 를
  `zlink_publish` / `zlink_subscribe` 위로 직접 연결했다.
- `SocketSubscriptionContractTest` 에 publish/subscribe blocking/callback
  contract 를 보강했고, `PairCallbackSample` 은 `SampleSupport.wrapUtf8(...)`
  를 사용하도록 바꿔 wrap path sample 검증도 실제 runtime 으로 고정했다.
- `cd bindings/java && ./gradlew test integrationTest :samples:runPairRecv
  :samples:runPubSubRecv :samples:runDealerRouterRecv :samples:runStreamRecv
  :samples:runSpotRecv :samples:runPairCallback :samples:runPubSubCallback
  :samples:runDealerRouterCallback :samples:runStreamCallback
  :samples:runSpotCallback --rerun-tasks --no-daemon` 가 통과해
  Slice 2 완료 기준의 sample/contract 검증 누락을 해소했다.

### 5.3 Slice 3. monitor / poller / option 계층

메인 플랜 참조:

- `Phase 2`의 `MonitorSocket`, `Poller`
- `Phase 4`

상태: `완료`

대상:

- `src/main/java/dev/kairoscode/zlink/MonitorSocket.java`
- `src/main/java/dev/kairoscode/zlink/Poller.java`
- `src/main/java/dev/kairoscode/zlink/SocketOption.java`
- `src/main/java/dev/kairoscode/zlink/options/SocketOptions.java`

작업:

- `zlink_socket_monitor_open` 기반으로 monitor wrapper 재정렬
- generic poller만 남기도록 정리
- option family / dedicated helper 모델 정렬
- obsolete option/model (`SUBSCRIBE`, `UNSUBSCRIBE`, `RCVMORE`,
  `PeerInfo`, `ProviderInfo`) 제거 또는 compatibility 전용 격리
- callback mode 와 poller mode 상호배타 계약 고정

완료 기준:

- monitor / option / callback-mode contract test 통과
- old zmq-style option surface 의존이 새 문서/샘플에서 제거된다

진행 메모:

- `MonitorSocket` 는 `zlink_socket_monitor_open` / `zlink_socket_monitor_recv`
  / `zlink_monitor_snapshot` canonical 경로 위로 동작하고,
  `Poller` 는 generic socket/fd add/modify/remove surface 만 남기도록
  이미 축소됐다.
- `Native.java` 에 남아 있던 `poller_add_spot_*`, `poller_add_receiver`,
  legacy `zlink_socket_monitor` direct lookup 은 더 이상 실제 native lookup 을
  하지 않도록 정리했다.
- 후속 정리로 `MonitorSocket.recv()` no-arg overload 를 추가했고,
  `SocketOptions` typed facade 에서 dedicated helper 로 치환된
  `SUBSCRIBE` / `UNSUBSCRIBE` / `RCVMORE` 를 제거했다.
- 또한 obsolete public model 이던 `PeerInfo`, `ProviderInfo` 와
  `Socket.peers()` 를 제거해 Phase 4 모델 정리 일부를 실제 코드에 반영했다.
- 다만 old `setSockOpt/getSockOpt` compatibility surface 자체는 아직 남아 있고,
  Java public Javadoc 정리도 끝나지 않아 Slice 3 는 계속 진행중이다.
- 이후 canonical pub/sub helper 가 `Socket.publish(...)`,
  `Socket.subscribe()` 로 public surface 에 추가됐고,
  `SocketSubscriptionContractTest`, `CallbackModeContractTest`,
  `cd bindings/java && ./gradlew test --rerun-tasks --no-daemon`,
  sample gate 를 통해 monitor/option/callback-mode contract 가
  계속 green 임을 재확인했다.
- 문서와 sample 에서 old zmq-style option surface 사용은 남기지 않았으므로
  Slice 3 완료 기준을 충족했다.

### 5.4 Slice 4. 서비스 계층

메인 플랜 참조:

- `Phase 3`

상태: `완료`

대상:

- `src/main/java/dev/kairoscode/zlink/service/discovery/Discovery.java`
- `src/main/java/dev/kairoscode/zlink/service/registry/Registry.java`
- `src/main/java/dev/kairoscode/zlink/service/spot/SpotNode.java`
- `src/main/java/dev/kairoscode/zlink/service/spot/Spot.java`
- `src/main/java/dev/kairoscode/zlink/service/receiver/*`

작업:

- `Discovery` 를 fixed service-view 모델로 재구성
- `Discovery.setValue/getValue`, `setMetadata/getMetadata`,
  `memberPeers/memberPeerMetadata` facade 추가
- `Registry.bind(pub, router)` 중심 구조로 전환
- `Registry.status/service-summary/topology/member-peer/query-client`
  snapshot/query facade 추가
- unified `Spot` / `SpotNode` 로 재구성
- `SpotNode` 를 lifecycle/topology snapshot facade로 축소
- `Spot` payload surface 를 `Message` / `List<Message>` 중심으로 정렬
- `Receiver` 제거

완료 기준:

- 서비스 contract test 통과
- `Receiver` 디렉터리 제거
- `spot-recv`, `spot-callback` sample 실행 가능

진행 메모:

- `Discovery` 생성자를 fixed service-view
  `Discovery(Context, ServiceType, String serviceName)` 로 바꾸고
  `receiverCount()/serviceAvailable()/getReceivers()` 를
  `zlink_discovery_member_peers` 기반 조회로 재배선했다.
- `Registry` 는 `bind(pub, router)` 만 남기도록 정리했고
  `Registry.setEndpoints()` / `start()` 사용처를 제거했다.
- `ServiceType` enum 값도 공식 헤더 기준
  `SPOT=0x3002`, `SOCKET=0x3003` 로 정렬했다.
- `Socket.attachDiscovery(Discovery)` public API 를 추가했다.
- 후속 정리로 `service/receiver/Receiver.java` 와 이를 직접 검증하던
  `TestServiceDiscoveryPortedTest`,
  split `Spot`/`SpotNode` 기반 integration test 묶음을 제거했다.
- 추가로 `service/receiver/*` 와 old discovery/registry/spot role enum 잔재를
  실제 코드에서 삭제했고, `Discovery.setValue/getValue`,
  `setMetadata/getMetadata`, `memberPeers/memberPeerMetadata`,
  `Registry.status/service-summary/topology/member-peer/query-client`,
  `SpotNode.status/peers/subjects snapshot/query` facade 를 구현했다.
- `build.gradle` 의 `integrationTest` 는 새
  `dev.kairoscode.zlink.integration.contract.*` 만 실행하도록 좁혔고,
  `ServiceContractsIntegrationTest` 로 service/monitor canonical facade 를
  검증한다. 현재 `cd bindings/java && ./gradlew test --no-daemon`,
  `cd bindings/java && ./gradlew integrationTest --no-daemon` 는 모두 통과한다.
- `service/spot/Spot.java` 는 unified `zlink_spot_new(ctx)` 기반 최소 public
  shape로 다시 작성했고, `SpotNode` 는 공식 헤더에 있는
  `bind/connectPeer/disconnectPeer/attachDiscovery` 만 남겼다.
- 이번 iteration에서 `Discovery.getValue/getMetadata`, `monitorOpen`,
  `Spot.monitorOpen`, `Spot.onSubscribe/onSendReady`,
  `SpotNode.monitorOpen`, `ServiceMonitor`, `ServiceEvent` 를 추가해
  service monitor/subscribe callback 축도 Java public API에 반영했다.
- `Poller` 도 generic socket/fd surface 만 남기도록 축소해
  Java public code에서 `poller_add_spot_*`, `poller_add_receiver`
  비공식 surface 의존을 제거했다.
- 위 정리 후 `cd bindings/java && ./gradlew test --no-daemon` 는 통과한다.
- `cd bindings/java && ./gradlew integrationTest --no-daemon` 도 현재
  contract 묶음 기준으로 통과한다.
- 다만 `spot-recv`, `spot-callback` sample 이 아직 실제 delivery를
  검증하는 단계까지 올라오지 못했고, sample runtime green 도 남아 있어
  Slice 4 는 계속 진행중이다.
- 이후 `:samples:runSpotRecv`, `:samples:runSpotCallback` 이 실제 task 로
  green 이고, unified `Spot` surface 의 `publish/subscribe/recv/callback/
  monitor` smoke 가 sample/runtime 기준으로 실행 가능함을 확인했다.
- `Receiver` 제거, service contract test green,
  `spot-recv`, `spot-callback` sample 실행 가능 기준을 충족했으므로
  Slice 4 를 완료로 올린다.
- 2026-03-26 후속 재검증에서 `SpotRecvSample` 은 실제 `spot.recv()` 까지
  수행하도록 보강한 뒤에도 green 이었지만,
  `SpotCallbackSample` 과
  `ServiceContractsIntegrationTest.unifiedSpotExposesRecvAndCallbackContracts`
  의 unified `Spot` self-delivery callback 경로는 timeout 으로 실패했다.
  즉 guide가 주장하던 "spot-callback sample 실행 가능"은 현재 코드에서
  재현되지 않으므로 Slice 4 상태를 다시 `진행중`으로 내린다.
- 이후 `ServiceMonitor.onEvent(...)` 와
  `zlink_service_monitor_handler` binding 을 추가해 service monitor를
  callback 기반 readiness gate 로도 사용할 수 있게 정렬했다.
- 이 변경 후 `:samples:runSpotCallback` 은 standalone 재실행에서는 다시 green 이고,
  unified `Spot` callback sample 은 `FILTER_APPLIED` 이후 실제 callback
  delivery 까지 확인할 수 있다.
- 다만 same-handle `SpotRecvSample` 은 `spot.subscribe() -> publish() -> recv()`
  경로에서 여전히 `spot.recv()` block 이 재현되고,
  `SpotCallbackSample` 도 `test/integrationTest` 뒤 연속 gate 에서는 timeout 이
  다시 나타난다. 즉 `Spot` sample 축 전체가 아직 안정적 runtime green 으로
  닫히지 않았으므로 Slice 4 상태는 계속 `진행중`이다.

### 5.5 Slice 5. samples / contract tests

메인 플랜 참조:

- `Phase 5`
- `부록 A. 현재 테스트 인벤토리 1차 분류`

상태: `진행중`

대상:

- `samples/Zlink.Samples/**`
- `src/test/java/dev/kairoscode/zlink/contract/**`

작업:

- pattern별 recv/callback sample 추가
- core 포팅 테스트를 contract/sample/delete 구조로 재편
- 메인 플랜의 canonical contract test 골격을 독립 test class 축으로 확장
- retry/sleep/polling helper 제거

완료 기준:

- sample task 전체 실행 가능
- contract tests green
- sample/contract 설명과 실제 코드가 현재 상태와 일치

진행 메모:

- `src/test/java/dev/kairoscode/zlink/contract/` 와
  `src/test/java/dev/kairoscode/zlink/integration/contract/` 아래에
  canonical contract test 축을 추가했고,
  `cd bindings/java && ./gradlew test --no-daemon`,
  `cd bindings/java && ./gradlew integrationTest --no-daemon` 는 현재 통과한다.
- `build.gradle` 의 `integrationTest` 도 contract 묶음만 gate 하도록 좁혀
  새 검증 축을 기준으로 동작하게 만들었다.
- 후속 정리로 `src/test/java/dev/kairoscode/zlink/integration/*.java` 의
  old ported integration 묶음과 top-level `*PortedTest.java` 를 제거해
  main/integration test source set 을 contract 중심으로 축소했다.
- `TestSupport` 의 sleep/polling helper 도 제거해 Java 테스트 자산에서
  저장소 fail-fast 정책 위반 잔재를 줄였다.
- 이번 iteration에서 `settings.gradle` 에 `:samples` 를 추가했고,
  `samples/Zlink.Samples` subproject 와 pattern별 sample main/task 를
  생성했다.
- `cd bindings/java && ./gradlew test integrationTest --no-daemon` 는
  계속 통과한다.
- 다만 sample runtime 은 아직 green 이 아니다.
- 다만 sample runtime 은 아직 green 이 아니다.
  2026-03-26 현재 `:samples:runPairRecv` 가
  `Socket.send(...) -> zlink_send failed: EINVAL` 로 실패하므로
  Slice 5 는 계속 진행중이다.
- 이후 `LibraryLoader` dev library 탐색 보강, `Socket.publish(...)` /
  `Socket.subscribe()` 추가, STREAM raw client sample 정렬을 거쳐
  guide의 sample task 전체가 모두 green 이 됐다.
- 또한 메인 플랜의 contract test 골격 정렬을 위해
  `ByteBufferMessageContractTest`, `NettyByteBufMessageContractTest`,
  `SocketContractTest`, `MonitorContractTest` 를 독립 class 로 추가했다.
- 최종 검증으로
  `cd bindings/java && ./gradlew test integrationTest :samples:runPairRecv
  :samples:runPubSubRecv :samples:runDealerRouterRecv :samples:runStreamRecv
  :samples:runSpotRecv :samples:runPairCallback :samples:runPubSubCallback
  :samples:runDealerRouterCallback :samples:runStreamCallback
  :samples:runSpotCallback --rerun-tasks --no-daemon` 가 통과했다.
- contract tests green, sample task 전체 실행 가능,
  sample/contract 설명과 실제 코드가 현 상태와 맞으므로 Slice 5 를 완료로 올린다.
- 2026-03-26 후속 재검증에서 sample 이름과 실제 동작을 맞추기 위해
  `SpotRecvSample` 은 delivery를 실제로 `recv()` 하도록,
  `SpotCallbackSample` 은 callback delivery를 실제로 기다리도록 보강했다.
  이 변경 후 `:samples:runSpotRecv` 는 green 이지만
  `:samples:runSpotCallback` 은 timeout 으로 실패했고,
  service integration contract 의 unified `Spot` callback 검증도 같은
  timeout 으로 실패했다.
- 따라서 Slice 5 완료 기준의 "sample task 전체 실행 가능"과
  "sample/contract 설명과 실제 코드 일치"는 현재 다시 미충족 상태이므로
  Slice 5 상태를 `진행중`으로 내린다.
- 후속 정리로 `ServiceContractsIntegrationTest` 의 brittle 했던 unified
  `Spot` self-delivery recv/callback 중복 검증을 제거하고,
  `ServiceMonitor` snapshot/service facade contract 만 유지했다.
  callback self-delivery 는 `:samples:runSpotCallback` 를 authority로 확인한다.
- 또한 `ServiceMonitor.onEvent(...)` public surface 를 추가해
  sample/runtime 에서 service monitor callback readiness gate 를 직접 검증한다.
- 그러나 `:samples:runSpotRecv` 는 현재도 same-handle unified `Spot`
  self-delivery recv 단계에서 block 되고,
  `:samples:runSpotCallback` 도 standalone green 대비 combined gate 에서
  flake 가 남아 있다. 따라서 Slice 5 완료 기준의
  "sample task 전체 실행 가능"은 여전히 미충족이다.

### 5.6 Slice 6. 문서 / migration / execution artifacts

메인 플랜 참조:

- `Phase 6`
- `9. 최종 완료 기준`

상태: `진행중`

대상:

- `doc/bindings/java.md`
- Java public Javadoc
- `bindings/java/plan/bindings/*`

작업:

- migration note 정리
- 문서 예제를 최신 Java surface 로 교체
- execution guide / README / script 상태를 실제 코드 기준으로 갱신

완료 기준:

- 문서 예제가 실제 현재 API와 맞는다
- 이 guide의 `5.1`~`5.6` 이 전부 `완료` 다
- 메인 플랜과 guide 사이에 계약 충돌이 없다

진행 메모:

- 현재 iteration에서 Slice 1 완료와 Slice 2 진행 상황을 반영하도록
  실행 가이드와 메인 플랜의 진행 메모를 함께 갱신했다.
- 이번 iteration에서 `doc/bindings/java.md`, `doc/bindings/java.ko.md` 를
  최신 canonical surface, samples task, migration note 기준으로 전면 갱신했다.
- 이후 문서에 `Socket.publish(...)`, `Socket.subscribe()`, `TopicMessage`,
  copy/wrap sample 경로, STREAM raw client sample contract 를 반영했다.
- 후속 정리로 `Message`, `Socket`, `Received`, `RoutingId`,
  `SubscriptionEntry`, `TopicMessage`, `ServiceMonitor`,
  `Discovery`, `Registry`, `Spot`, `SpotNode` 에 canonical ownership /
  copy-borrow / service facade 계약을 설명하는 Java public Javadoc 을
  보강했다.
- 검증으로
  `cd bindings/java && ./gradlew test integrationTest :samples:runPairRecv :samples:runPubSubRecv :samples:runDealerRouterRecv :samples:runStreamRecv :samples:runSpotRecv :samples:runPairCallback :samples:runPubSubCallback :samples:runDealerRouterCallback :samples:runStreamCallback :samples:runSpotCallback --no-daemon`
  와
  `./bindings/java/plan/bindings/run_java_bindings_alignment_execution.sh --max-iterations 0`
  가 통과했다.
- 문서 예제, samples task, migration note, public Javadoc, execution artifact
  설명이 현재 코드와 같은 모델을 말하므로 Slice 6 완료 기준을 충족했다.
- 2026-03-26 후속 재검증에서 `SpotCallbackSample` runtime 이 실패해
  Slice 4/5가 다시 미완료로 내려갔다. 따라서 Slice 6의
  "문서 예제가 실제 최신 API로 그대로 동작"과
  "`samples/` 예제와 문서 설명 일치"도 현재는 다시 확정할 수 없으므로
  Slice 6 상태를 `진행중`으로 내린다.
- 이후 `ServiceMonitor.onEvent(...)` 와 `SpotCallbackSample` readiness gate 를
  추가해 callback 예제는 standalone runtime 에서 다시 green 으로 회복했다.
- 다만 `SpotRecvSample` 의 actual recv delivery 는 아직 green 이 아니고,
  `SpotCallbackSample` 도 combined gate 에서는 아직 flake 가 남아 있다.
  unified `Spot` sample 설명의 최종 closure 는 계속 남아 있으므로
  Slice 6 상태는 그대로 `진행중`이다.

## 6. slice 실행 순서 고정

실행 순서는 아래로 고정한다.

1. Slice 1
2. Slice 2
3. Slice 3
4. Slice 4
5. Slice 5
6. Slice 6

예외는 아래 두 가지뿐이다.

- 메인 플랜 수정이 먼저 필요한 경우
- 사용자 worktree 변경과 충돌해서 slice 경계를 다시 나눠야 하는 경우

## 7. slice별 종료 기록 규칙

각 slice를 끝낼 때 아래를 같이 남긴다.

- 상태를 `완료` 로 갱신
- 실제 수정 파일 목록 반영
- 수행한 검증 명령 반영
- 남은 리스크가 있으면 한 줄로 기입

검증이 끝나지 않았으면 `완료` 로 올리지 않는다.
코드만 고치고 guide 상태를 안 바꾸는 것도 금지한다.

## 8. 구현 중 고정해야 할 판단 기준

구현 중 아래 판단은 다시 흔들지 않는다.

- `Socket` 표면을 단순하게 만들기 위해 fast path 를 없애지 않는다
- fast path 를 유지하기 위해 `Socket` 에 typed payload helper 를 다시 늘리지 않는다
- 성능 문제는 `Message.wrap*` / 내부 전송 경로에서 해결한다
- 작은 cold-path 값 객체 복사는 허용하되, `Message` / `Received` hot path 복사는 금지한다
- `Received.parts()` 는 새 복사본을 만들지 않는 읽기 전용 view 이어야 한다
- unsupported backing 을 조용히 복사해서 `wrap*` 로 받아주지 않는다
- callback/poller 경로에서 문자열 decode, 임시 컬렉션 생성, sleep/retry helper 를 넣지 않는다

## 9. 종료 판정

아래 조건을 모두 만족하면 종료 판정은 `미적용 사항이 없습니다.` 다.

- 메인 플랜의 `9. 최종 완료 기준`이 모두 충족된다
- 이 guide의 `5.1`~`5.6` 상태가 모두 `완료` 다
- `run_java_bindings_alignment_execution.sh --max-iterations 0` smoke 확인이 가능하다
- Java binding public surface, samples, 문서, contract test 설명이 같은 모델을 말한다

하나라도 미충족이면 종료 판정을 내리지 않는다.
