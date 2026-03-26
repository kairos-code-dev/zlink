# Java Bindings Core 최신 API 정렬 계획

작성일: 2026-03-26
대상: `bindings/java`
기준 소스:
- `core/include/zlink.h`
- `doc/guide/*.md`
- `bindings/java/src/main/java/**`
- `bindings/java/src/test/java/**`

## 1. 목표

`java` 바인딩을 최신 `core`의 공식 공개 표면에 다시 맞춘다. 이번 작업의 기준은 현재 네이티브 라이브러리에 우연히 남아 있을 수 있는 호환 심볼이 아니라, `core/include/zlink.h`와 `doc/guide`에 문서화된 API다.

핵심 목표는 다음과 같다.

- Java FFM 레이어가 공식 헤더에 없는 구 심볼 의존을 제거한다.
- Java 공개 API를 최신 `core`의 소유권/라이프사이클/콜백 모델에 맞게 재설계한다.
- 서비스 계층을 최신 `Discovery`/`Socket Family`/`Spot`/`Registry` 모델로 정렬한다.
- 기존 구현에 이미 있는 성능 경로(`direct ByteBuffer`, native `MemorySegment`, direct Netty `ByteBuf`, scratch buffer`)는 버리지 않고 새 contract 아래로 재배치한다.
- 대량의 core 포팅 테스트를 걷어내고, 사용자-facing `samples/` + 소수의 binding contract test 조합으로 검증 체계를 재편한다.
- 저장소의 fail-fast 정책에 맞지 않는 retry/sleep 기반 테스트와 helper 를 제거한다.

## 2. 현재 상태 요약

### 2.1 가장 큰 구조 문제

현재 Java 바인딩은 최신 `core`를 직접 반영한 표면이 아니라, 구 세대 C API와 호환용 심볼을 전제로 작성되어 있다.

대표 예:

- `Native.java` / `NativeMsg.java`가 `zlink_setsockopt`, `zlink_getsockopt`, `zlink_msg_send`, `zlink_msg_recv`, `zlink_monitor_recv` 같은 공식 헤더 비기재 심볼을 직접 lookup 한다.
- `Receiver` 계열(`zlink_receiver_*`)과 split `spot_pub` / `spot_sub` 계열(`zlink_spot_pub_*`, `zlink_spot_sub_*`)을 Java 공개 API의 중심으로 사용한다.
- `Socket` 이 raw byte send/recv, STREAM attach, peer enumeration, old getsockopt model에 크게 기대고 있다.
- `NativeLayouts` 의 `PROVIDER_INFO_LAYOUT`, `PEER_INFO_LAYOUT`, `MONITOR_EVENT_LAYOUT` 는 최신 구조체 집합과 맞지 않는다.

### 2.2 공식 헤더 기준으로 이미 어긋난 심볼들

공식 헤더에는 없지만 Java가 직접 의존하는 심볼 예시는 다음과 같다.

- 옵션/메시지: `zlink_setsockopt`, `zlink_getsockopt`, `zlink_msg_send`, `zlink_msg_recv`, `zlink_msg_more`
- 모니터링: `zlink_monitor_recv`, `zlink_socket_monitor`
- 서비스 discovery/receiver: `zlink_discovery_new_typed`, `zlink_discovery_get_receivers`, `zlink_receiver_*`
- registry: `zlink_registry_set_endpoints`, `zlink_registry_start`, `zlink_registry_setsockopt`
- spot: `zlink_spot_pub_*`, `zlink_spot_sub_*`, `zlink_spot_node_default_pub/sub`, `zlink_spot_node_register`
- poller: `zlink_poller_add_spot_*`, `zlink_poller_add_receiver`
- stream: `zlink_stream_attach`, `zlink_stream_attach_len32be`, `zlink_stream_send_msg`

현재 Linux 네이티브 라이브러리에서 일부 구 심볼은 남아 있지만, 공식 헤더 표면이 아니므로 Java 바인딩 정렬 작업의 기반으로 삼으면 안 된다.

### 2.3 최신 core가 요구하는 방향

최신 `core`는 다음 방향으로 재편되어 있다.

- 옵션 계층:
  - 공통 옵션은 `zlink_set_option` / `zlink_get_option`
  - 특화 옵션은 `zlink_set_router_option`, `zlink_set_pub_option`, `zlink_set_sub_option`, `zlink_set_stream_option`
  - 라우팅 ID / 구독은 전용 API(`zlink_set_routing_id`, `zlink_get_routing_id`, `zlink_set_subscription`, `zlink_unset_subscription`)
- 메시지 계층:
  - canonical send/recv는 `zlink_send`, `zlink_send_rid`, `zlink_recv`, `zlink_publish`, `zlink_subscribe`
  - raw message frame single-send/single-recv helper가 공개 표면 중심이 아니다
- 콜백/이벤트 계층:
  - `zlink_recv_handler`, `zlink_subscribe_handler`, `zlink_send_ready_handler`
  - `zlink_socket_monitor_open` + `zlink_socket_monitor_handler` + `zlink_socket_monitor_recv` + `zlink_monitor_snapshot`
  - `zlink_service_monitor_open` + `zlink_service_monitor_handler` + `zlink_service_monitor_recv`
- 서비스 계층:
  - `Registry`: bind/config/snapshot/query
  - `Discovery`: `(ctx, service_type, service_name)` 기반 단일 service view
  - raw socket discovery attach: `zlink_socket_attach_discovery`
  - unified `Spot`: `zlink_spot_new`, `zlink_publish`, `zlink_subscribe`, handlers, send-ready, monitor
  - `SpotNode`: topology/lifecycle/snapshot 역할

### 2.4 정책 위반 코드

현재 Java 바인딩과 테스트에는 저장소 정책과 충돌하는 구현이 있다.

- `Discovery.java`, `Receiver.java` 에 retry loop + `Thread.sleep()` 기반 재시도 로직 존재
- 테스트 유틸과 일부 integration test 에 sleep/polling 루프 존재

이번 정렬 작업에는 API 변경뿐 아니라 이 정책 위반 제거도 포함되어야 한다.

## 3. 설계 원칙

이번 작업은 다음 원칙으로 진행한다.

- 공식 헤더 우선:
  - Java는 `core/include/zlink.h` 기준으로만 FFM contract를 정의한다.
- Java 우선:
  - C++식 out parameter, pointer-like utility, shape마다 다른 얕은 wrapper 확산을 피한다.
  - public API는 "행위는 `Socket`, payload와 변환은 `Message`" 원칙으로 설명 가능해야 한다.
- POSD 우선:
  - 오래된 얕은 래퍼를 억지로 유지하지 않는다.
  - 동일 개념을 여러 Java 타입으로 중복 노출하지 않는다.
- 호환 심볼 금지:
  - 네이티브 라이브러리에 남아 있더라도 공식 헤더 밖 심볼은 신규 코드에서 사용하지 않는다.
- 라이프사이클 명확화:
  - close/destroy ownership, callback ownership, message ownership을 Java API 설명만으로 이해 가능해야 한다.
- copy/borrow 명시:
  - payload copy 경로와 zero-copy/borrow 경로는 메서드 이름만 보고 구분 가능해야 한다.
  - 내부 heuristic 으로 복사 여부가 달라지는 API는 canonical surface로 채택하지 않는다.
- hot path 절제:
  - hot path public API에서 `varargs`, `Stream`, `Optional`, 문자열 자동 decode 같은 숨은 allocation을 만들지 않는다.
- 성능 보존:
  - 이미 검증된 direct/native/Netty fast path 는 제거하지 않고 `Message` factory 또는 내부 transport fast path 로 유지한다.
- 검증 우선:
  - 각 단계마다 단위/통합 검증 기준을 둔다.

## 3.1 범위 고정 결정

이번 작업에서 아래 항목은 더 이상 열어두지 않고 고정한다.

- 공식 표면 기준:
  - Java FFM binding은 `core/include/zlink.h` 에 선언된 공개 함수/enum/struct 에만 의존한다.
- `Receiver`:
  - 최신 core 공식 표면과 불일치하므로 유지 대상이 아니다.
  - 이번 정렬 작업에서 `service/receiver/*` 는 deprecated 유지보다 삭제를 기본 방침으로 한다.
- `Spot`:
  - split `spot_pub` / `spot_sub` 모델은 유지하지 않는다.
  - public `Spot` 클래스는 unified `zlink_spot_new` 기반으로 다시 구현한다.
- `Discovery(Context, ServiceType)`:
  - 유지하지 않는다.
  - `serviceName` 없는 discovery view는 최신 모델과 맞지 않으므로 새 생성자만 남긴다.
- `Registry.setEndpoints()` / `start()`:
  - 새 canonical API는 `bind(pub, router)` 다.
  - 기존 메서드는 유지하지 않는다.
  - `bind(pub, router)` 로 일괄 치환한다.
- 테스트 전략:
  - core 테스트 포팅 확대는 중단한다.
  - 새 검증 자산은 `samples/`, contract tests 두 축으로만 추가한다.

## 3.2 비목표

이번 작업의 비목표는 다음과 같다.

- `core/tests` 의 transport/protocol/reconnect matrix 를 Java에서 다시 구현하는 것
- 최신 core에 없는 호환 심볼을 Java에서 계속 노출하기 위한 adapter layer 유지
- 별도 성능 전용 산출물이나 실행 경로 추가를 이번 작업 범위에 넣는 것
- `Receiver` / split `Spot` / old getsockopt 모델을 장기 호환 API로 승격하는 것

## 4. 공개 API 재정렬 방향

### 4.1 유지할 축

- `Context`
- `Socket`
- `Message`
- `Poller`
- `MonitorSocket`
- `Discovery`
- `Registry`
- `SpotNode`
- `Spot`

### 4.2 축소/제거/치환 대상

| 현재 Java 표면 | 문제 | 목표 방향 |
|---|---|---|
| `Receiver` | 최신 core 공식 서비스 모델과 불일치 | `Socket` + `Discovery` attach 모델로 치환 |
| split `spot_pub` / `spot_sub` 기반 `Spot` | unified `zlink_spot_new` 와 불일치 | unified `Spot` handle 로 재작성 |
| `Registry.setEndpoints()` + `start()` | 최신 core는 `bind()` 중심 | `Registry.bind(pub, router)` 로 치환 |
| `Discovery(Context, ServiceType)` | 최신 core는 service name 고정 view | `Discovery(Context, ServiceType, String serviceName)` 로 치환 |
| `Socket.setSockOpt/getSockOpt` old zmq-style | 최신 core는 option family 분리 | 전용 option API + dedicated helper 로 재설계 |
| `Message.send/recv` | 구 공개 심볼 의존 | `Socket` 중심 multipart API 로 이동, `Message`는 frame object 역할에 집중 |
| `Socket` 의 `byte[]` / `String` 직접 send/recv | socket surface 확산 | 변환 책임을 `Message` 로 이동 |

### 4.3 임시 호환 정책

레거시 API를 한 번에 삭제하지 말고 다음 순서로 간다.

1. 최신 core 기반 신규 내부 contract 구축
2. 신규 Java API 추가
3. 기존 API를 신규 API 위에서 재구현 가능한 범위만 `@Deprecated` 로 유지
4. 재구현이 억지인 API는 early removal 후보로 분류

`Receiver` 와 split `spot_pub/sub` 는 억지 호환이 복잡도를 크게 올리므로, 강한 삭제/치환 후보로 본다.

### 4.3.1 실제 호환성 결정

구현 단계에서 아래처럼 처리한다.

- `Receiver`
  - 삭제
  - migration note에서 `Socket + Discovery + socket_attach_discovery` 로 이동 경로 제공
- `Discovery(Context, ServiceType)`
  - 삭제
  - 새 생성자 `Discovery(Context, ServiceType, String serviceName)` 만 제공
- `Registry.setEndpoints()` / `start()`
  - 삭제
  - `bind(pub, router)` 로 일괄 치환
- `Message.send()` / `Message.recv()`
  - deprecated 처리 후 `Socket` 중심 API로 유도
  - 내부 구현은 새 canonical path 로만 유지
- old `setSockOpt/getSockOpt`
  - 최소 호환만 유지
  - 문서와 샘플에서는 사용 금지

### 4.4 테스트 전략

Java 바인딩 테스트는 `core` 동작을 다시 증명하는 대규모 포팅 테스트가 아니라, "Java binding이 최신 C API를 안전하게 노출하는가"만 검증해야 한다.

최종 방향:

- `samples/` 를 새로 만들고 패턴별 `recv` / `callback` 예제를 제공한다.
- `src/test/...` 는 작고 명확한 contract test 집합만 유지한다.
- 성능은 별도 전용 산출물이 아니라 API/구현 계약과 sample 경로에서 관리한다.

남길 테스트:

- native library load / symbol smoke
- `Context` / `Socket` / `Message` lifecycle
- multipart send/recv mapping
- recv mode 와 callback mode 배타성
- option / routing-id / subscription 매핑
- monitor / service-monitor wrapper
- unified `Spot` / `Discovery` / `Registry` 의 얇은 contract
- Java 예외 전파와 ownership 규칙

삭제 대상:

- transport matrix 복제
- reconnect / HWM / protocol corner case 대량 포팅
- 사실상 `core` correctness 를 다시 검증하는 테스트

샘플 후보:

- `samples/pair-recv`
- `samples/pair-callback`
- `samples/pubsub-recv`
- `samples/pubsub-callback`
- `samples/dealer-router-recv`
- `samples/dealer-router-callback`
- `samples/stream-recv`
- `samples/stream-callback`
- `samples/spot-recv`
- `samples/spot-callback`
- 필요 시 `samples/discovery-socket-family`
- 필요 시 `samples/registry-topology`

### 4.5 구현 산출물 고정

이번 작업에서 최종 산출물은 다음으로 고정한다.

- main binding module
  - 최신 core API 정렬된 `Context`, `Socket`, `Message`, `Poller`, `MonitorSocket`, `Discovery`, `Registry`, `SpotNode`, `Spot`
- `samples` subproject
  - 패턴별 `recv` / `callback` runnable example
- main test source set
  - contract test만 유지

### 4.5.1 Java 스타일 API 결정

이번 작업에서 raw socket 계층의 public API는 Java 스타일로 다음 원칙을 따른다.

- `Socket` 은 행위 이름을 `send`, `recv` 로 통일한다.
- `Socket` 은 `Message` 또는 `List<Message>` 만 직접 다룬다.
- `byte[]`, `String`, `ByteBuffer`, Netty `ByteBuf`, `MemorySegment`, `ByteSpan` 변환은 `Message` 가 담당한다.
- topic semantics 는 `publish`, `subscribe` 로 분리한다.
- C++식 out parameter 모델은 쓰지 않는다.
- receive 결과는 단일 Java value object 로 통일한다.
- public API에서는 payload 다건 표현에 배열보다 `List<Message>` 를 우선 사용한다.
- `flags` 는 core contract 표현을 위해 유지하되, Java 사용성은 delegating overload 로 보완한다.
- `varargs` multipart API는 두지 않는다.
- `Received.parts()` 는 recv 순서를 유지하는 읽기 전용 view 만 노출한다.
- hot path 에서 payload 자동 decode / encode 는 하지 않는다.

### 4.5.2 canonical raw socket API 초안

`Socket` 의 canonical API는 아래 수준으로 고정한다.

```java
void send(Message part);
void send(Message part, SendFlag flags);
void send(List<Message> parts);
void send(List<Message> parts, SendFlag flags);
void send(RoutingId rid, Message part);
void send(RoutingId rid, Message part, SendFlag flags);
void send(RoutingId rid, List<Message> parts);
void send(RoutingId rid, List<Message> parts, SendFlag flags);

Received recv();
Received recv(ReceiveFlag flags);

void setRoutingId(RoutingId rid);
RoutingId routingId();

void setSubscription(String filter);
void setSubscription(byte[] filter);
void unsetSubscription(String filter);
void unsetSubscription(byte[] filter);
List<SubscriptionEntry> subscriptions();

void onReceive(SocketMessageHandler handler);
void onSubscribe(SubscribeHandler handler);
void onSendReady(SendReadyHandler handler);

void attachDiscovery(Discovery discovery);
```

설명:

- `send` 는 single-part, multipart, directed-send 를 overload 로 통일한다.
- `recv` 는 항상 `Received` 를 돌려주고, single/multipart 차이는 결과 객체가 품는다.
- `Received` 는 routing id, multipart, single-part fast path helper를 함께 제공한다.
- `publish` / `subscribe` 는 topic/service 계층에서만 사용한다.

### 4.5.3 canonical message API 초안

`Message` 는 payload 변환 책임을 가진다.

```java
Message();
Message(int size);

static Message copyOf(byte[] data);
static Message copyOf(byte[] data, int offset, int length);
static Message copyOfUtf8(String value);
static Message copyOf(ByteBuffer buffer);
static Message copyOf(io.netty.buffer.ByteBuf buf);
static Message copyOf(ByteSpan span);

static Message wrapDirect(ByteBuffer buffer);
static Message wrapNative(MemorySegment segment);
static Message wrapNative(MemorySegment segment, long offset, long length);
static Message wrapDirect(io.netty.buffer.ByteBuf buf);
static Message wrap(ByteSpan span);

byte[] toByteArray();
String toUtf8String();
MemorySegment dataSegment();
ByteBuffer dataBuffer();
void copyTo(byte[] dst, int offset);
void copyTo(ByteBuffer dst);
void copyTo(io.netty.buffer.ByteBuf dst);
int size();
boolean empty();
boolean valid();
int refCount();
String property(String key);
void close();
```

설명:

- `toString()` override 로 payload decode 는 하지 않는다.
- 텍스트 decode 는 `toUtf8String()` 같은 명시적 메서드만 사용한다.
- `copyOf*` 는 항상 복사한다.
- `wrap*` 는 zero-copy/borrow 경로만 담당한다.
- `wrapDirect(ByteBuffer)`, `wrapNative(MemorySegment)`, `wrapDirect(ByteBuf)` 는 기존 구현의 빠른 경로를 유지하는 canonical API다.
- `wrap(ByteSpan)` 은 native-backed span 에서만 허용하고, 그 외에는 예외로 거절한다.
- `ByteBuffer`, Netty `ByteBuf`, `MemorySegment`, `ByteSpan` 은 `Message` 생성 경로로 지원한다.
- `Message` factory 는 입력 `ByteBuffer` position 이나 `ByteBuf` readerIndex/writerIndex 를 암묵적으로 바꾸지 않는다.
- `Socket.recv(byte[])`, `Socket.recv(String)`, `Socket.send(String)` 같은 직접 편의 API는 두지 않는다.
- `Socket.send(ByteBuffer)`, `Socket.recv(ByteBuf)` 같은 typed payload shortcut도 두지 않는다.

### 4.5.4 반환 타입 결정

Java는 out parameter 대신 결과 객체를 쓴다.

```java
final class RoutingId {
    static RoutingId copyOf(byte[] value) {}
    byte[] toByteArray() {}
    ByteBuffer asReadOnlyBuffer() {}
}
record Received(RoutingId routingId, List<Message> parts) implements AutoCloseable {
    boolean hasRoutingId() {}
    boolean isSinglePart() {}
    Message firstPart() {}
    Message singlePartOrThrow() {}
    @Override public void close() {}
}
record SubscriptionEntry(byte[] filter, boolean pattern) {}
```

주의:

- `RoutingId` 는 `String` alias가 아니라 binary-safe value object로 둔다.
- `RoutingId` 는 immutable semantics 를 가져야 한다.
- routing id 는 크기가 작으므로 생성 시 복사를 허용하고, aliasing 위험 제거를 우선한다.
- 문자열 편의성은 별도 factory/helper 에서만 제공한다.
- `Received` 는 자신이 소유한 `Message` frame 집합의 lifecycle aggregate 로 동작해야 한다.
- `Received.parts()` 는 immutable view 로 노출하고 추가 복사를 만들지 않는다.
- `Received` 하나로 raw recv 결과 설명을 끝낼 수 있어야 한다.
- `SubscriptionEntry` 는 cold-path snapshot model 이므로 필요 시 복사를 허용한다.

### 4.5.5 성능 계약

성능 관련 계약은 public surface 단계에서 먼저 고정한다.

- `Socket` 은 얕고 단순해야 하지만, 그 대가로 `Message`/FFM 내부 fast path 를 잃으면 안 된다.
- `Message.copyOf*` 와 `Message.wrap*` 는 의미가 절대 섞이지 않아야 한다.
- `copyOf(byte[])`, `copyOfUtf8(String)` 는 편의 API이면서 복사 비용을 명시적으로 감수하는 경로다.
- direct `ByteBuffer`, native `MemorySegment`, direct Netty `ByteBuf` 는 `wrap*` 를 통해 추가 복사 없이 보낼 수 있어야 한다.
- `Message` factory 는 caller buffer cursor 를 바꾸지 않아야 하므로, side effect 없는 생성 경로로 구현한다.
- `recv()` 는 `Received` 와 `Message` frame 을 반환할 뿐, `byte[]`/`String`/`ByteBuffer` 변환을 자동 수행하지 않는다.
- `Received.parts()` 는 recv 결과를 다시 복사해서 새 배열로 만드는 구현을 금지한다.
- multipart send 는 입력 `List<Message>` 를 재포장하거나 새 컬렉션으로 복사하지 않는다.
- 내부 scratch arena/buffer cache 는 deprecated compatibility path 또는 copy path 최적화에 한해 유지할 수 있다.
- callback path 와 poller path 모두에서 per-message 문자열 decode, 불필요한 boxing, 임시 컬렉션 생성을 금지한다.
- `RoutingId`, `SubscriptionEntry` 같은 cold-path 값 객체는 작은 복사를 허용하되, `Message` / `Received` hot path 에는 같은 원칙을 적용하지 않는다.
- 성능 최적화는 API 복잡도를 키우는 방향이 아니라, copy/borrow 경계를 명확히 하고 내부 구현을 깊게 만드는 방향으로만 허용한다.

### 4.6 샘플 프로젝트 배치 결정

샘플은 별도 Gradle subproject 로 둔다.

- `settings.gradle` 에 `:samples` 추가
- project dir:
  - `bindings/java/samples/Zlink.Samples`
- package:
  - `dev.kairoscode.zlink.samples`
- 실행 방식:
  - `JavaExec` task 로 각 샘플 개별 실행

샘플 클래스 이름 고정:

- `PairRecvSample`
- `PairCallbackSample`
- `PubSubRecvSample`
- `PubSubCallbackSample`
- `DealerRouterRecvSample`
- `DealerRouterCallbackSample`
- `StreamRecvSample`
- `StreamCallbackSample`
- `SpotRecvSample`
- `SpotCallbackSample`
- 선택:
  - `DiscoverySocketFamilySample`
  - `RegistryTopologySample`

### 4.7 contract test 골격 고정

contract test는 새 기준으로 아래 골격으로 정리한다.

- `NativeContractTest`
- `ContextContractTest`
- `MessageContractTest`
- `MessageCopyWrapContractTest`
- `SocketContractTest`
- `ReceivedContractTest`
- `OptionContractTest`
- `CallbackModeContractTest`
- `MonitorContractTest`
- `DiscoveryContractTest`
- `RegistryContractTest`
- `SpotContractTest`
- `SpotNodeContractTest`
- `ErrorPropagationContractTest`
- `ByteBufferMessageContractTest`
- `NettyByteBufMessageContractTest`

각 테스트는 하나의 바인딩 계약만 검증한다. transport matrix, stress, protocol corner case 는 넣지 않는다.
특히 아래 계약은 반드시 독립 test 로 검증한다.

- `copyOf*` 는 항상 복사하고 source cursor 를 바꾸지 않는가
- `wrap*` 는 zero-copy path 만 허용하고 unsupported backing 을 조용히 copy 하지 않는가
- `Received.close()` 가 소유한 frame 을 모두 정리하는가
- multipart `List<Message>` send 와 `Received.parts()` 가 추가 컬렉션 복사를 만들지 않는가

## 5. 단계별 실행 계획

## Phase 0. 기준선 고정 및 갭 매트릭스 작성

목적:
- 무엇을 유지/치환/삭제할지 먼저 고정한다.

작업:
- `core/include/zlink.h` 기준의 함수/enum/struct 목록을 Java 바인딩 표면과 1:1 매핑한다.
- Java가 의존 중인 비공식 심볼 목록을 `deprecated-native-contract` 목록으로 고정한다.
- 최신 `doc/guide` 기준으로 서비스 모델을 확정한다.
- Java public API 변화안을 세 갈래로 분류한다.
  - 유지
  - 호환 유지 후 폐기
  - 즉시 치환

대상 파일:
- `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeMsg.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/**`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/**`

산출물:
- Java-to-core API matrix
- deprecated-native-symbol list
- public API break/change list

완료 기준:
- 이후 구현 단계에서 "어떤 구 API를 살릴지"로 다시 흔들리지 않는 상태

## Phase 1. FFM 네이티브 contract 레이어 재구축

목적:
- Java가 공식 헤더 밖 심볼을 lookup 하지 않게 만든다.

작업:
- `Native.java` 를 최신 헤더 함수만 사용하도록 전면 재정의한다.
- `NativeMsg.java` 에서 다음 의존을 제거한다.
  - `zlink_msg_send`
  - `zlink_msg_recv`
  - `zlink_msg_more`
- canonical multipart path helper를 추가한다.
  - `zlink_send`
  - `zlink_send_rid`
  - `zlink_recv`
  - `zlink_publish`
  - `zlink_subscribe`
  - `zlink_subscription_event`
- option family 전용 downcall 추가:
  - `zlink_set_option` / `zlink_get_option`
  - `zlink_set_router_option` / `zlink_get_router_option`
  - `zlink_set_pub_option` / `zlink_get_pub_option`
  - `zlink_set_sub_option` / `zlink_get_sub_option`
  - `zlink_set_stream_option` / `zlink_get_stream_option`
  - `zlink_set_routing_id` / `zlink_get_routing_id`
  - `zlink_set_subscription` / `zlink_unset_subscription` / `zlink_subscription_at`
- callback/monitor/service-monitor/snapshot/query용 downcall 추가
- `NativeLayouts.java` 를 최신 struct 집합 기준으로 재작성한다.
  - `zlink_routing_id_t`
  - `zlink_monitor_event_t`
  - `zlink_monitor_snapshot_t`
  - `zlink_service_event_t`
  - `zlink_spot_node_status_t`
  - `zlink_spot_node_peer_entry_t`
  - `zlink_spot_node_subject_entry_t`
  - `zlink_registry_status_t`
  - `zlink_registry_service_summary_entry_t`
  - `zlink_registry_topology_entry_t`
  - `zlink_member_peer_entry_t`

리스크:
- FFM descriptor 시그니처가 잘못되면 조용한 메모리 오염이 발생할 수 있다.

대응:
- 각 native downcall에 대해 작은 smoke-level unit test 추가
- struct layout은 byte offset 상수 대신 layout 기반 accessor로 전환

완료 기준:
- Java 바인딩이 공식 헤더 비기재 심볼을 lookup 하지 않는다.
- `Native.java` / `NativeMsg.java` 의 downcall name 목록이 헤더 기준으로 정리된다.
- `NativeLayouts.java` 에 old `PROVIDER_*` / `PEER_*` 전용 byte blob layout 이 남지 않는다.

진행 메모 (2026-03-26):
- `Native` / `NativeMsg` 의 lookup 실패를 call-site fail 로 바꿔 비공식 심볼
  부재가 클래스 초기화 전체를 깨뜨리는 상태는 제거했다.
- `zlink_socket` socket type 값, monitor open/recv 시그니처,
  `zlink_routing_id_t` / monitor layout 1차 정렬을 반영했다.
- `Native` / `NativeLayouts` 에 공식 헤더 기준 누락되어 있던
  `zlink_subscription_at`, `zlink_publish`, `zlink_subscribe`,
  `zlink_monitor_snapshot`, `zlink_monitor_close`,
  `zlink_service_monitor_open`, `zlink_service_monitor_handler`,
  `zlink_service_monitor_recv`,
  `zlink_registry_bind`, fixed-service `zlink_discovery_new`,
  registry/discovery/topology snapshot/query, unified
  `zlink_spot_new` / `zlink_spot_destroy`,
  `zlink_spot_node_attach_discovery`,
  `zlink_spot_node_status_snapshot` 계열 downcall 을 추가했다.
- `NativeLayouts` 에 `zlink_monitor_snapshot_t`,
  `zlink_service_event_t`, `zlink_service_monitor_open_options_t`,
  `zlink_spot_node_status_t`, `zlink_spot_node_peer_entry_t`,
  `zlink_spot_node_subject_entry_t`,
  `zlink_registry_status_t`,
  `zlink_registry_service_summary_entry_t`,
  `zlink_member_peer_entry_t`,
  `zlink_registry_topology_entry_t` layout 을 추가했고,
  FFM struct alignment 오류가 없도록 padding/unaligned field 를 보정했다.
- 위 변경 후 `cd bindings/java && ./gradlew compileJava --no-daemon` 는 통과한다.
- `Socket` typed option 경로 일부는 공식 option/routing/subscription API 위로
  연결했지만, canonical multipart send/recv 와 service query/snapshot downcall 은
  아직 미반영이다.
- 이후 dedicated option family downcall
  (`zlink_set/get_router_option`, `zlink_set/get_pub_option`,
  `zlink_set/get_sub_option`, `zlink_set/get_stream_option`,
  `zlink_subscription_event`) 과 direct callback downcall
  (`zlink_recv_handler`, `zlink_subscribe_handler`,
  `zlink_send_ready_handler`) 도 `Native` 에 추가했고,
  `src/test/java/dev/kairoscode/zlink/NativeContractTest.java` 에
  `router/pub/sub/stream` option family smoke 를 보강한 뒤
  `cd bindings/java && ./gradlew test --no-daemon` 가 계속 통과한다.
- `NativeMsg` 에서 `zlink_msg_send` / `zlink_msg_recv` / `zlink_msg_more`
  direct lookup 을 제거하고 `zlink_msg_refcnt`, `zlink_msg_gets`,
  multipart array close+free 경로를 추가했다.
- `Socket` / `Message` 는 single-frame legacy helper 를 canonical
  `zlink_send` / `zlink_recv` 위로 재배선하기 시작했고,
  `LibraryLoader` 는 `core/build/lib/libzlink.so` 도 개발용 우선 후보로
  찾도록 보정했다.
- 이후 core regression
  `core/tests/integration/test_public_inproc_multipart_send.cpp` 와 Java
  regression `src/test/java/dev/kairoscode/zlink/NativeContractTest.java`
  로 `Socket.sendMessageFrame -> zlink_send` 의 blocking `EINVAL` 를 재현했고,
  `core/src/core/multipart_send_txn.cpp` 에서 blocking `sndtimeo` 조회 실패 시
  public send fallback 을 타도록 보정해 해소했다.
- 2026-03-26 현재 `cd bindings/java && ./gradlew test --no-daemon` 는
  다시 통과한다.
- 이후 `Native.java` 의 non-canonical symbol lookup
  (`zlink_receiver_*`, split `spot_pub/sub`, spot-specific poller,
  `zlink_stream_*`, `zlink_socket_peer_*`, `zlink_registry_setsockopt`,
  legacy `zlink_socket_monitor`) 은 모두 direct lookup 대신
  `unsupportedLegacyDowncall(...)` 로 고정해 공식 헤더 밖 심볼을 더 이상
  찾지 않도록 보정했다.
- `NativeLayouts` 에서 old `PROVIDER_INFO_LAYOUT`, `PEER_INFO_LAYOUT`
  blob 정의를 제거했고, 해당 blob parser 였던 `PeerInfo.fromNative`,
  `ProviderInfo.from(...)` 도 함께 정리했다.
- 위 정리 후 `cd bindings/java && ./gradlew test --no-daemon`,
  `cd bindings/java && ./gradlew integrationTest --no-daemon` 가 모두
  통과하므로, Phase 1 완료 기준의 "비공식 심볼 lookup 제거"와
  "old provider/peer layout 제거" 는 충족했다.

## Phase 2. Core wrapper 재정렬

목적:
- `Context` / `Message` / `Socket` / `Poller` / `MonitorSocket` 를 최신 core 모델에 맞춘다.

### 2.1 `Context`

작업:
- 현 구조 유지
- context option enum 값을 공식 헤더와 다시 대조
- `ZLINK_CTX_OPT_BLOCKY` 포함

### 2.2 `Message`

작업:
- `Message` 는 frame wrapper 역할에 집중시킨다.
- `send/recv` 인스턴스 메서드 중심 구조를 축소하고, `Socket` 의 multipart send/recv를 우선 API로 만든다.
- 추가 노출:
  - `refCount()`
  - `property(String)` for `zlink_msg_gets`
- zero-copy anchor ownership 규칙 문서화
- 기존 지원 타입의 빠른 경로를 유지한다.
  - `byte[]`
  - `String`
  - `ByteBuffer`
  - Netty `ByteBuf`
  - `MemorySegment`
  - `ByteSpan`

결정:

- `more()` 는 제거
  - 최신 canonical multipart API에서는 `partCount` 로 경계를 표현한다.
- `send()` / `recv()` 는 deprecated
  - 신규 샘플/문서/테스트에서는 사용하지 않는다.
- payload 편의성은 `Message` 에만 둔다.
- `toUtf8String()` 은 명시적 decode API로만 제공한다.
- `Message` 생성 API는 `copyOf*` / `wrap*` 이원 구조로 고정한다.
- `ByteBuffer` / Netty `ByteBuf` / native `MemorySegment` 빠른 경로는 제거하지 않고 `Message.wrap*` 로 승격한다.
- `byte[]` / `String` 경로는 편의성은 유지하되 `copyOf*` 로 복사 비용을 명확히 드러낸다.

### 2.3 `Socket`

작업:
- current raw byte send/recv 구현을 multipart canonical path 위로 재작성
- 최소 제공 API:
  - one-part `Message` send/recv convenience
  - multipart `List<Message>` send/recv
  - directed send (`RoutingId` + `Message` / `List<Message>`)
  - publish/subscribe-oriented helper
- dedicated helper 추가:
  - routing id set/get
  - subscription set/unset/list
  - topic-aware publish/subscribe recv helper
  - recv callback attach
  - subscribe callback attach
  - send-ready callback attach
- STREAM API는 구 전용 심볼이 아니라 최신 callback/send path로 다시 설계한다.
  - 기존 `attachStream*` 는 deprecated convenience 로만 유지 가능
- deprecated payload helper 는 내부적으로 `Message.copyOf*` 또는 `Message.wrap*` 로만 연결한다.

결정:

- `Socket` 의 canonical raw send surface 는 `send(...)` overload 로 통일한다.
- `Socket` 의 canonical raw recv surface 는 `recv(...)` 하나로 고정하고 결과는 `Received` 로 통일한다.
- topic-aware `PUB` / `SUB` recv surface 는 `subscribe(...)` helper 로 분리한다.
- `Socket` 에는 `byte[]`, `String`, `ByteBuffer`, `ByteBuf` 직접 send/recv helper 를 두지 않는다.
- `setSockOpt/getSockOpt` 는 compatibility layer 로만 남기고, 새 API는 typed option + dedicated helper 를 사용한다.
- `Socket.attachDiscovery(Discovery)` 를 public API로 추가한다.
- multipart 입력은 `List<Message>` 만 받고 내부에서 `Message[]` 재포장 같은 추가 할당을 하지 않는다.
- callback path 에서는 `Received` / `Message` 전달까지만 하고 decode/복사는 caller 선택으로 남긴다.

진행 메모 (2026-03-26):

- `RoutingId`, `Received`, `SubscriptionEntry` 를 추가했고
  `Socket.send(Message/List<Message>/RoutingId, ...)`, `Socket.recv()`,
  `setRoutingId/routingId`, `setSubscription/unsetSubscription/subscriptions`
  1차 구현을 반영했다.
- `Message` 에 `copyOf*`, `wrap*`, `toByteArray()`, `toUtf8String()`,
  `empty()`, `valid()`, `property(String)` 를 추가했고,
  `copyOf(ByteBuffer)` / `wrapDirect(ByteBuffer)` 가 source cursor 를
  바꾸지 않는 contract test 를 추가했다.
- `Received.parts()` 는 immutable view 로 노출하고,
  `Received.close()` 가 소유한 `Message` 를 정리하는 contract test 도
  추가했다.
- `SocketMessageHandler`, `SubscribeHandler`, `SendReadyHandler` 와
  `Socket.onReceive/onSubscribe/onSendReady` 를 추가해 callback surface 를
  공식 direct handler API 위로 연결했다.
- callback 경로는 native multipart 배열을 managed `Received` 로 이동한 뒤
  callback 반환 시 `Received.close()` 로 lifecycle 을 정리하도록
  ownership contract 를 고정했다.
- `CallbackModeContractTest` 로 pair recv callback, sub subscribe callback,
  send-ready replace contract 를 검증했고,
  `cd bindings/java && ./gradlew test --no-daemon` 는 계속 통과한다.
- legacy STREAM/peer convenience (`attachStream*`, `streamSend*`,
  `streamPeerRoutingId*`, `peers()`) 는 canonical socket API와 맞지 않아
  unsupported compatibility path 로 강등했다. compile 호환은 유지하지만
  더 이상 비공식 native contract 를 타지 않는다.
- 후속 정리로 raw `Socket` 의 `byte[]` / `ByteBuffer` / `ByteBuf` /
  `MemorySegment` direct send/recv helper 와 `Message.from*`,
  `Message.send/recv/more` compatibility surface 를 package-private 로 내려
  public canonical API 밖으로 격리했다.
- 이 변경에 맞춰 top-level ported core tests 와 old integration 묶음을
  제거했고, `cd bindings/java && ./gradlew clean test integrationTest --no-daemon`
  는 통과한다.
- `MonitorSocket.recv()` no-arg overload 도 추가해 monitor recv surface 를
  `recv()` / `recv(flag)` 형태로 정리했다.
- 이후 `LibraryLoader` 를 상향 탐색으로 보강해 sample subproject 작업
  디렉터리에서도 저장소 루트의 `core/build/lib/libzlink.so` 를 우선 찾게 했다.
- 또한 `Socket.publish(...)`, `Socket.subscribe()` 와 `TopicMessage` 를
  추가해 raw `PUB` / `SUB` 의 topic-aware send/recv surface 를
  `zlink_publish` / `zlink_subscribe` 위로 정렬했다.
- `SocketSubscriptionContractTest` 에 publish/subscribe blocking/callback
  contract 를 추가했고, `PairCallbackSample` 은
  `SampleSupport.wrapUtf8(...)` 를 사용하도록 바꿔 wrap path sample 검증도
  runtime 으로 고정했다.
- `cd bindings/java && ./gradlew test integrationTest :samples:runPairRecv
  :samples:runPubSubRecv :samples:runDealerRouterRecv :samples:runStreamRecv
  :samples:runSpotRecv :samples:runPairCallback :samples:runPubSubCallback
  :samples:runDealerRouterCallback :samples:runStreamCallback
  :samples:runSpotCallback --rerun-tasks --no-daemon` 가 통과해
  `samples/` runtime green 과 copy/wrap sample 검증 누락도 해소했다.

### 2.4 `MonitorSocket`

작업:
- `zlink_socket_monitor_open` 기반으로만 생성
- event recv, callback handler, snapshot API 지원
- event mask enum 재정렬

### 2.5 `Poller`

작업:
- generic `zlink_poller_add/modify/remove` 모델로 단순화
- `spot`/`receiver` 전용 add/remove API 삭제
- callback mode 와 poller mode 의 상호배타 제약을 API 문서로 명시

완료 기준:
- core-level Java wrapper가 최신 `doc/guide/02-core-api.md`, `06-monitoring.md`, `12-socket-options.md` 의 개념 모델과 맞는다.

## Phase 3. 서비스 계층 재설계

목적:
- 가장 큰 변경 증폭 지점인 서비스 API를 최신 core 서비스 구조에 맞춘다.

### 3.1 `Registry`

작업:
- `bind(pubEndpoint, routerEndpoint)` 를 canonical API로 추가
- `setEndpoints()` + `start()` 삭제
- `bind(pubEndpoint, routerEndpoint)` 중심으로 재작성
- status/summary/topology snapshot/query wrapper 추가
- remote query client wrapper 추가
- old role-based setsockopt model 삭제

추가 대상:
- `statusSnapshot()`
- `serviceSummarySnapshot(...)`
- `topologySnapshot()`
- `topologyQuery(...)`
- `memberPeers(...)`
- `memberPeerMetadata(...)`
- `RegistryQueryClient`

### 3.2 `Discovery`

작업:
- 생성자를 최신 core signature 로 변경
  - `(Context ctx, ServiceType type, String serviceName)`
- old `getReceivers()/receiverCount()/serviceAvailable()` 모델 제거
- 대신 최신 discovery view API 추가
  - `setValue/getValue`
  - `setMetadata/getMetadata`
  - `memberPeers()`
  - `memberPeerMetadata(...)`
- raw socket attach use case 를 `Socket` 와 연결

결정:

- `ProviderInfo` 기반 API는 제거
- discovery role enum 은 제거 또는 internal 전용으로 축소

진행 메모 (2026-03-26):

- `Discovery(Context, ServiceType, String serviceName)` 생성자와
  fixed-view `memberPeers` 기반 조회로 1차 전환했다.
- `Registry.bind(pub, router)` 와 `ServiceType` 헤더 상수 정렬도
  함께 반영했지만, integration 경로는 아직 `Receiver` 에 묶여 있어
  `Socket.attachDiscovery(...)` 기반 치환 전까지 Phase 3 완료는 아니다.
- `Socket.attachDiscovery(Discovery)` public API 연결점도 추가했다.
- 이후 `service/receiver/Receiver.java` 와 그에 직접 묶인 integration test 를
  제거해 `Receiver` 삭제 방침을 실제 코드에 반영하기 시작했다.
- `service/spot/Spot.java` 는 split `spot_pub/sub` 대신
  unified `zlink_spot_new(ctx)` + `zlink_publish` + `zlink_subscribe`
  기반 최소 facade 로 다시 작성했고,
  `SpotNode` 도 공식 `bind/connect_peer/disconnect_peer/attach_discovery`
  만 남기는 쪽으로 축소했다.
- 후속 정리로 `Discovery.getValue/getMetadata`, `monitorOpen`,
  `Spot.monitorOpen`, `Spot.onSubscribe/onSendReady`,
  `SpotNode.monitorOpen`, `ServiceMonitor`, `ServiceEvent` 도 추가해
  service monitor / subscribe callback 축을 Java public API에 반영했다.
- `Poller` 역시 generic socket/fd 모델만 남기도록 정리해 Java public code에서
  `poller_add_spot_*`, `poller_add_receiver` 의존을 제거했다.
- 또한 obsolete public model 이던 `PeerInfo`, `ProviderInfo` 와
  `Socket.peers()` 를 제거했고, typed option facade 에서 dedicated helper 로
  대체된 `SUBSCRIBE` / `UNSUBSCRIBE` / `RCVMORE` 를 정리했다.
- 정리 후 `cd bindings/java && ./gradlew test integrationTest --no-daemon` 는
  통과한다.
- 다만 Spot sample runtime green 과 Java public Javadoc 정리까지는 아직 남아 있어
  Phase 3/4 완료 기준은 아직 충족하지 못한다.

### 3.3 `Receiver` 제거/치환

판단:
- `Receiver` 는 최신 core 공식 표면과 가장 크게 어긋난 타입이다.
- 유지하면 Java 쪽에서만 별도 서비스 모델을 계속 떠안게 된다.

계획:
- 이번 정렬 범위에서 public API에서 제거한다.
- 대체 경로는 아래로 고정한다.
  - `Socket` + `Discovery`
  - `socket.attachDiscovery(...)`
  - socket family role match helper

완료 기준:

- `bindings/java/src/main/java/dev/kairoscode/zlink/service/receiver/*` 가 삭제된다.
- 관련 문서/테스트/샘플이 모두 새 경로로 이동한다.

### 3.4 `SpotNode`

작업:
- 역할을 topology/lifecycle wrapper로 축소
- 유지 API:
  - `bind`
  - `connectPeer`
  - `disconnectPeer`
  - `attachDiscovery`
  - status/peer/subject snapshot/query
- 제거 후보:
  - `defaultPub/defaultSub`
  - `register/unregister`
  - split pub/sub socket option setters

결정:

- 위 제거 후보는 모두 삭제 방향으로 고정한다.
- `SpotNode` 는 topology/snapshot/lifecycle facade 만 담당한다.

### 3.5 `Spot`

작업:
- split `spot_pub_new` / `spot_sub_new` 기반 구현을 버리고 unified `zlink_spot_new` 기반으로 재작성
- generic service subject API 위에서 제공
  - `publish`
  - `subscribe`
  - `recv`
  - subscribe handler
  - send-ready handler
  - service monitor
- low-copy helper (`PreparedTopic`, reusable contexts) 는 새 canonical path 위로 재구성

결정:

- public `Spot` 생성자는 내부에서 `zlink_spot_new` 하나만 소유한다.
- split pub/sub native handle 을 public contract 로 노출하지 않는다.
- 새 `Spot` 은 recv mode, subscribe callback mode, send-ready callback, service monitor 를 모두 같은 handle 위에서 제공한다.
- `Spot` 의 payload 역시 `Message` / `List<Message>` 기반으로만 주고받는다.
- topic payload에 대한 `byte[]` / `String` / `ByteBuffer` / `ByteBuf` 편의성은 `Message` 변환 경로로만 제공한다.

완료 기준:
- Java 서비스 계층이 최신 `doc/guide/07-0-services.md`, `07-1-discovery.md`, `07-3-spot.md`, `07-4-registry.md` 와 개념적으로 일치한다.

## Phase 4. enum / option / 모델 타입 정리

목적:
- 숫자 상수와 모델 타입의 드리프트를 제거한다.

작업:
- `SocketOption`, `ContextOption`, `ServiceType`, `MonitorEventType`, `DisconnectReason`, `ProtocolError` 재검증
- old option 제거:
  - `SUBSCRIBE`
  - `UNSUBSCRIBE`
  - `RCVMORE`
  - 그 외 dedicated API로 이동한 항목
- ambiguous option key 제거 또는 명시적 dedicated helper로 분리
- stale model 교체:
  - `ProviderInfo`
  - `ReceiverInfo`
  - old `PeerInfo`
- 새 모델 추가:
  - `RoutingId`
  - `MonitorSnapshot`
  - `ServiceEvent`
  - `DiscoveryMemberPeer`
  - `RegistryTopologyEntry`
  - `RegistryStatus`
  - `SpotNodeStatus`
  - `SpotNodePeerEntry`
  - `SpotNodeSubjectEntry`

완료 기준:
- Java enum/model 이 공식 헤더에 없는 개념을 독자적으로 유지하지 않는다.
- `SocketOption` 의 old zmq-style 숫자값과 최신 `zlink_option_t` / specialized option enum 값이 일치한다.
- obsolete enum/model 이 main public package 에 남지 않는다.

## Phase 5. 테스트 재정렬

목적:
- 대량 core 포팅 테스트를 걷어내고, 샘플 + contract test 구조로 바꾼다.

작업:
- 기존 `src/test/...` 를 네 그룹으로 분류
  - 유지할 contract test
  - 최신 API 기준으로 축소 수정할 test
  - sample 로 이동할 사용 예제성 test
  - 완전히 삭제할 core 재검증 test
- `samples/` 디렉터리 신설
- `:samples` Gradle subproject 추가
- 패턴별 `recv` / `callback` 샘플 추가
  - pair
  - pubsub
  - dealer/router
  - stream
  - spot
- contract test 재구성
  - native symbol smoke
  - lifecycle / close contract
  - message copy/wrap semantics
  - ByteBuffer/ByteBuf cursor non-mutation
  - multipart mapping
  - send_rid / routing id
  - subscription API
  - recv/subscription/send-ready callback
  - received aggregate close contract
  - socket monitor snapshot
  - service monitor event
  - discovery member peers / metadata
  - registry topology snapshot/query
  - unified spot publish/subscribe
  - spot node snapshot/query

contract test 소스 구조 결정:

- `src/test/java/dev/kairoscode/zlink/contract/**`
- 기존 top-level/core/integration 테스트는 이 구조로 재배치하거나 삭제

sample 실행 task 결정:

- `:samples:runPairRecv`
- `:samples:runPairCallback`
- `:samples:runPubSubRecv`
- `:samples:runPubSubCallback`
- `:samples:runDealerRouterRecv`
- `:samples:runDealerRouterCallback`
- `:samples:runStreamRecv`
- `:samples:runStreamCallback`
- `:samples:runSpotRecv`
- `:samples:runSpotCallback`

sample 구현 규칙:

- pair/dealer-router/stream raw sample 은 `Socket.send(...)`, `Socket.recv()` 만 사용한다.
- pubsub raw sample 은 `Socket.publish(...)`, `Socket.subscribe()` 또는 `onSubscribe(...)` 만 사용한다.
- payload 생성은 `Message.copyOf*` 또는 `Message.wrap*` 만 사용한다.
- sample 에서 `Socket.send(String)` 같은 직접 편의 API는 사용하지 않는다.
- sample 문서는 copy path 와 wrap path 를 둘 다 보여줘야 한다.

정책 정렬:
- `retryTransient()` 삭제
- `Thread.sleep()` 기반 재시도 삭제
- 테스트 유틸의 polling loop 정리
- deterministic sync 로 교체
  - monitor event wait
  - poller wait
  - queue/latch/condition with hard timeout

검증 명령:
- `cd bindings/java && ./gradlew test --no-daemon`
- `cd bindings/java && ./gradlew integrationTest --no-daemon`
- `cd bindings/java && ./gradlew :samples:runPairRecv`
- `cd bindings/java && ./gradlew :samples:runPubSubRecv`
- `cd bindings/java && ./gradlew :samples:runDealerRouterRecv`
- `cd bindings/java && ./gradlew :samples:runStreamRecv`
- `cd bindings/java && ./gradlew :samples:runSpotRecv`
- `cd bindings/java && ./gradlew :samples:runPairCallback`
- `cd bindings/java && ./gradlew :samples:runPubSubCallback`
- `cd bindings/java && ./gradlew :samples:runDealerRouterCallback`
- `cd bindings/java && ./gradlew :samples:runStreamCallback`
- `cd bindings/java && ./gradlew :samples:runSpotCallback`

진행 메모 (2026-03-26):

- `src/test/java/dev/kairoscode/zlink/integration/*.java` 의 old ported
  integration 묶음과 top-level `*PortedTest.java` 를 제거해 test source set 을
  contract 중심으로 축소했다.
- `TestSupport` 에 남아 있던 sleep/polling helper 도 삭제해 test 자산에서
  저장소 fail-fast 정책 위반 잔재를 정리했다.
- `cd bindings/java && ./gradlew clean test integrationTest --no-daemon` 는
  현재 통과한다.
- 이후 `settings.gradle` 에 `:samples` 를 추가했고,
  `samples/Zlink.Samples` subproject 와
  `runPairRecv/runPairCallback/runPubSubRecv/runPubSubCallback/
  runDealerRouterRecv/runDealerRouterCallback/runStreamRecv/runStreamCallback/
  runSpotRecv/runSpotCallback` task 를 모두 추가했다.
- 이후 `LibraryLoader` dev library 탐색을 보강하고,
  raw `PUB` / `SUB` 용 `Socket.publish(...)` / `Socket.subscribe()` 를
  추가했으며, STREAM sample 은 server-only 계약에 맞게 raw TCP client 로
  재작성했다.
- contract 골격 분리를 위해
  `ByteBufferMessageContractTest`, `NettyByteBufMessageContractTest`,
  `SocketContractTest`, `MonitorContractTest` 도 독립 class 로 추가했다.
- 최종적으로
  `cd bindings/java && ./gradlew test integrationTest :samples:runPairRecv
  :samples:runPubSubRecv :samples:runDealerRouterRecv :samples:runStreamRecv
  :samples:runSpotRecv :samples:runPairCallback :samples:runPubSubCallback
  :samples:runDealerRouterCallback :samples:runStreamCallback
  :samples:runSpotCallback --rerun-tasks --no-daemon` 가 통과해
  Phase 5 완료 기준의 sample runtime green 을 충족했다.
- 2026-03-26 후속 재검증에서 `SpotRecvSample` 은 실제 `spot.recv()` 까지
  수행하도록 보강한 뒤에도 green 이었지만,
  `SpotCallbackSample` 을 실제 callback delivery 대기 방식으로 보강하자
  timeout 으로 실패했다. 같은 현상은
  `ServiceContractsIntegrationTest.unifiedSpotExposesRecvAndCallbackContracts`
  의 unified `Spot` self-delivery callback 검증에서도 재현된다.
- 따라서 Phase 5 진행 메모의 sample runtime green 중
  `spot-callback` 축은 현재 다시 미충족이며, guide와 함께 재정렬한 뒤
  코드 보강이 더 필요하다.
- 후속 보강으로 `ServiceMonitor.onEvent(...)` public API 와
  `zlink_service_monitor_handler` downcall 을 추가해 service monitor readiness 를
  callback 기반으로도 검증 가능하게 했다.
- 이 변경 후 `SpotCallbackSample` 은 standalone runtime 에서는 다시 green 이고,
  unified `Spot` callback self-delivery 는 sample task 단독 실행 기준으로는
  확인된다.
- 반면 same-handle `SpotRecvSample` 의 `spot.recv()` self-delivery 는 현재도
  block 되고, `SpotCallbackSample` 도 `test/integrationTest` 뒤 연속 gate 에서는
  timeout flake 가 남아 있다. 따라서 Phase 5의 `spot` sample runtime green 은
  아직 전체적으로 미충족이다.

완료 기준:
- 대량 core 포팅 테스트가 제거되거나 sample/contract test 로 재분류됨
- `samples/` 가 패턴별 `recv` / `callback` 사용 예제를 제공함
- Java contract tests 가 최신 API 기준으로 통과
- 저장소 fail-fast 정책 위반 없음
- sample task 가 모두 실행 가능하다.

## Phase 6. 문서/예제/마이그레이션 가이드 정리

목적:
- 코드와 문서의 불일치를 제거한다.

작업:
- `doc/bindings/java.md` 갱신
- Java public Javadoc 갱신
- 새 `samples/` 사용법 문서화
- 레거시 사용자용 migration note 작성
  - `Receiver` -> `Socket + Discovery attach`
  - split `Spot` -> unified `Spot`
  - `setSockOpt/getSockOpt` -> dedicated API + option family
  - single-frame `Message.send/recv` -> `Socket.send/recv` + `Received` 중심 API

완료 기준:
- 문서 예제가 실제 최신 API로 그대로 동작한다.
- `samples/` 예제와 문서 설명이 일치한다.

진행 메모 (2026-03-26):

- `doc/bindings/java.md`, `doc/bindings/java.ko.md` 를 최신 canonical
  `Socket` / `Message` / `Received` / service API 기준으로 전면 교체했고,
  `:samples` task 및 migration note 도 함께 정리했다.
- 이후 문서에 `Socket.publish(...)`, `Socket.subscribe()`, `TopicMessage`,
  copy/wrap sample 사용처, STREAM raw TCP client contract 도 반영했다.
- 후속 정리로 `Message`, `Socket`, `Received`, `RoutingId`,
  `SubscriptionEntry`, `TopicMessage`, `ServiceMonitor`,
  `Discovery`, `Registry`, `Spot`, `SpotNode` 에 canonical 계약 설명 Javadoc 을
  보강해 공개 표면 설명을 코드 옆으로 이동시켰다.
- 최종 검증으로
  `cd bindings/java && ./gradlew test integrationTest :samples:runPairRecv :samples:runPubSubRecv :samples:runDealerRouterRecv :samples:runStreamRecv :samples:runSpotRecv :samples:runPairCallback :samples:runPubSubCallback :samples:runDealerRouterCallback :samples:runStreamCallback :samples:runSpotCallback --no-daemon`
  와
  `./bindings/java/plan/bindings/run_java_bindings_alignment_execution.sh --max-iterations 0`
  가 통과했다.
- 따라서 Phase 6 완료 기준의 "문서 예제가 실제 최신 API로 그대로 동작" 과
  "`samples/` 예제와 문서 설명 일치" 를 충족했고, `9. 최종 완료 기준`
  항목도 모두 만족한다.
- 2026-03-26 후속 재검증에서 `SpotCallbackSample` runtime 이 timeout 으로
  실패해 Phase 5/6 완료 판정을 유지할 수 없게 되었다. 문서와 execution
  artifact는 현 상태를 기준으로 다시 내려 써야 하며, unified `Spot`
  callback 경로가 실제로 green 이 되기 전까지 `9. 최종 완료 기준`도
  다시 미충족으로 본다.

## 6. 파일 단위 작업 범위

### 우선 수정 대상

- `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeMsg.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeLayouts.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/Socket.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/Message.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/MonitorSocket.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/RoutingId.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/Received.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/SubscriptionEntry.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/Poller.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/SocketOption.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/options/SocketOptions.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/discovery/Discovery.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/registry/Registry.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/SpotNode.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/Spot.java`
- `bindings/java/build.gradle`
- `bindings/java/settings.gradle`

### 강한 삭제/축소 후보

- `bindings/java/src/main/java/dev/kairoscode/zlink/service/receiver/Receiver.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/receiver/*`
- old role enums / info records that only support receiver-based model

### 테스트 수정 대상

- `bindings/java/src/test/java/dev/kairoscode/zlink/**`
- `bindings/java/src/test/java/dev/kairoscode/zlink/integration/**`
- 특히 sleep/retry helper 를 가진 `TestSupport.java`

### 신규/재구성 대상

- `bindings/java/samples/Zlink.Samples/**`
- `bindings/java/samples/Zlink.Samples/build.gradle`
- sample runner 또는 sample execution Gradle task
- `bindings/java/src/test/java/dev/kairoscode/zlink/contract/**`

## 7. 리스크와 대응

### 리스크 1. 호환 심볼이 현재는 동작해서 문제를 가린다

영향:
- 지금은 테스트 일부가 통과해도, 헤더/문서와 분리된 상태가 계속 유지된다.

대응:
- 공식 헤더 비기재 심볼 사용을 CI에서 금지한다.

### 리스크 2. 서비스 계층 변경이 공개 API 파괴를 만든다

영향:
- 기존 사용자 코드 수정 필요

대응:
- migration note 제공
- `Message.send/recv`, `attachStream*`, old `setSockOpt/getSockOpt` 수준만 제한적으로 deprecated wrapper 유지
- 얕은 wrapper를 위해 구조 복잡도를 늘리지는 않는다

### 리스크 3. FFM callback/arena 수명 관리 오류

영향:
- crash / UAF / leak

대응:
- callback stub 수명은 handle 수명과 동일하게 관리
- callback attach/detach/close 조합별 테스트 추가

### 리스크 5. Java 스타일 정리 과정에서 기존 fast path 성능이 후퇴한다

영향:
- 표면은 정리되지만 실제 처리량/할당량이 나빠질 수 있다.

대응:
- 기존 direct `ByteBuffer` / native `MemorySegment` / direct Netty `ByteBuf` 경로를 구현 기준선으로 유지
- `copyOf*` / `wrap*` 각각의 비용 모델을 문서와 contract 수준에서 명시
- `Received.parts()` 와 multipart send 경로에서 추가 컬렉션 복사를 금지
- deprecated helper 제거 전후 불필요한 추가 복사가 들어가지 않도록 코드 리뷰 기준에 포함

### 리스크 4. 테스트 정책 위반 제거 시 기존 테스트 대량 수정 필요

영향:
- 단기 비용 증가

대응:
- 먼저 test inventory 를 sample/contract/delete 로 분류
- 그 다음 공통 helper 교체
- 마지막에 개별 test 를 축소 또는 삭제

## 8. 제안 마일스톤

### M1. Native Contract Freeze

- 공식 헤더 기준 symbol/enum/layout 매트릭스 완료
- deprecated-native-symbol 목록 고정

### M2. Core Wrapper Green

- `Native*`, `Context`, `Message`, `Socket`, `MonitorSocket`, `Poller` 정렬 완료
- contract unit tests green
- `copyOf*` / `wrap*` 계약과 direct/native fast path 유지 확인

### M3. Service Model Green

- `Registry`, `Discovery`, unified `Spot`, `SpotNode` 정렬 완료
- `Receiver` 삭제 완료

### M4. Sample/Contract Validation Green

- `samples/` 추가 완료
- core 포팅 성격 test 정리 완료
- retry/sleep 제거
- contract tests green
- 주요 fast path 보존 계약이 코드와 문서에 반영됨

### M5. Docs/Migration Green

- binding docs/Javadoc/migration guide 정리 완료

## 8.1 실제 구현 순서

구현은 아래 순서로 고정한다.

1. `settings.gradle` / `build.gradle` 에 `:samples` 구조 추가
2. `Native.java` / `NativeMsg.java` / `NativeLayouts.java` 재정의
3. `Context` / `Message` / `Socket` / `MonitorSocket` / `Poller` 정렬
4. `RoutingId` / receive result types / subscription result types 추가
5. `Message.copyOf*` / `wrap*` 계약과 `Received` 구현 고정
6. `Registry` / `Discovery` / `SpotNode` / `Spot` 정렬
7. `service/receiver/*` 삭제
8. contract test 재구성
9. `samples` 구현
10. `doc/bindings/java.md` 와 migration note 정리

## 9. 최종 완료 기준

다음 조건을 모두 만족하면 작업 완료로 본다.

- Java 바인딩이 공식 헤더 밖 네이티브 심볼을 더 이상 직접 사용하지 않는다.
- Java 공개 API가 최신 `doc/guide` 서비스/소켓/모니터링 모델과 일치한다.
- `Receiver`, split `Spot`, `Registry.setEndpoints()/start()` 같은 구 모델 의존이 제거된다.
- raw socket API가 `send(...)` / `recv(...)` + `Received` 값 객체로 정리된다.
- `Message.copyOf*` / `wrap*` 로 copy/borrow 경계가 public API에서 명시된다.
- direct/native/Netty fast path 가 유지되고 불필요한 추가 복사 경로가 들어가지 않는다.
- 대량 core 포팅 테스트가 sample/contract 중심 구조로 재편된다.
- contract tests 가 최신 API 기준으로 통과하고 retry/sleep 기반 정책 위반이 제거된다.
- `doc/bindings/java.md`, `samples/`, 실제 코드 예제가 일치한다.

## 부록 B. 구현 착수 전 체크리스트

아래 항목이 모두 `예`가 되면 문서는 실제 구현 착수 기준을 만족한 것으로 본다.

- legacy 삭제 범위가 고정되었는가
  - `Receiver`
  - split `Spot`
  - `Registry.setEndpoints()/start()`
- `samples` 배치 위치와 실행 task 이름이 고정되었는가
- contract test 클래스 골격이 고정되었는가
- FFM downcall 은 공식 헤더 함수만 대상으로 고정되었는가
- `Socket`, `Discovery`, `Registry`, `Spot`, `SpotNode` 의 canonical API 이름과 역할이 고정되었는가
- `Socket` 은 `Message` / `List<Message>` 기반만 다루고, 변환 책임이 `Message` 로 고정되었는가
- raw recv 결과가 `Received` 값 객체 하나로 고정되었는가
- `Message.copyOf*` / `wrap*` 로 copy/borrow 경계가 고정되었는가
- `ByteBuffer`, Netty `ByteBuf`, `MemorySegment`, `ByteSpan` 지원 경로가 `Message` 로 모였는가
- 기존 테스트 인벤토리가 `contract / sample / delete` 로 분류되었는가

현재 문서는 위 항목을 모두 충족하는 버전으로 간주한다.

## 부록 A. 현재 테스트 인벤토리 1차 분류

기준:

- `contract`: Java binding 전용 계약 검증으로 유지 또는 축소 재작성
- `sample`: 테스트 파일은 삭제하고 `samples/` 예제로 이동 또는 대체
- `delete`: core 재검증 성격이 강하므로 제거

이 부록은 실제 구현 순서의 입력 자료다. 분류가 끝난 테스트는 즉시 `contract`, `sample`, `delete` 세 버킷 중 하나로 이동시킨다.

현재 기준 대상:

- 일반 테스트 클래스 `14`개
- integration 테스트 클래스 `52`개
- support/helper 클래스 `2`개

### A.1 유지 또는 축소 재작성할 contract test

일반 테스트:

- `CoreCapabilitiesPortedTest`
- `CoreCtxDestroyPortedTest`
- `CoreCtxOptionsPortedTest`
- `CoreEnumPortedTest`
- `CoreMessageLifecyclePortedTest`
- `CoreMessageSpanPortedTest`
- `CoreNettyByteBufPortedTest`
- `CoreSocketNullPortedTest`
- `CoreSocketOptionsTypedPortedTest`
- `CoreSocketSpanPortedTest`
- `CoreVersionPortedTest`
- `SocketOptionsTypeMapTest`

integration 테스트:

- `TestConnectRidPortedTest`
- `TestConnectRidStringAliasPortedTest`
- `TestLastEndpointPortedTest`
- `TestMonitoringEnhancedPortedTest`
- `TestMultipartPortedTest`
- `TestPollerPortedTest`
- `TestServiceDiscoveryPortedTest`
- `TestSpotDiscoveryPortedTest`
- `TestStreamRoutingIdSizePortedTest`
- `TestTimeoPortedTest`

재작성 방향:

- 최신 `Socket` canonical multipart API 기준으로 축소
- 최신 `Discovery` / `Registry` / unified `Spot` 기준으로 다시 작성
- callback/snapshot/service-monitor 계약 검증을 중심으로 재편

### A.2 `samples/` 로 이동 또는 대체할 테스트

recv 예제로 이동:

- `TestPairInprocPortedTest`
- `TestPairTcpPortedTest`
- `TestPubSubPortedTest`
- `TestDealerRouterPortedTest`
- `TestStreamSocketPortedTest`
- `TestSpotPubsubScenarioPortedTest`
- `TestXpubXsubPortedTest`

새 callback 샘플로 대체:

- 현재 직접 대응 테스트 없음
- 아래 샘플을 신규 추가
  - `pair-callback`
  - `pubsub-callback`
  - `dealer-router-callback`
  - `stream-callback`
  - `spot-callback`

샘플 확장 후보:

- `TestSpotDiscoveryPortedTest` 의 일부 흐름은 `samples/discovery-socket-family` 또는 `samples/spot-recv` 에 흡수 가능
- `TestMonitoringEnhancedPortedTest` 의 일부 흐름은 `samples/monitoring` 예제로 분리 가능

### A.3 삭제 대상 테스트

일반 테스트:

- `CoreSystemPortedTest`
- `TestManySocketsPortedTest`

integration 테스트:

- `TestBindAfterConnectTcpPortedTest`
- `TestBindSrcAddressPortedTest`
- `TestConnectResolvePortedTest`
- `TestDiffservPortedTest`
- `TestDisconnectInprocPortedTest`
- `TestHeartbeatsPortedTest`
- `TestHwmPubSubPortedTest`
- `TestImmediatePortedTest`
- `TestIssue566PortedTest`
- `TestMsgFlagsPortedTest`
- `TestPairSendBlockingWakeupPortedTest`
- `TestProbeRouterPortedTest`
- `TestPubInvertMatchingPortedTest`
- `TestPubSubFilterXpubPortedTest`
- `TestReconnectIvlPortedTest`
- `TestReconnectOptionsPortedTest`
- `TestRouterAutoIdFormatPortedTest`
- `TestRouterHandoverPortedTest`
- `TestRouterMandatoryHwmPortedTest`
- `TestRouterMandatoryPortedTest`
- `TestRouterMultipleDealersPortedTest`
- `TestShutdownStressPortedTest`
- `TestSpecRouterPortedTest`
- `TestSpotSendBlockingWakeupPortedTest`
- `TestSpotServicePollerPortedTest`
- `TestStreamFastpathPortedTest`
- `TestStreamSendBlockingWakeupPortedTest`
- `TestSubForwardPortedTest`
- `TestTransportMatrixPortedTest`
- `TestXpubManualPortedTest`
- `TestXpubNodropPortedTest`
- `TestXpubTopicPortedTest`
- `TestXpubVerbosePortedTest`
- `TestXpubWelcomeMsgPortedTest`
- `TestZmpWsWssPortedTest`

삭제 이유:

- transport/protocol matrix 재검증
- reconnect/HWM/backpressure corner case 재검증
- 특정 core bug regression 직접 포팅
- 성능/부하/타이밍 특성 재검증
- binding contract 보다 core semantics 검증 비중이 큰 경우

### A.4 helper / support 파일 처리

- `TestSupport`
  - 현재 sleep/polling helper 중심 구조는 삭제 또는 전면 축소
  - contract test용 deterministic helper만 남김
- `IntegrationSupport`
  - sample runner 또는 소수 integration contract test에 필요한 최소 기능만 유지

### A.5 실제 정리 순서

1. 현재 테스트 파일에 `contract / sample / delete` 태그를 먼저 매긴다.
2. `delete` 대상을 먼저 제거해 유지비를 줄인다.
3. `sample` 대상을 `samples/` 예제로 옮긴다.
4. `contract` 대상만 최신 API 기준으로 재작성한다.
5. 마지막에 `TestSupport` / `IntegrationSupport` 를 축소 정리한다.

### A.6 참고

이 분류는 1차 초안이다. 실제 구현 중 다음 상황에서는 재분류할 수 있다.

- unified `Spot` 도입 후 기존 테스트의 절반 이상이 자연스럽게 sample 로 흡수되는 경우
- `Discovery` / `Registry` 공개 API를 더 강하게 단순화해 별도 contract test 수가 더 줄어드는 경우
- callback sample이 계약 검증을 일부 겸할 수 있어 contract test 수를 더 줄일 수 있는 경우
