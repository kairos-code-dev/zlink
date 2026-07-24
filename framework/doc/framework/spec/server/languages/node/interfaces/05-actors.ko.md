# Node.js Actor와 session binding 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Actor model](../../../22-actor-model.ko.md) ·
[Spot·Actor membership](../../../23-spot-actor.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Actor 관련 정확한 TypeScript declaration을 고정한다.

## 1. Actor identity, factory와 context

`ActorId`는 Location Store transaction domain 전체에서 유일한 logical ID다. UTF-8 encoded 크기는
1..255 bytes이고 case-sensitive exact value로 비교하며 normalization하지 않는다. 일반 message는
`ActorId`만 받고 current authority를 resolve한다. `ActorRef`는 exact incarnation을 변경하거나 session에
bind할 때 사용하는 immutable location snapshot이다.

```ts
export interface ZLinkActor {
    readonly actorId: ActorId;
    readonly context: ZLinkActorContext;
    configure?(): void;
}

export interface ZLinkActorContext {
    readonly meshName: string;
    readonly spotId?: SpotId;
    readonly boundSession: ZLinkBoundSession;
    joinSpot(spotId: SpotId, request: unknown): ZLinkActorJoinSpotCall;
    joinEntrySpot(request: unknown): ZLinkActorJoinEntrySpotCall;
}

export interface ZLinkActorFactory<TActor extends ZLinkActor = ZLinkActor> {
    create(actorId: ActorId, context: ZLinkActorContext, signal?: AbortSignal): Promise<TActor>;
}

export interface ZLinkActorHandlerRegistry {
    addHandler<THandler>(handlerType: Type<THandler>, packetName?: string): this;
}

export interface ZLinkActorJoinCall<TSelf> {
    timeout(timeoutMs: number): TSelf;
    submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall
    extends ZLinkActorJoinCall<ZLinkActorJoinEntrySpotCall> {}

export interface ZLinkActorJoinSpotCall
    extends ZLinkActorJoinCall<ZLinkActorJoinSpotCall> {}

export type ZLinkActorJoinResult<TReply = unknown> =
    | { readonly status: 'accepted'; readonly actor: ActorRef; readonly reply: TReply }
    | { readonly status: 'rejected'; readonly rejection: TReply };
```

`ActorId`와 `ActorRef`의 canonical declaration은
[기초 타입과 구성](01-foundation-configuration.ko.md)이 소유한다. 이 문서는 Actor lifecycle과 manager가
그 타입을 사용하는 정확한 위치만 고정한다.

## 2. Global client와 manager

```ts
export interface ZLinkActorClient {
    sendToActor(actorId: ActorId, message: unknown): ZLinkActorSendCall;
    requestToActor(actorId: ActorId, request: unknown): ZLinkActorRequestCall;
}

export interface ZLinkActorManager {
    create(actorId: ActorId, actorType: string): ZLinkActorCreateCall;
    getOrCreate(actorId: ActorId, actorType: string): ZLinkActorGetOrCreateCall;
    find(actorId: ActorId, signal?: AbortSignal): Promise<ActorRef | undefined>;
    findSpot(actorId: ActorId, signal?: AbortSignal): Promise<SpotRef | undefined>;
    destroy(actor: ActorRef, signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkActorCreateCall {
    inMesh(meshName: string): this;
    request(request: unknown): this;
    timeout(timeoutMs: number): this;
    submit(signal?: AbortSignal): Promise<ZLinkActorCreateResult>;
    yield(signal?: AbortSignal): Promise<ZLinkActorCreateResult>;
}

export interface ZLinkActorGetOrCreateCall {
    inMesh(meshName: string): this;
    request(request: unknown): this;
    timeout(timeoutMs: number): this;
    submit(signal?: AbortSignal): Promise<ZLinkActorCreateResult>;
    yield(signal?: AbortSignal): Promise<ZLinkActorCreateResult>;
}

export type ZLinkActorCreateResult =
    | { readonly status: 'existing'; readonly actor: ActorRef }
    | {
        readonly status: 'created';
        readonly actor: ActorRef;
        readonly reply?: unknown;
      }
    | { readonly status: 'rejected'; readonly reply?: unknown };

export interface ZLinkActorRequestCall {
    metadata(key: string, value: string): this;
    timeout(timeoutMs: number): this;
    submit<TReply>(signal?: AbortSignal): Promise<TReply>;
    yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkActorSendCall {
    metadata(key: string, value: string): this;
    submit(signal?: AbortSignal): Promise<void>;
}
```

`create`와 `getOrCreate`가 반환하는 call은 single-use다. 같은 option을 두 번 설정하면
`InvalidConfiguration`, terminal `submit(...)` 또는 `yield(...)`를 두 번 호출하면 `AlreadySubmitted`다. 두
terminal은 같은 `ZLinkActorCreateResult`를 반환한다. `yield(...)`는 `SpotWide` User Spot 또는 Instance Spot
application callback에서만 현재 Spot gate를 반납하며, 다른 문맥에서는 reservation과 factory 실행 전에
`InvalidConfiguration`으로 완료한다. `inMesh(...)`를
생략했는데 eligible Mesh가 둘 이상이면 `MeshSelectionRequired`, object-role Mesh가 하나도 없으면
`ObjectClientNotConfigured`다. 명시한 Mesh가 없으면 `MeshNotFound`다.
Target RID나 predicate를 지정하는 최초 배치 option은 공개하지 않는다.

`create`는 같은 ActorId의 ready incarnation이 있으면 `ActorAlreadyExists`, stable type이 다르면
`ActorTypeMismatch`다. 새 attempt는 `created` 또는 `rejected`를 반환한다. `getOrCreate`는 같은 type의
ready Actor에서 factory와 creation callback을 호출하지 않고 `existing`을 반환하며, creating attempt가 있으면
authority 변경을 기다린다. Ready면 `existing`, rejection cleanup이면 새 reservation으로 자신의 request를
실행한다. 서로 다른 operation은 앞선 `rejected` reply를 공유하지 않고 같은 operation ID의 retry만 terminal
result를 재사용한다. 전체 deadline이
끝나면 `DeadlineExceeded`, capacity가 없으면 `PlacementCapacityExhausted`다. ActorRef의 object generation이
current와 다르면 `ActorGenerationStale`, 이동 중이면 retriable `ActorMoving`이다.

Actor create는 선택한 owner MeshNode의 Entry Spot creation callback 결과가 승인일 때만 membership과 Ready
barrier를 같은 lifecycle에서 완료한다. 최초 생성에는 join·joined callback을 호출하지 않는다.
Ready 이후 one-way message는 Actor queue에 직접 제출한다. Resolve 또는 queue admission 이후 stale route가
확인되어도 Framework는 새 owner를 찾아 같은 operation을 hidden retry하지 않는다.

Actor join call은 `submit(...)`만 제공하고 `yield(...)`를 제공하지 않는다. `SpotWide` User Spot의 member
Actor가 Actor·Spot·Channel request 또는 worker call을 `yield(...)`하면 Actor queue claim은 유지하고 User
Spot gate만 반환한다. 같은 Actor의 다음 job은 terminal continuation이 gate를 다시 얻어 현재 job을 완료할
때까지 시작하지 않는다. Entry Spot과 `PerActor` User Spot Actor에서는 request·worker operation submit 전에
`InvalidConfiguration`으로 완료한다.

`SpotWide` member Actor가 현재 User Spot을 떠나는 `joinSpot(...).submit(...)`을 기다리면 source lifecycle
callback이 같은 User Spot gate를 얻을 수 없다. Framework는 join operation submission, outbound admission과
source·target queue 변경 전에 `InvalidConfiguration`으로 완료하며 callback을 inline 또는 재진입 방식으로
호출하지 않는다.

`ZLinkActorContext.spotId`가 없으면 Actor는 current Entry Spot member이고 값이 있으면 해당 User Spot member다.
같은 상태를 나타내는 별도 boolean이나 mutable Spot instance를 제공하지 않는다. `findSpot(actorId)`도 current User
Spot membership만 `SpotRef`로 반환하며 Entry Spot에서는 `undefined`다. Factory는 target attempt마다 새 Actor와
context를 만들고 cross-node restore가 실패한 instance를 다음 attempt에 재사용하지 않는다.

## 3. Session binding

Session binding은 `ActorRef.actorId + objectGeneration`의 exact incarnation을 고정한다. Local Actor instance를
받는 bind overload, global Actor directory, handle resolver와 별도 ActorRef snapshot 변환 API는 제공하지 않는다.
`find(actorId)`는 해당 session에 이미 bind된 Actor만 조회하며 global directory가 아니다.

`boundSession`의 push는 현재 binding token이 지정하는 connection에만 보내는 one-way operation이다. 연결이
교체되거나 binding generation이 바뀌면 이전 operation을 새 connection으로 retarget하거나 hidden retry하지
않는다. Disconnect는 binding만 해제하며 Actor와 Spot membership은 유지한다.

Public trace category는 `actor-relocation`다. 의미와 검증 기준은
[Actor model](../../../22-actor-model.ko.md), [Spot·Actor membership](../../../23-spot-actor.ko.md),
[Session Actor dispatch](../../../31-session-actor-dispatch.ko.md)가 소유한다.
