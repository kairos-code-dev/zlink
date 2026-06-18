<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 채팅·메시징 플랫폼](17-case-chat-messaging.ko.md) | [다음: 케이스 — 라이브 커머스·라이브스트림 채팅](17-2-case-live-commerce-chat.ko.md)
<!-- framework-adapter-nav:end -->

# 케이스 — 마켓플레이스 구매자·판매자 채팅

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> 거래나 문의 단위로 생기는 1:1 또는 소규모 group conversation 을 독립 채팅
> 시스템으로 구축하는 예다. 실행 가능한 샘플이 아니라, marketplace 채팅에서 ZLink 가
> 맡는 통신·상태 경계와 남는 책임을 판단하기 위한 문서다.

> **이 케이스에서 ZLink 이 좋은 지점**
> - STREAM 이 web/mobile client 연결을 받고 user actor 에 붙인다.
> - conversation actor 또는 SPOT 이 참여자, typing, read state, fan-out 결정을 소유한다.
> - **그대로 남는 것**: 메시지 DB, 첨부 저장소, moderation 정책, offline push, 검색 색인.

## 1. 도메인 — 마켓플레이스 채팅의 진짜 난제

마켓플레이스 채팅은 Discord 나 Slack 같은 범용 대형 채팅 플랫폼보다 작지만,
단순 WebSocket 예제보다는 복잡하다. 구매자와 판매자는 상품, 주문, 예약, 견적,
배송 같은 business object 를 중심으로 대화한다. 대화방 대부분은 1:1 이지만,
운영자, 중개자, 공동 판매자, 배송 담당자가 들어오면 소규모 group 이 된다.

채팅 솔루션 업체들도 이 영역을 독립 use case 로 본다. CometChat 은 marketplace 를
"buyer-seller conversation" 과 moderation 이 필요한 use case 로 설명하고, Twilio
Conversations 는 digital marketplace 를 고객과 적절한 상대를 연결하는 conversation 으로
제시한다. TalkJS 도 marketplace, hiring, team, livestream 을 별도 데모로 나눈다.

이 규모에서 중요한 요구는 아래와 같다.

- **conversation ownership.** `productId`, `orderId`, `supportTicketId` 같은 domain id 가
  채팅방 id 와 연결된다.
- **participant policy.** 구매자, 판매자, 운영자, bot 이 대화에 들어오고 나간다.
- **message history.** 거래 분쟁, 환불, 신고 대응을 위해 durable history 가 필요하다.
- **read/unread state.** unread badge, last read cursor, delivery/read receipt 가 필요하다.
- **notification.** offline 사용자는 push, email, SMS 같은 외부 알림을 받는다.
- **moderation.** 금칙어, 개인정보 노출, 사기 링크, 신고, 차단이 필요하다.
- **attachment.** 이미지, 영수증, 상품 사진은 object storage 와 malware scan 이 맡는다.

권장 규모 가정은 아래 정도다.

| 항목 | 예시 규모 |
|------|-----------|
| registered users | 100만-500만 |
| DAU | 5만-30만 |
| concurrent users | 1만-10만 |
| active conversations | 수만-수십만 |
| room size | 대부분 2명, 일부 3-10명 |
| message rate | 초당 수백-수천 |

이 정도면 global fan-out 최적화보다 **연결, routing, conversation state, DB 연동을
단순하게 유지하는 것**이 더 중요하다.

## 2. 기존 시스템 — WS gateway + conversation service

### 2.1 컴포넌트와 역할

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| WebSocket gateway | client 연결 수용, 인증, ping/pong, reconnect 처리 |
| connection registry | `userId -> gateway node/session` 위치 추적 |
| conversation service | 참여자 검증, message append, read state 갱신 |
| fan-out service | online 참여자 gateway 로 deliver |
| message DB | durable history, cursor query |
| object storage | attachment upload/download |
| moderation service | 금칙어, 사기 링크, 신고, 차단 |
| notification worker | offline push/email/SMS |
| search index | 메시지 검색, 운영자 조사 |

### 2.2 메시지 흐름

```text
[classic marketplace chat]

  +-------------+     +------------------+     +-------------+
  | Web client  | --> | WebSocket gateway| --> | Chat API    |
  +-------------+     +------------------+     +------+------+
                                                   |
                       +------------------+        |
                       | Conn registry    | <------+
                       +------------------+
                                                   |
  +-------------+     +------------------+     +---v---------+
  | Push/email  | <-- | Notification     | <-- | Message DB  |
  +-------------+     +------------------+     +-------------+
```

일반적인 send 흐름은 아래와 같다.

```java
Conversation conversation = conversations.load(conversationId);
conversation.requireParticipant(senderId);
ChatMessage message = messages.append(conversationId, senderId, text);
moderation.enqueue(message);
for (String userId : conversation.participants()) {
    connectionRegistry.find(userId)
        .ifPresentOrElse(
            session -> gatewayDeliver.send(session.nodeId(), session.sessionId(), message),
            () -> notifications.enqueue(userId, message));
}
```

이 구조는 이해하기 쉽지만 연결 위치 조회, fan-out routing, gateway 간 deliver,
conversation state 변경이 여러 서비스에 흩어진다.

## 3. ZLink 시스템 — STREAM + user actor + conversation actor

ZLink 로 구축할 때도 DB, 검색, 첨부 저장소는 사라지지 않는다. ZLink 가 줄이는 부분은
**연결 위치를 application 이 직접 추적하고, 그 위치로 직접 deliver 하는 코드**다.

### 3.1 컴포넌트와 역할

| 컴포넌트 | 역할 |
|----------|------|
| STREAM node | client socket 연결과 packet dispatch |
| UserActor | user 의 online session/device binding 소유 |
| ConversationActor 또는 ConversationSpot | 참여자, typing, read state, fan-out 결정 |
| MessageStore | durable append, page query |
| Moderation channel | 비동기 moderation 작업 |
| Notification channel | offline push/email/SMS 작업 |

### 3.2 메시지 흐름

```text
[ZLink marketplace chat]

  +-------------+     +------------------+
  | Web client  | --> | STREAM session   |
  +-------------+     +--------+---------+
                              |
                              v
                       +------+------+
                       | UserActor   |
                       +------+------+
                              |
                              v
                       +------+------+
                       | Conversation|
                       | Actor/SPOT  |
                       +------+------+
                              |
                    +---------+----------+
                    |                    |
             +------v------+      +------v------+
             | Message DB  |      | Notify jobs |
             +-------------+      +-------------+
```

### 3.3 코드 골격

```kotlin
@Component
class MarketplaceChatSession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingSession() {
    private var user: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(header: ZLinkStreamHeader, payload: Message) {
        if (header.name() == "auth") {
            val req = payload.decode(AuthReq::class.java)
            val actor = actors.getOrCreate(req.userId, "chat-user").await()
            user = context.actors().bind(actor).await()
            context.client().reply(AuthOk()).submit().await()
            return
        }
        val bound = user ?: throw IllegalStateException("actor is not bound")
        bound.relay(header, payload).await()
    }
}
```

```kotlin
class UserActor(private val context: ZLinkActorContext) : ZLinkActor {
    fun push(message: ChatMessage): CompletionStage<Void> =
        context.boundSession().send(message).submit()
}
```

```kotlin
class UserActor(private val context: ZLinkActorContext) : ZLinkActor {
    fun push(message: ChatMessage): CompletionStage<Void> =
        context.boundSession().send(message).submit()
}
```

## 4. 기존 시스템과 ZLink 비교

| 축 | 기존 시스템 | ZLink 시스템 |
|----|-------------|--------------|
| client 연결 | WebSocket gateway 직접 구현 | STREAM session |
| user 위치 | connection registry 조회 | actor/session binding |
| conversation routing | API/service 가 room owner 를 직접 찾음 | ActorGateway 또는 SPOT routing |
| online fan-out | gateway node/session 으로 직접 deliver | UserActor `boundSession().send(...)` |
| offline 알림 | notification worker | 그대로 유지 |
| history 저장 | message DB | 그대로 유지 |
| moderation | 별도 service/job | 그대로 유지 |
| read state | DB/cache 직접 갱신 | conversation actor 가 직렬 결정, 저장은 DB/cache |

## 5. ZLink 로 쉽게 감당 가능한 범위

이 구조는 아래 조건에서 적당하다.

- conversation 대부분이 1:1 또는 10명 이하 group 이다.
- 한 room 에 수천 명 이상이 동시에 몰리는 open chat 이 주 요구가 아니다.
- 메시지 저장, 검색, 첨부, moderation 은 기존 DB/service 로 분리한다.
- 채팅의 핵심 문제가 "누가 어디 붙었는지", "어느 conversation actor 로 보내야 하는지",
  "online 참여자에게 어떻게 push 할지"에 있다.

아래 조건이면 별도 fan-out 계층이나 hosted chat service 검토가 필요하다.

- 하나의 room 에 수만 명 이상이 들어오는 open livestream chat 이 많다.
- 모든 메시지에 delivery receipt 를 모든 device 단위로 정확히 기록해야 한다.
- cross-region active-active 와 지역별 data residency 가 핵심 요구다.
- 채팅 검색, moderation, anti-abuse 가 제품의 대부분을 차지한다.

## 6. 참고한 제품 사례

- [Sendbird Chat Messaging](https://sendbird.com/products/chat-messaging): marketplace,
  delivery/rideshare, retail/e-commerce, digital health 같은 산업 use case 를 제시한다.
- [CometChat Chat and Messaging](https://www.cometchat.com/chat-and-messaging):
  marketplace 에서 buyer-seller conversation, moderation, rich media 를 강조한다.
- [Twilio Conversations API](https://www.twilio.com/en-us/messaging/apis/conversations-api):
  customer care, commerce, digital marketplace, relationship management 를 주요 용도로 둔다.
- [TalkJS hiring demo](https://talkjs.com/demo/hiring/): marketplace, hiring, livestream,
  team chat 을 제품 데모 단위로 구분한다.

## 7. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 공통 채팅 개요: [17-case-chat-messaging](17-case-chat-messaging.ko.md)
- 다음 케이스: [17-2-case-live-commerce-chat](17-2-case-live-commerce-chat.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 채팅·메시징 플랫폼](17-case-chat-messaging.ko.md) | [다음: 케이스 — 라이브 커머스·라이브스트림 채팅](17-2-case-live-commerce-chat.ko.md)
<!-- framework-adapter-nav:bottom:end -->
