[문서 목록](../../README.ko.md) | [이전: 케이스 — 라이드헤일링 실시간 디스패치](16-case-ride-hailing.ko.md) | [다음: 케이스 — 마켓플레이스 구매자·판매자 채팅](17-1-case-marketplace-chat.ko.md)

# 케이스 — 채팅·메시징 플랫폼

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> room 을 **주소 가능한 노드(SPOT)** 로 두어 membership 과 fan-out 결정을 직렬화하는
> 사례다. 실제 client push 는 각 user actor 의 `boundSession` 이 맡고,
> **메시지 영속 저장은 그대로 DB** 다. 이 문서는 채팅 도메인의
> 책임 경계와 ZLink 매핑을 설명하는 케이스 스터디다.

> **이 케이스에서 ZLink 이 좋은 지점**
> - STREAM 이 client 연결을, actor binding 이 "누가 어디 붙었나"를 소유한다.
> - room SPOT 이 membership 과 fan-out 결정을 단일 큐에서 직렬 처리한다.
> - **그대로 남는 것**: 메시지 durable 저장·순서/전달 보장 정책은 DB·앱 책임.

## 1. 도메인 — 채팅 백엔드의 진짜 난제

- **연결 레지스트리.** 수백만 동시 연결에서 "누가 어느 노드에 붙었나" 를 알아야
  메시지를 전달한다. 보통 **Redis 연결 레지스트리** 로 추적한다.
- **room fan-out + presence.** 한 메시지가 room 멤버 전원에게 가고, presence(접속
  상태)는 한 사람 변화가 수백 구독자로 퍼진다 — fan-out 이 넓다.
- **순서와 전달 보장.** room 안 메시지 순서, 재접속 시 누락 없는 전달, read receipt
  같은 의미가 필요하다.
- **영속 이력.** 메시지는 durable 저장(스크롤백·검색)이 필요하다.
  ([getstream](https://getstream.io/blog/chat-application-architecture/),
  [Ably](https://ably.com/blog/scaling-pub-sub-with-websockets-and-redis))

남는 난제: **메시지 durable 저장(DB)** 과 순서/전달 보장 정책은 그대로 남는다.
ZLink 가 줄이는 건 연결 위치 조회 직접 관리·room fan-out 결정·group service 다.

## 2. 기존 스택 — WS fleet + Redis 레지스트리 + group service

### 2.1 컴포넌트와 그 이유

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| WS gateway fleet | 수백만 동시 연결 수용(stateless 노드 여러 대) |
| Redis 연결 레지스트리 | "누가 어느 노드에 붙었나" 추적 → 전달 라우팅의 근거 |
| group/fan-out service | room 멤버 목록 조회 + 각 멤버 노드로 전달 결정 |
| Redis pub/sub | 노드 간 메시지 전달 버스 |
| 메시지 DB | 스크롤백·검색용 **영속 이력** |

### 2.2 전달 경로

```ts
const userId = authenticate(ws);
await redis.hset(`presence:${userId}`, 'node', thisNodeId);
const members = await redis.smembers(`room:${roomId}:members`);
for (const member of members) {
  const node = await redis.hget(`presence:${member}`, 'node');
  await redis.publish(`deliver:${node}`, encode(roomId, msg));
}
await messageDb.append(roomId, msg);
```

서 있어야 하는 것: WS gateway fleet, Redis 연결 레지스트리, Redis pub/sub 라우팅,
group/fan-out service, 메시지 DB.

## 3. ZLink 스택 — STREAM + room SPOT + boundSession

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .actorFactory('user', UserActorFactory)
    .addSpotNode('rooms')
      .enableRouter('tcp://0.0.0.0:7700')
      .enablePubSub('tcp://0.0.0.0:7701')
      .addSpotFactory(ChatRoomSpot)
    .addStreamNode('chat')
      .bind('tcp://0.0.0.0:7710')
      .attachActorGateway('rooms')
      .registerSession(ChatSession)
    .build()
);
```

```ts
@zlinkSpotActorSendHandler({
  spot: () => ChatRoomSpot,
  actor: () => UserActor,
  packetName: 'Say',
})
export class SayHandler
  implements ZLinkSpotActorSendHandler<ChatRoomSpot, UserActor, Say> {
  constructor(private readonly db: MessageDb) {}

  async handle(
    spot: ChatRoomSpot,
    actor: UserActor,
    context: ZLinkSpotActorSendContext,
    msg: Say,
  ): Promise<void> {
    const chat = new ChatMessage(spot.roomId, actor.actorId, msg.text);
    await this.db.append(chat);
    for (const member of spot.members()) {
      await member.pushChat(chat);
    }
  }
}

export class UserActor implements ZLinkActor {
  constructor(
    readonly actorId: string,
    readonly context: ZLinkActorContext,
  ) {}

  pushChat(chat: ChatMessage): Promise<void> {
    return this.context.boundSession.send(chat).submit();
  }
}
```

```ts
@Injectable()
export class ChatSession implements ZLinkSession {
  private user?: ZLinkActor;

  constructor(
    readonly context: ZLinkSessionContext,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === 'auth') {
      const req = payload.decode<AuthReq>();
      const actor = await this.actors.getOrCreate(req.userId, 'user');
      this.user = await this.context.actors.bind(actor);
      await this.context.client.reply(new AuthOk()).submit();
      return;
    }
    if (!this.user) {
      throw new Error('actor is not bound');
    }
    await this.user.relay(payload);
  }
}
```

> 연결 레지스트리("누가 어디 붙었나")를 응용이 직접 조회하지 않는다. session↔actor
> binding 은 framework 가 들고, room membership 과 fan-out 순서는 room SPOT 이
> 소유한다. 다른 actor 의 client 로 보내려면 해당 actor 가 자기 `boundSession` 으로
> push 한다([06-actor-session](../06-actor-session.ko.md)). presence 는 같은 room SPOT
> 상태 변화나 별도 pub/sub 토픽으로 표현할 수 있다.

## 4. 양쪽 코드 비교 — "room 에 한 마디"

| 축 | 기존(WS + Redis) | ZLink |
|----|------------------|-------|
| 연결 수용 | WS gateway 직접 | STREAM `ZLinkSession` |
| 연결 레지스트리 | Redis presence 해시 | framework(session/actor binding) |
| 멤버 조회·라우팅 | group service + Redis pub/sub | room SPOT(membership 소유) |
| fan-out | 멤버별 deliver publish | room SPOT loop + `boundSession.send(...)` |
| 메시지 영속 | 메시지 DB | 메시지 DB(유지) |

## 5. 아키텍처 비교 — 컴포넌트와 메시지 흐름

```text
[classic]  WS fleet + Redis registry + group service

  +----------------------+
  | WS gateway fleet     |
  +----------+-----------+
  +----------v-----------+   +------------------+
  | Redis conn registry  |   | group/fanout svc |
  | + pub/sub routing    |   +------------------+
  +----------+-----------+
  +----------v-----------+
  | message DB (history) |
  +----------------------+
```

```text
[ZLink]  STREAM + room SPOT + boundSession

  +----------------------+
  | Session server       |   conn + actor bind/relay
  | (STREAM)             |
  +----------+-----------+
  +----------v-----------+    +-----------+
  | room SPOT            |--->| Registry  |
  | membership + fan-out |    +-----------+
  +----------+-----------+
  +----------v-----------+
  | message DB (history) |   (unchanged)
  +----------------------+
```

- **빠지는 박스:** WS gateway fleet, Redis 연결 레지스트리, group/fan-out service,
  pub/sub 라우팅 계층.
- **그대로인 박스:** 메시지 DB(영속 이력).

### 메시지 흐름 — 시퀀스 비교

room 에 한 마디 보내는 흐름이다.

```mermaid
sequenceDiagram
%%{init: {'theme': 'base', 'themeVariables': {'signalTextColor': '#000000', 'actorTextColor': '#000000', 'noteTextColor': '#000000', 'actorBkg': '#ffffff', 'actorBorder': '#555555', 'activationBorderColor': '#555555'}}}%%
  autonumber
  participant C as client
  participant GW as WS gateway
  participant R as Redis registry
  participant GS as group svc
  participant DB as message DB
  C->>GW: WS Say
  GW->>GS: room 멤버에게 전달 요청
  GS->>R: 멤버 노드 위치 조회
  GS->>GW: 각 멤버 노드로 deliver
  GW->>DB: 이력 append
  GW-->>C: delivered
```

```mermaid
sequenceDiagram
%%{init: {'theme': 'base', 'themeVariables': {'signalTextColor': '#000000', 'actorTextColor': '#000000', 'noteTextColor': '#000000', 'actorBkg': '#ffffff', 'actorBorder': '#555555', 'activationBorderColor': '#555555'}}}%%
  autonumber
  participant C as client
  participant S as Session 서버
  participant RM as room SPOT
  participant U as member actors
  participant DB as message DB
  C->>S: STREAM Say
  S->>RM: RelayToActor
  RM->>DB: 이력 append
  RM->>U: member loop
  U-->>S: boundSession.send
  S-->>C: STREAM push
```

연결 레지스트리 조회와 group service 라우팅이 줄어든다. room SPOT 이 membership 과
fan-out 순서를 소유하고, 각 user actor 가 자기 `boundSession` 으로 client 에 push
한다. 메시지 DB append 는 양쪽 모두 그대로다.

## 6. 줄어드는 것 / 그대로 남는 것

- **줄어드는 것:** WS gateway fleet, 연결 위치 조회 직접 관리, group/fan-out service.
- **그대로 남는 것:** **메시지 durable 저장(DB)**, 순서/전달 보장 정책, read
  receipt 같은 의미. pub/sub 는 transport fan-out 이라 영속/replay 가 필요하면
  broker 가 맞다. 공통 경계는
  [12-grpc-alternative](../12-grpc-alternative.ko.md)의 참고 절 참고.

## 7. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 사용법: [04-channel-messaging](../04-channel-messaging.ko.md), [05-spot](../05-spot.ko.md), [06-actor-session](../06-actor-session.ko.md), [07-stream](../07-stream.ko.md)
- 상세 채팅 케이스: [Marketplace](17-1-case-marketplace-chat.ko.md),
  [Live commerce](17-2-case-live-commerce-chat.ko.md),
  [Game chat](17-3-case-game-chat.ko.md)
- 다음 케이스: [17-1-case-marketplace-chat](17-1-case-marketplace-chat.ko.md)

---
[문서 목록](../../README.ko.md) | [이전: 케이스 — 라이드헤일링 실시간 디스패치](16-case-ride-hailing.ko.md) | [다음: 케이스 — 마켓플레이스 구매자·판매자 채팅](17-1-case-marketplace-chat.ko.md)
