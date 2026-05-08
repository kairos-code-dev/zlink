# Java Socket Surface 상세 설계

작성일: 2026-03-27
대상: `bindings/java`
기준:
- `core/include/zlink.h`
- `doc/guide/02-core-api*.md`
- `doc/guide/03-2-pubsub*.md`
- `doc/guide/03-3-dealer*.md`
- `doc/guide/03-5-stream.md`
- `bindings/java/src/main/java/systems/zlink/Socket.java`

## 1. 목적

이 문서는 `bindings/java`의 raw socket public surface를 재정의하고,
현재 단일 [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Socket.java)
에 몰려 있는 책임을 Java 스타일과 POSD 철학에 맞게 분리하는 상세 계획이다.

목표는 다음 세 가지다.

- 사용자가 socket 타입별로 "무엇을 할 수 있는지"를 클래스 이름만 보고 이해하게 만들 것
- FFM/native handle/callback/buffer fast path 같은 복잡한 구현은 깊은 공통 모듈에 모아 change amplification을 줄일 것
- 기존 구현이 이미 갖고 있는 성능 경로를 잃지 않으면서, compile-time surface 제한을 강화할 것

즉, public class는 타입별 facade로 분리하되, 구현은 다시 흩뿌리지 않는다.

## 2. 현재 상태와 분리 필요성

현재 Java 바인딩의 [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Socket.java)
는 약 2200라인 규모의 giant class다. 한 클래스 안에 다음 책임이 동시에 들어 있다.

- handle ownership / lifecycle
- bind/connect/unbind/disconnect
- option set/get
- monitor open
- discovery attach
- raw `send` / `recv`
- topic `publish` / `subscribe`
- subscription 관리
- receive / subscribe / send-ready callback
- legacy `byte[]` / `ByteBuffer` / `ByteBuf` send/recv bridge
- STREAM legacy helper와 scratch buffer 관리

이 구조는 다음 POSD smell을 만든다.

- change amplification:
  - socket surface 한 군데 수정이 callback, legacy compat, topic, poller, monitor에 같이 번진다.
- hidden coupling:
  - `sendScratch`, `recvScratch`, callback arena, legacy recv state가 unrelated API와 묶여 있다.
- shallow wrapper:
  - generic `SocketType` 생성자 때문에 타입별로 지원되지 않는 동작이 같은 public class에 모두 노출된다.
- temporal decomposition:
  - "먼저 socket 만들고, 나중에 타입을 기억해서 어떤 메서드를 조심해서 써야 하는" 사용 패턴을 강제한다.

현재 구조는 Java의 장점인 class-level semantic restriction을 못 살리고 있다.

## 3. 설계 원칙

- public surface는 Java 타입으로 제한한다.
  - `new Socket(ctx, SocketType.SUB)` 같은 generic 생성보다 `new SubSocket(ctx)`를 우선한다.
- illegal operation은 runtime `ENOTSUP`보다 compile-time surface 제한을 우선한다.
- `Message`는 payload container이며, payload 변환 책임도 `Message`에 둔다.
- raw transport와 topic transport는 class 계층에서 분리한다.
- `send/recv`와 `publish/subscribe`는 같은 class에 같이 두지 않는다.
- callback mode와 pull mode의 배타성은 공통 메커니즘으로 한 곳에서 관리한다.
- direct `ByteBuffer`, native `MemorySegment`, direct Netty `ByteBuf`, scratch buffer 같은 기존 fast path는 제거하지 않는다.
- public API hot path에는 `varargs`, `Stream`, `Optional`, 숨은 문자열 decode를 넣지 않는다.
- public type은 설명 가능한 최소 집합으로 유지하고, 실제 복잡성은 package-private 공통 모듈에 숨긴다.

## 4. native 기준 확정 범위

이번 설계가 전제로 삼는 raw socket type은 최신 `core`가 실제 공개하는 아래 8종뿐이다.

- `PAIR`
- `PUB`
- `SUB`
- `DEALER`
- `ROUTER`
- `XPUB`
- `XSUB`
- `STREAM`

제외 타입:

- `PUSH`
- `PULL`
- `SCATTER`
- `GATHER`
- `REQ`
- `REP`

이유:

- 최신 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)에 없다.
- Java에서만 별도 facade를 부활시키면 shallow wrapper만 늘어난다.

## 5. 최종 public 계층 구조

최종 public 계층은 아래로 고정한다.

```text
Socket (abstract common base)
  ^
  +-- PairSocket
  +-- DealerSocket
  +-- RouterSocket
  +-- StreamSocket
  +-- PubSocket
  +-- SubSocket
  +-- XPubSocket
  +-- XSubSocket

Value types
  - Message
  - Received
  - TopicMessage
  - SubscriptionEvent
  - RoutingId
  - SubscriptionEntry
```

정책:

- `Socket`은 더 이상 generic 생성 class가 아니다.
- `Socket`은 공통 lifecycle/option/monitor/TLS의 abstract base가 된다.
- concrete type은 타입별 허용 API만 연다.
- `Poller`, `SocketPollSet`, `MonitorSocket`은 계속 `Socket` base를 기준으로 동작한다.
- 새 샘플과 새 contract test는 `new Socket(ctx, SocketType.X)`를 사용하지 않는다.
- capability 분류는 public 다중 상속 계층이 아니라, final facade의 thin delegator 메서드와 내부 공통 모듈로 구현한다.
  - Java 단일 상속 제약 때문에 public capability base를 여러 갈래로 여는 방식은 채택하지 않는다.

## 6. 내부 구현 구조

Java에서는 public 상속 계층을 크게 여는 것보다, public base 하나와 package-private
깊은 모듈 조합이 더 적합하다. 최종 내부 구조는 아래로 고정한다.

```text
Socket (public abstract)
  - owns SocketCore

SocketCore (package-private final)
  - handle ownership
  - common bind/connect/unbind/disconnect
  - common option codec
  - monitor open
  - discovery attach
  - callback mode state
  - callback stub / arena ownership
  - scratch buffer ownership
  - close lifecycle

MessagePlane (package-private final)
  - send / recv
  - recv callback
  - routing-id encode/decode
  - message vector ownership recovery

TopicPlane (package-private final)
  - publish / subscribe
  - subscribe callback
  - topic encode/decode
  - subscription list snapshot
  - subscription event receive

LegacySocketCompat (package-private final)
  - deprecated byte[] / ByteBuffer / ByteBuf / ByteSpan bridge
  - only used by legacy compatibility surface
```

핵심은 `SocketCore`가 깊은 모듈이 되고, data-plane은 `MessagePlane`과
`TopicPlane` 두 축으로만 나눈다는 점이다. helper class를 지나치게 많이 쪼개지 않는다.

## 7. 파일 배치

최종 파일 배치는 아래를 목표로 한다.

- `src/main/java/systems/zlink/Socket.java`
- `src/main/java/systems/zlink/PairSocket.java`
- `src/main/java/systems/zlink/DealerSocket.java`
- `src/main/java/systems/zlink/RouterSocket.java`
- `src/main/java/systems/zlink/StreamSocket.java`
- `src/main/java/systems/zlink/PubSocket.java`
- `src/main/java/systems/zlink/SubSocket.java`
- `src/main/java/systems/zlink/XPubSocket.java`
- `src/main/java/systems/zlink/XSubSocket.java`
- `src/main/java/systems/zlink/SubscriptionEntry.java`
- `src/main/java/systems/zlink/SubscriptionEvent.java`
- `src/main/java/systems/zlink/SocketCore.java`
- `src/main/java/systems/zlink/MessagePlane.java`
- `src/main/java/systems/zlink/TopicPlane.java`
- `src/main/java/systems/zlink/LegacySocketCompat.java`

의도:

- public facade와 내부 엔진이 같은 package에 있어 package-private 공유가 가능하다.
- `internal/` 하위 패키지로 내리지 않는다.
  - 같은 package가 아니면 `Poller`, `MonitorSocket`, `SocketPollSet`, `Message`와의 협력이 오히려 복잡해진다.
- endpoint/discovery/data-plane capability는 public base class가 아니라 final facade 메서드 집합으로 드러낸다.
  - 구현 공유는 `SocketCore`, `MessagePlane`, `TopicPlane`이 담당한다.

## 8. 클래스별 책임

### 8.1 `Socket` abstract base

역할:

- 공통 lifecycle
- 공통 option API
- monitor open
- poller/socket-pollset 통합 지점
- common validation

`Socket`에는 다음만 둔다.

- `monitorOpen`
- `setTlsServer`, `setTlsClient`
- common `setOption` / `getOption`
- `close`
- package-private `handle()`

`Socket`에는 두지 않는 것:

- endpoint API
- discovery attach
- raw `send`, `recv`
- topic `publish`, `subscribe`
- subscription 관리
- type-specific option API
- deprecated buffer convenience

즉 `Socket`은 더 이상 "다 되는 소켓"이 아니라 공통 기반이다.

구현 메모:

- 위 항목은 public canonical surface 기준이다.
- typed facade가 얇게 위임할 수 있도록 `Socket` base 안에 package-private delegation
  entrypoint는 남을 수 있지만, 사용자는 이를 직접 보지 않는다.

TLS 정책:

- TLS는 일반 option key로 우회하지 않는다.
- `Socket` base에 dedicated function으로 고정한다.

```java
public abstract class Socket implements AutoCloseable {
    public final void setTlsServer(String certPem, String keyPem,
                                   boolean requireClientCert);
    public final void setTlsClient(String caCertPem, String hostname,
                                   boolean trustSystem);
}
```

### 8.2 `PairSocket`

역할:

- 가장 단순한 raw message transport facade

공개 API:

```java
public final class PairSocket extends Socket {
    public PairSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);

    public void send(Message part);
    public void send(Message part, SendFlag flags);
    public void send(List<Message> parts);
    public void send(List<Message> parts, SendFlag flags);

    public Received recv();
    public Received recv(ReceiveFlag flags);

    public void onReceive(SocketMessageHandler handler);
    public void onSendReady(SendReadyHandler handler);
}
```

제약:

- routing-id API 없음
- topic API 없음

### 8.3 `DealerSocket`

역할:

- raw message transport
- self routing-id 관리

공개 API:

```java
public final class DealerSocket extends Socket {
    public DealerSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);
    public void attachDiscovery(Discovery discovery);

    public void send(Message part);
    public void send(Message part, SendFlag flags);
    public void send(List<Message> parts);
    public void send(List<Message> parts, SendFlag flags);

    public Received recv();
    public Received recv(ReceiveFlag flags);

    public void setRoutingId(RoutingId routingId);
    public RoutingId routingId();

    public void onReceive(SocketMessageHandler handler);
    public void onSendReady(SendReadyHandler handler);
}
```

정책:

- dealer는 directed send API를 public으로 열지 않는다.
- dealer-specific option이 native set-only이면 Java도 set-only로 둔다.
- `setRoutingId()`는 `connect()` 전에 호출해야 한다는 lifecycle 제약을 Javadoc에 명시한다.

### 8.4 `RouterSocket`

역할:

- routing-aware raw transport
- directed send

공개 API:

```java
public final class RouterSocket extends Socket {
    public RouterSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);
    public void attachDiscovery(Discovery discovery);

    public void send(RoutingId routingId, Message part);
    public void send(RoutingId routingId, Message part, SendFlag flags);
    public void send(RoutingId routingId, List<Message> parts);
    public void send(RoutingId routingId, List<Message> parts, SendFlag flags);

    public Received recv();
    public Received recv(ReceiveFlag flags);

    public void setRoutingId(RoutingId routingId);
    public RoutingId routingId();

    public void onReceive(SocketMessageHandler handler);
    public void onSendReady(SendReadyHandler handler);
}
```

정책:

- `Received.routingId()`는 router에서 핵심 의미를 가진다.
- routing-id를 out parameter로 받는 C++식 모델은 쓰지 않는다.
- ROUTER 자신의 routing-id도 core 문서에 존재하므로 Java facade에 노출한다.

### 8.5 `StreamSocket`

역할:

- STREAM raw transport facade
- routing-aware send/recv

공개 API:

```java
public final class StreamSocket extends Socket {
    public StreamSocket(Context ctx);

    public void bind(String endpoint);
    public void unbind(String endpoint);

    public void send(RoutingId routingId, Message part);
    public void send(RoutingId routingId, Message part, SendFlag flags);
    public void send(RoutingId routingId, List<Message> parts);
    public void send(RoutingId routingId, List<Message> parts, SendFlag flags);

    public Received recv();
    public Received recv(ReceiveFlag flags);

    public void onReceive(SocketMessageHandler handler);
    public void onSendReady(SendReadyHandler handler);
}
```

정책:

- old STREAM attach helper는 canonical surface에서 제거한다.
- STREAM도 `recv_handler` / `send_rid` 기반 surface만 유지한다.
- `connect()` / `disconnect()` / non-directed `send()`는 public으로 열지 않는다.
- 구현 완료 메모:
  - 2026-03-27 기준 legacy `attachStream*` / `streamSend*` / `streamPeerRoutingId*`
    계열 public 메서드는 `Socket` base에서 제거되어 typed facade 상속으로 다시 새지 않는다.

### 8.6 `PubSocket`

역할:

- topic publish facade

공개 API:

```java
public final class PubSocket extends Socket {
    public PubSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);
    public void attachDiscovery(Discovery discovery);

    public void publish(String topicId, Message part);
    public void publish(String topicId, Message part, SendFlag flags);
    public void publish(String topicId, List<Message> parts);
    public void publish(String topicId, List<Message> parts, SendFlag flags);

    public void onSendReady(SendReadyHandler handler);
}
```

제약:

- raw `send`, `recv` 없음
- subscription 관리 없음

### 8.7 `SubSocket`

역할:

- topic receive facade
- subscription 관리

공개 API:

```java
public final class SubSocket extends Socket {
    public SubSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);
    public void attachDiscovery(Discovery discovery);

    public TopicMessage subscribe();
    public TopicMessage subscribe(ReceiveFlag flags);

    public void setSubscription(String filter);
    public void unsetSubscription(String filter);
    public List<SubscriptionEntry> subscriptions();

    public void onSubscribe(SubscribeHandler handler);
}
```

정책:

- `recv()` 대신 `subscribe()`를 유지한다.
  - Java binding에서 raw transport와 topic transport 이름을 분리하기 위함이다.
- `onSendReady()`는 SUB에 두지 않는다.
- canonical filter type은 `String`으로 고정한다.

### 8.8 `XSubSocket`

역할:

- `SubSocket`과 같은 topic receive facade
- local filtering이 아니라 pass-through proxy 용도

공개 API:

```java
public final class XSubSocket extends Socket {
    public XSubSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);

    public TopicMessage subscribe();
    public TopicMessage subscribe(ReceiveFlag flags);

    public void setSubscription(String filter);
    public void unsetSubscription(String filter);
    public List<SubscriptionEntry> subscriptions();

    public void onSubscribe(SubscribeHandler handler);
}
```

정책:

- surface는 같되 semantics는 Javadoc으로 설명한다.
- `XSUB`도 raw `send`를 public으로 열지 않는다.

### 8.9 `XPubSocket`

역할:

- topic publish
- subscription event 수신

공개 API:

```java
public final class XPubSocket extends Socket {
    public XPubSocket(Context ctx);

    public void bind(String endpoint);
    public void connect(String endpoint);
    public void unbind(String endpoint);
    public void disconnect(String endpoint);

    public void publish(String topicId, Message part);
    public void publish(String topicId, Message part, SendFlag flags);
    public void publish(String topicId, List<Message> parts);
    public void publish(String topicId, List<Message> parts, SendFlag flags);

    public SubscriptionEvent subscriptionEvent();
    public SubscriptionEvent subscriptionEvent(ReceiveFlag flags);

    public void onSendReady(SendReadyHandler handler);
}
```

정책:

- `XPUB` subscription frame 수신은 `subscriptionEvent()`로 분리한다.
- `subscribe()`라는 이름은 `SUB`/`XSUB` data-plane receive에만 쓴다.
- 2026-03-27 기준 core `zlink_set_subscription` / `zlink_unset_subscription`는 `SUB` / `XSUB`만 허용하고 `XPUB`에서는 `EINVAL`을 반환하므로 Java `XPubSocket`은 manual mutation facade를 public으로 노출하지 않는다.
- 다만 `subscriptions()` snapshot API는 `SUB`/`XSUB` canonical path에만 둔다.
  - XPUB manual mode의 핵심은 current-pipe mutation이지 local subscription snapshot이 아니다.

## 9. 보조 값 객체 규격

### 9.1 `Received`

- raw message recv 결과 aggregate
- optional `RoutingId`
- owned `Message` list

유지 API:

- `routingId()`
- `hasRoutingId()`
- `parts()`
- `isSinglePart()`
- `firstPart()`
- `singlePartOrThrow()`
- `close()`

### 9.2 `TopicMessage`

- topic receive 결과 aggregate
- optional `RoutingId`
- canonical topic type은 `String`
- payload는 `List<Message>`

유지 API:

- `routingId()`
- `hasRoutingId()`
- `topicId()`
- `parts()`
- `isSinglePart()`
- `firstPart()`
- `singlePartOrThrow()`
- `close()`

### 9.3 `SubscriptionEntry`

최종 형태:

```java
public record SubscriptionEntry(String filter, boolean pattern) {}
```

정책:

- canonical subscription/filter 표현은 `String`이다.
- 기존 `byte[]` 기반 `SubscriptionEntry`는 legacy compatibility path에서만 유지하고 제거 방향으로 간다.

### 9.4 `SubscriptionEvent`

신규 추가:

```java
public record SubscriptionEvent(
    RoutingId routingId,
    boolean subscribed,
    String topicId
) {}
```

정책:

- callback 쪽과 pull 쪽 topic 표현은 모두 `String topicId`로 통일한다.
- `byte[] topic` public exposure는 남기지 않는다.

## 10. 허용 인터페이스 매트릭스

| 클래스 | bind/connect | raw `send/recv` | `publish/subscribe` | subscription event | subscription 관리 | routing-id set/get | discovery attach |
|---|---|---|---|---|---|---|---|
| `PairSocket` | O | O | X | X | X | X | X |
| `DealerSocket` | O | O | X | X | X | O | O |
| `RouterSocket` | O | O | X | X | X | O | O |
| `StreamSocket` | bind only | routed only | X | X | X | X | X |
| `PubSocket` | O | X | `publish` | X | X | X | O |
| `SubSocket` | O | X | `subscribe` | X | O | X | O |
| `XPubSocket` | O | X | `publish` | O | X | X | X |
| `XSubSocket` | O | X | `subscribe` | X | O | X | X |

의미:

- PUB/SUB 계열 4소켓에는 raw `send/recv`가 아예 없다.
- `XPubSocket`에는 `subscribe()`가 없다.
- `SubSocket`/`XSubSocket`에는 `publish()`가 없다.
- `PairSocket`에는 routing-aware send가 없다.
- `StreamSocket` routing-id는 recv 결과로만 노출되며 직접 설정하지 않는다.
- `StreamSocket`은 bind-only server socket이다.
- discovery attach는 `DealerSocket`, `RouterSocket`, `PubSocket`, `SubSocket`만 지원한다.

## 11. option 노출 원칙

공통 규칙:

- common option은 `Socket` base에 둔다.
- dedicated function인 routing-id / TLS는 option API로 우회하지 않는다.
- type-specific option은 concrete type에만 둔다.
- old `setSockOpt/getSockOpt` raw zmq-style API는 canonical surface에서 제거 방향으로 간다.

이번 slice에서 고정할 것:

- `Socket.setOption(SocketOptionKey<T>, ...)`
- `Socket.getOption(SocketOptionKey<T>)`

후속 slice에서 concrete facade별 전용 option helper를 추가한다.

예:

- `RouterSocket`: router option helper
- `PubSocket`, `XPubSocket`: pub option helper
- `SubSocket`, `XSubSocket`: sub option helper
- `StreamSocket`: stream option helper

제외:

- TLS는 dedicated API이므로 option helper로 흡수하지 않는다.
- routing-id도 dedicated API이므로 option key로 흡수하지 않는다.

중요:

- socket split과 option key system 전체 재설계를 같은 단계에서 엮지 않는다.
- 이번 작업은 class surface 분리와 internal responsibility 정리가 우선이다.

## 12. 성능 계약

기존 구현은 이미 성능을 고려한 부분이 많다. 이번 분리는 그 경로를 버리는 작업이 아니라,
어디에 둘지 다시 정리하는 작업이다.

반드시 지킬 계약:

- direct `ByteBuffer` fast path 유지
- native `MemorySegment` zero-copy path 유지
- direct Netty `ByteBuf` fast path 유지
- socket별 scratch arena / scratch buffer 재사용 유지
- callback stub / arena는 socket당 1세트만 유지
- `Received` / `TopicMessage` / `SubscriptionEvent` 생성 시 불필요한 추가 복사 금지
- poller ready 경로에서 wrapper allocation 추가 금지

구체 정책:

- `Message.copyOf*` / `wrap*` 가 canonical payload factory다.
- public send/recv/publish/subscribe는 `Message` / `List<Message>` 만 쓴다.
- deprecated legacy buffer API는 `LegacySocketCompat`로 이동시키고, 새 facade에는 복제하지 않는다.
- scratch buffer는 `SocketCore`가 소유한다.
  - facade별로 따로 두지 않는다.
- `MessagePlane` / `TopicPlane`는 `SocketCore`의 scratch와 callback state를 공유한다.

금지:

- facade마다 별도 scratch arena 생성
- callback attach 때마다 per-call helper object 생성
- `List.copyOf()`나 `Stream`으로 hot path 결과를 다시 감싸는 패턴
- `String` / `byte[]` 자동 변환 convenience를 새 socket class에 재도입

## 13. `Poller` / `SocketPollSet` / `MonitorSocket` 영향 범위

socket split은 이 세 타입까지 같이 고정해야 한다.

### 13.1 `Poller`

현재 [`Poller.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Poller.java)
는 `Socket`에 직접 결합돼 있다. 최종 상태에서는 이 결합을 유지한다.

정책:

- `Poller.add/modify/remove/readySocket`는 계속 `Socket` base를 받는다.
- `Socket.handle()`는 package-private 유지한다.
- concrete facade 추가 때문에 `Poller` public signature를 다시 벌리지 않는다.

### 13.2 `SocketPollSet`

현재 [`SocketPollSet.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/SocketPollSet.java)
도 `Socket[]`에 직접 의존한다.

정책:

- `SocketPollSet`도 `Socket` base로 유지한다.
- typed socket 도입 때문에 별도 overload를 늘리지 않는다.

### 13.3 `MonitorSocket`

정책:

- monitor open은 `Socket` common base 책임으로 둔다.
- 다만 `MonitorSocket` 내부 구현은 generic `Socket.adopt(...)`에 의존하지 않는다.
- 최종 상태에서 `MonitorSocket`는 monitor handle 전용 internal owner를 사용한다.
  - 선택지 A: `SocketCore`를 직접 소유
  - 선택지 B: package-private `MonitorPeerSocket extends Socket`
- [`MonitorSocket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/MonitorSocket.java)의 `socket()` accessor는 canonical surface에서 제거 또는 deprecated 처리한다.
  - monitor handle을 또 다른 user-facing socket처럼 노출하면 abstract `Socket` 전환을 방해한다.

## 14. deprecated 호환 정책

최종 목표는 typed socket surface가 canonical path가 되는 것이다. 다만 구현 난이도와
기존 호출부 전환 비용을 고려해 두 단계로 간다.

### 14.1 1단계

- 기존 `Socket`은 당장 유지한다.
- 내부 구현을 `SocketCore` + `MessagePlane` + `TopicPlane`으로 먼저 분해한다.
- deprecated buffer convenience도 `LegacySocketCompat`로 분리한다.
- 이 단계에서는 외부 호출부와 테스트를 깨지 않는다.

### 14.2 2단계

- typed socket facade를 추가한다.
- 샘플, 문서, contract test를 concrete socket 기준으로 옮긴다.
- generic `Socket(Context, SocketType)` 생성자와 broad surface를 deprecated 처리한다.
- `MonitorSocket.socket()`과 `Socket.adopt(...)` 같은 generic compat hook도 이 단계에서 deprecated 처리한다.

### 14.3 3단계

- `Socket`을 abstract common base로 전환한다.
- generic 생성 경로를 제거한다.
- 필요하면 transitional `LegacySocket`를 한 릴리즈만 유지하고 제거한다.
- `MonitorSocket`의 internal adopted-handle 경로를 generic `Socket`과 완전히 분리한다.

결정:

- 이번 상세 설계의 최종 public surface는 abstract `Socket` + concrete typed sockets다.
- 구현은 1단계 내부 분해부터 시작한다.

## 15. 구현 순서

### Slice 1. 내부 분해

파일:

- [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Socket.java)
- `SocketCore.java`
- `MessagePlane.java`
- `TopicPlane.java`
- `LegacySocketCompat.java`

작업:

- `Socket.java`에서 common lifecycle/monitor/discovery/option/callback state를 `SocketCore`로 추출
- raw send/recv 계열을 `MessagePlane`으로 추출
- topic publish/subscribe/subscription 계열을 `TopicPlane`으로 추출
- deprecated buffer convenience를 `LegacySocketCompat`로 이동

완료 기준:

- public API 변화 없이 기존 test baseline 유지
- giant class가 더 이상 data-plane 전체를 직접 소유하지 않음

### Slice 2. typed facade 추가

파일:

- `PairSocket.java`
- `DealerSocket.java`
- `RouterSocket.java`
- `StreamSocket.java`
- `PubSocket.java`
- `SubSocket.java`
- `XPubSocket.java`
- `XSubSocket.java`
- `SubscriptionEvent.java`
- [`SubscriptionEntry.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/SubscriptionEntry.java)

작업:

- concrete facade 추가
- facade는 `SocketCore` / `MessagePlane` / `TopicPlane`에 위임
- type별 허용 API만 public으로 노출
- `SubscriptionEntry`를 canonical `String filter` 기반으로 전환

완료 기준:

- concrete socket만으로 raw/topic 사용 패턴 전부 표현 가능
- new sample / contract test는 generic `Socket` 없이 작성 가능
- subscription/filter canonical type이 public surface에서 `String`으로 정렬됨

### Slice 3. 주변 타입 정리

파일:

- [`Poller.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Poller.java)
- [`SocketPollSet.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/SocketPollSet.java)
- [`MonitorSocket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/MonitorSocket.java)
- [`Message.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Message.java)

작업:

- `Socket` base 중심으로 의존 관계 정리
- `Message.send/recv` 같은 legacy helper가 있으면 typed facade 기준으로 재배선 또는 deprecated 축소
- `MonitorSocket`가 abstract base를 안정적으로 참조하도록 정리

완료 기준:

- poller/monitor/helper 경계가 typed socket 도입 뒤에도 흔들리지 않음

### Slice 4. canonical surface 전환

파일:

- 샘플
- contract tests
- Javadoc

작업:

- 샘플과 테스트를 concrete socket 기준으로 전환
- generic `Socket` broad surface deprecated
- 문서에서 typed socket을 canonical path로 고정

완료 기준:

- 외부 사용자 관점에서 "어떤 socket을 써야 하는지"가 문서와 샘플로 명확함

### Slice 5. generic `Socket` 제거

작업:

- `Socket` abstract base 전환
- `Socket(Context, SocketType)` 제거
- broad generic methods 제거

완료 기준:

- compile-time surface가 최종 목표와 일치
- main/test/sample 코드에서 `new Socket(ctx, SocketType.X)` 사용이 `0건`
- main 코드에서 `Socket.adopt(...)` 사용이 `0건`

### Slice 6. POSD 후속 리팩토링

목적:

- socket split 구현이 끝난 뒤에도 남아 있을 수 있는 shallow wrapper, hidden coupling,
  change amplification, information leak를 추가로 제거한다.

대상:

- [`Socket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Socket.java)
- `SocketCore.java`
- `MessagePlane.java`
- `TopicPlane.java`
- typed socket facade 전반
- [`Poller.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Poller.java)
- [`SocketPollSet.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/SocketPollSet.java)
- [`MonitorSocket.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/MonitorSocket.java)
- [`Message.java`](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/systems/zlink/Message.java)

작업:

- facade가 단순 위임만 하는 얕은 래퍼에 그치지 않는지 다시 점검
- 여전히 여러 타입에 흩어진 lifecycle / callback / buffer 정책이 있으면 더 깊은 공통 모듈로 흡수
- 설명하기 어려운 public 메서드 조합이나 중복 shape를 추가 축소
- Javadoc과 테스트만으로 설명되지 않는 hidden contract를 surface나 내부 구조로 정리
- 한 번의 수정이 여러 클래스에 반복 전파되는 change amplification 지점을 다시 줄임

반복 규칙:

- POSD 관점에서 설명 가능한 리팩토링 대상이 하나라도 남아 있으면 계속 진행한다.
- "이미 동작하니 중단"은 종료 사유가 아니다.
- 새 리팩토링이 public contract를 바꾸면 먼저 문서와 테스트를 맞춘 뒤 적용한다.

완료 기준:

- 더 이상 설명 가능한 POSD 리팩토링 대상이 남아 있지 않다.
- 남은 복잡성이 있다면 성능 또는 core 공개 표면 제약 때문에 의도적으로 유지된 것임을 문서로 설명할 수 있다.
- 공통 정책은 공통 모듈에 모이고, facade는 타입별 의미 제한 역할만 남는다.

구현 완료 메모:

- 2026-03-27: compat-only STREAM public stub 군을 제거해 typed facade public surface 누수를 막았다.
- 2026-03-27: helper 경계용 `*Internal` shim을 없애고 `SocketCore`, `MessagePlane`,
  `TopicPlane`, `LegacySocketCompat`가 `Socket` package-private 메서드를 직접 사용하도록 정리했다.
- 2026-03-27: reflection contract test로 `PairSocket`과 `StreamSocket`의 public surface를 고정했고,
  `./gradlew clean test --no-daemon`, `./gradlew integrationTest --no-daemon`으로 최종 검증했다.

## 16. 테스트 전략

이번 작업의 검증 포인트는 "socket split 후에도 Java binding 계약이 더 명확하고 안전한가"다.

유지/추가할 contract test:

- concrete socket constructor smoke
- `PairSocket` send/recv
- `DealerSocket` routing-id set/get
- `RouterSocket` directed send + recv routing-id
- `RouterSocket` own routing-id set/get
- `PubSocket` publish + `SubSocket` subscribe
- `SubSocket` / `XSubSocket` subscription snapshot string contract
- `XPubSocket` subscription event
- `XPubSocket` subscription event + dedicated pub option routing
- TLS dedicated API smoke
- callback mode vs pull mode 배타성
- `Poller` / `SocketPollSet` / `MonitorSocket`가 abstract `Socket` base와 정상 동작하는지
- deprecated generic `Socket` compatibility path가 2단계 동안만 유지되는지

샘플 전환 기준:

- `pair-recv` -> `PairSocket`
- `dealer-router-recv` -> `DealerSocket`, `RouterSocket`
- `pubsub-recv` -> `PubSocket`, `SubSocket`
- `proxy-xsub-xpub` -> `XSubSocket`, `XPubSocket`
- `stream-recv` -> `StreamSocket`

하지 않을 것:

- core transport/protocol matrix 복제 확대
- 별도 `perf/` 트리 추가

## 17. 구현 착수 전 체크리스트

- `Socket.java` 내부 메서드를 common/message/topic/legacy 네 그룹으로 먼저 인벤토리 정리
- `Poller`, `SocketPollSet`, `MonitorSocket`, `Message`의 `Socket` 직접 의존 지점 확인
- `Native.java`에 이미 있는 `zlink_subscription_event` downcall을 public surface에 어떻게 연결할지 결정
- `SocketType` enum의 최종 역할을 compat-only로 축소할지 확인
- deprecated buffer send/recv 경로가 아직 외부 테스트에서 직접 쓰이는지 확인
- `MonitorSocket.socket()` accessor 실제 사용처가 있는지 확인하고 제거 가능성 판단

## 18. 최종 완료 기준

아래 조건이 모두 만족되면 socket split 작업을 완료로 본다.

- giant generic `Socket`가 더 이상 canonical user-facing entry가 아님
- concrete typed socket 8종으로 최신 core raw socket surface를 설명 가능함
- raw transport와 topic transport가 class level에서 분리됨
- `Poller`, `SocketPollSet`, `MonitorSocket`가 공통 base 위에서 안정적으로 동작함
- `Message`가 payload conversion의 유일한 public 진입점으로 유지됨
- existing fast path가 유지되고, facade 분리 때문에 추가 복사나 scratch duplication이 생기지 않음
- 샘플과 contract test가 concrete socket surface 기준으로 정렬됨
- POSD 관점의 추가 리팩토링 대상이 더 이상 남아 있지 않음
