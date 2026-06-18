<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 마켓플레이스 구매자·판매자 채팅](17-1-case-marketplace-chat.ko.md) | [다음: 케이스 — 게임 채팅](17-3-case-game-chat.ko.md)
<!-- framework-adapter-nav:end -->

# 케이스 — 라이브 커머스·라이브스트림 채팅

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> 방송 하나에 붙는 실시간 채팅, reaction, moderation, slow mode, 구매 이벤트를
> 다루는 독립 채팅 시스템 예다. 실행 가능한 샘플이 아니라, live chat 에서 ZLink 가
> 줄이는 배선과 그대로 남는 영상·moderation·저장 책임을 구분하는 문서다.

> **이 케이스에서 ZLink 이 좋은 지점**
> - STREAM 이 viewer 연결을 받고 stream room 으로 packet 을 보낸다.
> - stream SPOT 이 slow mode, moderator action, pinned message, lightweight fan-out 을
>   직렬 처리한다.
> - **그대로 남는 것**: 영상 송출, 상품/결제, 메시지 장기 저장, 대규모 CDN, abuse 분석.

## 1. 도메인 — 라이브 채팅의 진짜 난제

라이브 커머스와 라이브스트림 채팅은 marketplace 1:1 채팅보다 fan-out 이 넓고,
메시지 수명은 더 짧다. viewer 는 방송을 보며 message, emoji reaction, 질문,
구매 의사, 신고를 빠르게 보낸다. 모든 기능을 일반 group chat 처럼 켜면 고비용이
된다.

채팅 솔루션 업체들도 livestream 을 별도 유형으로 다룬다. Sendbird 는 live event
engagement, public/private watch party, moderation, live commerce 를 강조한다.
Stream 은 livestream channel 에서 read event, typing event, file upload, thread 같은
기능을 끄고 watcher 모델과 slow mode 를 쓰는 것을 권장한다. Ably 는 livestream,
customer support, social chat 을 chat API use case 로 함께 제시한다.

이 도메인의 핵심 요구는 아래와 같다.

- **high fan-in.** 짧은 시간에 많은 viewer 가 메시지와 reaction 을 보낸다.
- **bounded fan-out.** 모든 viewer 에게 모든 메시지를 반드시 보내는 것이 항상 답은 아니다.
- **moderation first.** 금칙어, 링크, spam, mute, ban, moderator delete 가 hot path 다.
- **feature gating.** read receipt, typing, attachment, thread 는 기본으로 끄는 편이 낫다.
- **slow mode/rate limit.** 사용자별, 방송별 rate limit 이 필요하다.
- **ephemeral window.** client 는 최근 N개 메시지와 pinned state 만 유지해도 충분한 경우가 많다.
- **commerce event.** 상품 클릭, 구매, 쿠폰, 재고, host message 같은 business event 가 섞인다.

권장 규모 가정은 아래 정도다.

| 항목 | 예시 규모 |
|------|-----------|
| concurrent viewers per stream | 100-20,000 |
| typical active chatters | viewer 의 1-10% |
| message rate per stream | 초당 수십-수백 |
| reaction rate | 메시지보다 높을 수 있음 |
| history | 최근 window + 선택적 저장 |
| exact delivery receipt | 보통 필요 없음 |

동시 viewer 가 수십만인 초대형 방송은 별도 fan-out service, edge pub/sub, hosted realtime
platform 이 필요할 수 있다. 이 문서는 **중소 규모 live commerce 플랫폼**을 기준으로 한다.

## 2. 기존 시스템 — WS gateway + stream chat service

### 2.1 컴포넌트와 역할

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| WebSocket gateway | viewer 연결 수용, stream room subscribe |
| stream chat service | room state, slow mode, moderator command 처리 |
| channel pub/sub | gateway node 들에 room event fan-out |
| moderation service | 금칙어, spam, mute/ban, 신고 |
| recent message cache | 최근 N개 메시지, pinned message, deleted marker |
| message store | 선택적 장기 저장, 운영자 review |
| commerce event bridge | 상품/주문/쿠폰 이벤트를 chat timeline 에 주입 |

### 2.2 메시지 흐름

```text
[classic live chat]

  +-------------+     +------------------+
  | Viewer app  | --> | WebSocket gateway|
  +-------------+     +--------+---------+
                              |
                              v
                       +------+------+
                       | Chat svc    |
                       +------+------+
                              |
                    +---------+----------+
                    |                    |
             +------v------+      +------v------+
             | Pub/Sub bus |      | Moderation  |
             +------+------+      +-------------+
                    |
             +------v------+
             | Gateways    |
             +-------------+
```

일반적인 chat send 흐름은 아래와 같다.

```java
LiveStream stream = streams.load(streamId);
stream.requireOpen();
ModerationDecision decision = moderation.check(userId, text);
if (!decision.allowed()) {
    gateway.send(sessionId, new MessageRejected(decision.reason()));
    return;
}
ChatMessage message = recent.append(streamId, userId, text);
pubsub.publish("stream:" + streamId, message);
```

이 구조는 표준적이지만 gateway subscription, pub/sub topic, moderator command, recent
cache, rate limit 이 여러 곳에 흩어지기 쉽다.

## 3. ZLink 시스템 — STREAM + stream SPOT + pub/sub

ZLink 로 구축할 때는 stream room 을 SPOT 으로 두는 편이 자연스럽다. 방송 room 은
동적으로 열리고 닫히며, room 별로 slow mode, muted users, pinned message, recent window 를
소유해야 한다.

### 3.1 컴포넌트와 역할

| 컴포넌트 | 역할 |
|----------|------|
| STREAM node | viewer connection, auth, packet dispatch |
| ViewerActor | viewer session/device binding |
| StreamSpot | stream room state, rate limit, moderation command, fan-out decision |
| ModeratorActor | mute, ban, delete, pin command |
| RecentMessageStore | 최근 message window 와 선택적 durable append |
| CommerceEvent channel | 상품/쿠폰/주문 event 를 stream room 으로 주입 |

### 3.2 메시지 흐름

```text
[ZLink live chat]

  +-------------+     +------------------+
  | Viewer app  | --> | STREAM session   |
  +-------------+     +--------+---------+
                              |
                              v
                       +------+------+
                       | ViewerActor |
                       +------+------+
                              |
                              v
                       +------+------+
                       | StreamSpot  |
                       +------+------+
                              |
                    +---------+----------+
                    |                    |
             +------v------+      +------v------+
             | Viewers     |      | Store/Jobs  |
             +-------------+      +-------------+
```

### 3.3 코드 골격

```kotlin
@Component
class LiveChatSession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingSession() {
    private var viewer: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(header: ZLinkStreamHeader, payload: Message) {
        if (header.name() == "auth") {
            val req = payload.decode(AuthViewerReq::class.java)
            val actor = actors.getOrCreate(req.viewerId, "viewer").await()
            viewer = context.actors().bind(actor).await()
            context.client().reply(AuthViewerOk()).submit().await()
            return
        }
        val bound = viewer ?: throw IllegalStateException("actor is not bound")
        bound.relay(header, payload).await()
    }
}
```

```kotlin
@Component
class SendLiveMessageHandler(
    private val moderation: Moderation,
    private val recent: RecentMessages,
) : ZLinkSuspendingSpotActorSendHandler<StreamSpot, ViewerActor, SendLiveChat> {
    override suspend fun handle(
        stream: StreamSpot,
        viewer: ViewerActor,
        context: ZLinkSpotActorSendContext,
        req: SendLiveChat,
        cancellationToken: CancellationToken,
    ) {
        stream.requireOpen()
        stream.requireNotMuted(viewer.actorId())
        stream.rateLimit().requireAllowed(viewer.actorId())
        val decision = moderation.check(stream.streamId(), viewer.actorId(), req.text)
        if (!decision.allowed()) {
            viewer.push(ChatRejected(decision.reason()))
            return
        }
        val message = recent.append(stream.streamId(), viewer.actorId(), req.text)
        stream.activeViewers().forEach { watching -> watching.push(message) }
    }
}
```

```kotlin
@Component
class PinMessageHandler :
    ZLinkSuspendingSpotActorSendHandler<StreamSpot, ModeratorActor, PinMessage> {
    override suspend fun handle(
        stream: StreamSpot,
        moderator: ModeratorActor,
        context: ZLinkSpotActorSendContext,
        req: PinMessage,
        cancellationToken: CancellationToken,
    ) {
        stream.requireModerator(moderator.actorId())
        stream.pin(req.messageId)
        stream.activeViewers().forEach { watching -> watching.push(MessagePinned(req.messageId)) }
    }
}
```

## 4. 기존 시스템과 ZLink 비교

| 축 | 기존 시스템 | ZLink 시스템 |
|----|-------------|--------------|
| viewer 연결 | WebSocket gateway | STREAM session |
| room subscribe | gateway subscription registry | StreamSpot membership 또는 watcher set |
| slow mode | chat service/cache | StreamSpot 상태 |
| mute/ban/delete | moderation service + pub/sub | ModeratorActor -> StreamSpot |
| fan-out | pub/sub bus -> gateway nodes | StreamSpot -> ViewerActor, 필요 시 pub/sub |
| recent window | Redis/cache | RecentMessageStore 유지 |
| durable history | DB optional | DB optional |
| commerce event | bridge service | channel/Spot handler 로 주입 |

## 5. ZLink 로 쉽게 감당 가능한 범위

이 구조가 잘 맞는 조건은 아래와 같다.

- 방송당 동시 viewer 가 보통 100-20,000명 수준이다.
- 메시지에는 exact delivery receipt 가 필요 없다.
- typing, thread, read event 같은 무거운 group-chat 기능을 기본으로 끈다.
- 최근 N개 메시지와 moderator command 가 핵심이다.
- 초대형 방송은 별도 pub/sub fan-out 또는 hosted realtime platform 을 붙일 수 있다.

주의할 점은 fan-out 이다. `for (ViewerActor watching : stream.activeViewers())` 는 작은 방송과
중간 규모 방송의 설명에는 좋지만, 방송 하나가 수만 명을 넘고 초당 수백 메시지가 계속되면
별도 fan-out shard 를 둬야 한다. 이때도 StreamSpot 은 rate limit, moderation, pinned state
같은 **결정 지점**으로 남고, 실제 전송은 shard 에 위임할 수 있다.

## 6. 참고한 제품 사례

- [Sendbird Live Stream Chat](https://sendbird.com/solutions/chat-for-live-streaming):
  live event engagement, moderation, watch party, live commerce 를 강조한다.
- [Stream livestream channel best practices](https://support.getstream.io/hc/en-us/articles/4403177485463-Best-Practices-for-Livestream-type-Channels-Chat):
  high-volume livestream channel 에서 read event, typing event, upload, thread 등을 끄고
  watcher 와 slow mode 를 쓰는 방향을 제시한다.
- [Ably Chat](https://ably.com/solutions/chat): customer support, social chat,
  livestream conversation 같은 live interaction use case 를 함께 제시한다.

## 7. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 공통 채팅 개요: [17-case-chat-messaging](17-case-chat-messaging.ko.md)
- 이전 케이스: [17-1-case-marketplace-chat](17-1-case-marketplace-chat.ko.md)
- 다음 케이스: [17-3-case-game-chat](17-3-case-game-chat.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 마켓플레이스 구매자·판매자 채팅](17-1-case-marketplace-chat.ko.md) | [다음: 케이스 — 게임 채팅](17-3-case-game-chat.ko.md)
<!-- framework-adapter-nav:bottom:end -->
