<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 라이브 커머스·라이브스트림 채팅](17-2-case-live-commerce-chat.ko.md) | [다음: 케이스 — 트레이딩 시스템](18-case-trading-system.ko.md)
<!-- framework-adapter-nav:end -->

# 케이스 — 게임 채팅

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> 게임 안에서 private chat, party chat, guild chat, battle chat, system alert 를
> 독립 채팅 시스템으로 구축하는 예다. 실행 가능한 샘플이 아니라, game chat 도메인에
> ZLink 의 STREAM·actor·room 모델을 어디까지 넣을지 판단하는 케이스 스터디다.

> **이 케이스에서 ZLink 이 좋은 지점**
> - game client 연결을 STREAM session 으로 받고 player actor 에 붙인다.
> - party, guild, battle 같은 room 단위를 actor 또는 SPOT 으로 표현한다.
> - **그대로 남는 것**: account DB, game state DB, moderation, push notification,
>   장기 chat history, anti-cheat/abuse 분석.

## 1. 도메인 — 게임 채팅의 진짜 난제

게임 채팅은 독립 채팅 시스템 예제로 좋다. 전체 게임 동접은 커도 실제 chat room 은
자연스럽게 scope 가 나뉜다. private, party, guild, match, battle, world notice 는
각각 필요한 보장과 fan-out 크기가 다르다.

채팅 솔루션 업체들도 gaming 을 주요 use case 로 둔다. Sendbird 는 game studio 들이
cross-game/platform chat, voice, moderation, player insight 를 사용한다고 설명한다.
PubNub 의 War Dragons 사례는 in-house private chat 은 단순 메시지에는 충분했지만,
guild battle 처럼 동시에 많은 사용자가 대화하는 순간 scale 문제가 드러났다고 설명한다.
CometChat 도 gaming use case 에서 multi-user group, moderation, multiplayer messaging 을
강조한다.

게임 채팅의 핵심 요구는 아래와 같다.

- **scope 별 room 모델.** whisper, party, guild, match, battle, world 가 서로 다르다.
- **session binding.** player 는 모바일/PC/콘솔 중 하나 이상으로 접속할 수 있다.
- **low latency.** gameplay 중 채팅은 빠르게 보여야 하지만, 모든 메시지가 영속일 필요는 없다.
- **moderation.** 욕설, spam, 신고, mute, block, GM command 가 필요하다.
- **context event.** 전투 위치, guild 가입, 점령, 아이템 획득 같은 game event 가 chat 에 섞인다.
- **offline behavior.** guild notice, DM 은 offline push 가 필요할 수 있다.
- **region/shard.** 서버군, world, region 에 따라 player 와 chat room 이 나뉜다.

권장 규모 가정은 아래 정도다.

| 항목 | 예시 규모 |
|------|-----------|
| total CCU | 10만-100만 |
| shard/world CCU | 5천-5만 |
| party room | 2-8명 |
| guild online | 10-300명 |
| battle/event room | 20-500명 |
| world chat | 크지만 rate limit 이 강함 |
| exact read receipt | 대부분 필요 없음 |

이 문서는 게임 전체 동접 100만을 한 room 으로 보내는 구조가 아니라, **gameplay scope 로
나뉜 독립 채팅 시스템**을 다룬다.

## 2. 기존 시스템 — game gateway + chat service

### 2.1 컴포넌트와 역할

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| game gateway | game client 연결, 인증, ping/pong |
| chat service | chat command, room membership, rate limit |
| player connection registry | `playerId -> gateway node/session` 위치 추적 |
| guild/party service | game domain membership 조회 |
| pub/sub bus | gateway node 간 deliver, system alert broadcast |
| moderation service | profanity, spam, mute, block, report |
| chat history store | guild/DM 같은 선택적 history |
| push notification worker | offline DM/guild notice |

### 2.2 메시지 흐름

```text
[classic game chat]

  +-------------+     +--------------+     +-------------+
  | Game client | --> | Game gateway | --> | Chat svc    |
  +-------------+     +--------------+     +------+------+
                                                  |
                    +------------------+          |
                    | Conn registry    | <--------+
                    +------------------+
                                                  |
                    +------------------+     +----v------+
                    | Guild/party svc  | --> | Pub/Sub   |
                    +------------------+     +-----------+
```

일반적인 guild message 흐름은 아래와 같다.

```java
Guild guild = guilds.load(guildId);
guild.requireMember(playerId);
ModerationDecision decision = moderation.check(playerId, text);
if (!decision.allowed()) { return; }
GameChatMessage message = chatHistory.appendGuild(guildId, playerId, text);
for (String member : guild.members()) {
    connectionRegistry.find(member)
        .ifPresent(session -> gatewayDeliver.send(session.nodeId(), session.sessionId(), message));
}
```

이 구조는 game server 와 chat server 가 분리되어도 동작하지만, player 위치 조회와
guild/party membership 조회가 매 메시지 hot path 에 섞이기 쉽다.

## 3. ZLink 시스템 — STREAM + player actor + room SPOT

ZLink 기반 구조에서는 player connection 을 actor binding 으로 표현하고, chat scope 를
room actor 또는 SPOT 으로 둔다.

### 3.1 chat scope 별 모델

| scope | 권장 모델 | 이유 |
|-------|-----------|------|
| whisper / DM | target PlayerActor direct send | room 상태가 거의 없음 |
| party | PartyActor | 작은 group, gameplay state 와 가까움 |
| guild | GuildChatSpot | membership, mute, history, notice 를 직렬 처리 |
| match / battle | MatchChatSpot | match lifecycle 과 함께 생성/종료 |
| world chat | WorldChatSpot + rate limit | 큰 fan-out 이므로 제한과 batching 필요 |
| system alert | pub/sub channel | 모든 room state 가 필요하지 않은 broadcast |

### 3.2 컴포넌트와 역할

| 컴포넌트 | 역할 |
|----------|------|
| STREAM node | game client 연결과 packet dispatch |
| PlayerActor | player session/device binding, client push |
| PartyActor | party membership 과 small-room fan-out |
| GuildChatSpot | guild member online set, mute/block, notice, history append |
| MatchChatSpot | match/battle 채팅과 lifecycle |
| ChatModeration channel | profanity/spam/report 작업 |
| ChatHistoryStore | DM/guild history 저장 |

### 3.3 메시지 흐름

```text
[ZLink game chat]

  +-------------+     +----------------+
  | Game client | --> | STREAM session |
  +-------------+     +-------+--------+
                             |
                             v
                      +------+------+
                      | PlayerActor |
                      +------+------+
                             |
             +---------------+----------------+
             |               |                |
      +------v------+ +------v------+ +-------v------+
      | PartyActor  | | GuildSpot   | | MatchSpot    |
      +-------------+ +------+------+ +--------------+
                             |
                      +------v------+
                      | History DB  |
                      +-------------+
```

### 3.4 코드 골격

```kotlin
@Component
class GameChatSession(
    private val context: ZLinkSessionContext,
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingSession() {
    private var player: ZLinkSessionActor? = null

    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(header: ZLinkStreamHeader, payload: ZLinkMessage) {
        if (header.name() == "auth") {
            val req = payload.decode(AuthPlayerReq::class.java)
            val actor = actors.getOrCreate(req.playerId, "player").await()
            player = context.actors().bind(actor).await()
            context.client().reply(AuthPlayerOk()).submit().await()
            return
        }
        val bound = player ?: throw IllegalStateException("actor is not bound")
        bound.relay(header, payload).await()
    }
}
```

```kotlin
class PlayerActor(
    private val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    override fun actorId(): String = actorId
    override fun context(): ZLinkActorContext = context

    fun pushChat(message: GameChatMessage): CompletionStage<Void> =
        context.boundSession().send(message).submit()
}
```

```kotlin
class PlayerActor(
    private val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    override fun actorId(): String = actorId
    override fun context(): ZLinkActorContext = context

    fun pushChat(message: GameChatMessage): CompletionStage<Void> =
        context.boundSession().send(message).submit()
}
```

```kotlin
@Component
class SendPartyChatHandler :
    ZLinkSuspendingSpotActorSendHandler<PartyRoomSpot, PartyActor, SendPartyChat> {
    override suspend fun handle(
        spot: PartyRoomSpot,
        party: PartyActor,
        context: ZLinkSpotActorSendContext,
        req: SendPartyChat,
        cancellationToken: CancellationToken,
    ) {
        party.requireMember(req.senderId)
        party.members().forEach { member ->
            member.pushChat(PartyChatMessage(req.senderId, req.text))
        }
    }
}
```

## 4. 기존 시스템과 ZLink 비교

| 축 | 기존 시스템 | ZLink 시스템 |
|----|-------------|--------------|
| client 연결 | game gateway 직접 | STREAM session |
| player 위치 | connection registry 조회 | PlayerActor session binding |
| party chat | chat service + party service 조회 | PartyActor |
| guild chat | chat service + guild service 조회 | GuildChatSpot |
| battle chat | match service + pub/sub | MatchChatSpot |
| system alert | pub/sub bus | pub/sub channel 유지 |
| moderation | 별도 service | 별도 service/channel 유지 |
| history | DB | DB 유지 |

## 5. ZLink 로 쉽게 감당 가능한 범위

이 구조가 잘 맞는 조건은 아래와 같다.

- room scope 가 gameplay domain 과 자연스럽게 나뉜다.
- party, guild, match room 대부분이 수백 명 이하다.
- world chat 은 rate limit 이 강하고, 모든 메시지에 exact delivery receipt 가 필요 없다.
- message history 는 DM/guild 중심이고, battle/world 는 짧은 window 만 저장해도 된다.
- anti-cheat, abuse 분석, long-term search 는 별도 pipeline 으로 둔다.

아래 조건이면 별도 대규모 chat service 또는 hosted realtime platform 이 필요할 수 있다.

- 하나의 world chat 에 수만 명 이상이 지속적으로 참여한다.
- 모든 reaction, read receipt, typing event 를 full fidelity 로 저장해야 한다.
- cross-region global shard 간 message ordering 이 핵심 요구다.
- chat moderation 과 fraud detection 이 product 핵심이며 별도 data platform 이 필요하다.

## 6. 참고한 제품 사례

- [Sendbird gaming chat](https://sendbird.com/solutions/chat-for-gaming): game studio 들의
  cross-game/platform chat, voice, moderation, player insight use case 를 제시한다.
- [PubNub War Dragons 사례](https://www.pubnub.com/customers/pocket-gems/): private chat 은
  단순 메시지에는 충분했지만, guild battle 같은 동시 대화에서 scale 이 문제가 되었고,
  in-game chat, guild conversation, global battle update 를 realtime 기능으로 다룬다.
- [CometChat Chat and Messaging](https://www.cometchat.com/chat-and-messaging): gaming 에서
  multiple user groups, community, moderation, multiplayer realtime messaging 을 강조한다.

## 7. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 게임 서버 케이스: [15-case-realtime-game](15-case-realtime-game.ko.md)
- 공통 채팅 개요: [17-case-chat-messaging](17-case-chat-messaging.ko.md)
- 이전 케이스: [17-2-case-live-commerce-chat](17-2-case-live-commerce-chat.ko.md)
- 다음 케이스: [18-case-trading-system](18-case-trading-system.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 라이브 커머스·라이브스트림 채팅](17-2-case-live-commerce-chat.ko.md) | [다음: 케이스 — 트레이딩 시스템](18-case-trading-system.ko.md)
<!-- framework-adapter-nav:bottom:end -->
