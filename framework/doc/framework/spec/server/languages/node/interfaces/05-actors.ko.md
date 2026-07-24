# Node.js Actor와 session binding 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Actor model](../../../22-actor-model.ko.md) ·
[Spot·Actor membership](../../../23-spot-actor.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 Actor 관련 정확한 TypeScript declaration을 고정한다.

Session에 bind된 Actor의 physical disconnect는 Framework가 automatic all-settled로 통지한다. Actor
disconnect callback은 destroy·leave·membership 변경이 아니다. Actor relocation은 같은 ObjectGeneration에
대해 owner·membership commit, 필요한 lifecycle callback과 accepted journal replay·logical timer 복원, durable source cleanup,
`Completed` CAS를 차례로 끝낸 뒤 command 44·45로 해당 binding route만 바꾼다. Relocation 자체는
disconnect callback을 실행하지 않는다. 같은 Session의 다른 Actor route와 physical STREAM connection은
유지하며 routed ACK와 steady normalization 전에는 target session packet·push admission을 열지 않는다.

## 1. Actor identity, factory와 context

`ActorId`는 Location Store transaction domain 전체에서 유일한 logical ID다. UTF-8 encoded 크기는
1..255 bytes이고 case-sensitive exact value로 비교하며 normalization하지 않는다. 일반 message는
`ActorId`만 받고 current authority를 resolve한다. `ActorRef`는 exact incarnation을 변경하거나 session에
bind할 때 사용하는 immutable location snapshot이다.

```ts
export interface ZLinkActor {
    readonly context: ZLinkActorContext;
    configure?(): void;
    onJoinCompleted?(completion: ZLinkActorJoinCompletion): Promise<void>;
}

export interface ZLinkActorContext {
    readonly actorId: ActorId;
    readonly objectGeneration: bigint;
    readonly meshName: string;
    readonly spotId?: SpotId;
    readonly boundSession: ZLinkBoundSession;
    joinSpot(spotId: SpotId): ZLinkActorJoinSpotCall;
    joinSpot(spotId: SpotId, request: unknown): ZLinkActorJoinSpotCall;
    joinEntrySpot(): ZLinkActorJoinEntrySpotCall;
    joinEntrySpot(request: unknown): ZLinkActorJoinEntrySpotCall;
}

export interface ZLinkActorFactory<TActor extends ZLinkActor = ZLinkActor> {
    create(context: ZLinkActorContext, signal?: AbortSignal): Promise<TActor>;
}

export interface ZLinkActorHandlerRegistry {
    addHandler<THandler>(handlerType: Type<THandler>, packetName?: string): this;
}

export interface ZLinkActorJoinCall<TSelf> {
    timeout(timeoutMs: number): TSelf;
    defer(): void;
}

export interface ZLinkActorJoinEntrySpotCall
    extends ZLinkActorJoinCall<ZLinkActorJoinEntrySpotCall> {}

export interface ZLinkActorJoinSpotCall
    extends ZLinkActorJoinCall<ZLinkActorJoinSpotCall> {}

export interface ZLinkActorJoinOperationId {
    readonly high: bigint;
    readonly low: bigint;
}

export type ZLinkActorJoinCompletion =
    | {
        readonly status: 'accepted';
        readonly operationId: ZLinkActorJoinOperationId;
        readonly actor: ActorRef;
        readonly reply?: ZLinkMessage;
      }
    | {
        readonly status: 'rejected';
        readonly operationId: ZLinkActorJoinOperationId;
        readonly reply?: ZLinkMessage;
      }
    | {
        readonly status: 'failed';
        readonly operationId: ZLinkActorJoinOperationId;
        readonly kind: ZLinkFrameworkErrorKind;
        readonly isRetriable: boolean;
      };
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

Actor join call은 동기 `defer()`만 제공하고 `submit(...)`·`yield(...)`를 제공하지 않는다. `SpotWide` User Spot의 member
Actor가 Actor·Spot·Channel request 또는 worker call을 `yield(...)`하면 Actor queue claim은 유지하고 User
Spot gate만 반환한다. 같은 Actor의 다음 job은 terminal continuation이 gate를 다시 얻어 현재 job을 완료할
때까지 시작하지 않는다. Entry Spot과 `PerActor` User Spot Actor에서는 request·worker operation submit 전에
`InvalidConfiguration`으로 완료한다.

`defer()`는 current handler registration만 완료하고 handler 정상 terminal 뒤 Join을 실행한다. Handler가
`yield(...)`를 사용하면 최종 continuation terminal까지 barrier를 활성화하지 않는다. Result는 same operation
ID의 `onJoinCompleted(...)` Actor callback으로 전달한다.
Operation ID는 completion idempotency ID이며 RelocationId나 reservation ID가 아니다. Same-node outcome과
Rejected·commit 전 Failed completion retry는 current process lifetime으로 제한한다. Cross-node commit 뒤
Accepted만 Relocation manifest에 operation ID, optional reply와 cursor를 보존해 durable at-least-once로 전달한다.
Request 없는 overload는 empty `ZLinkMessage`를 고정한다. Timeout 기본값은 5초이고 명시 값은
millisecond 올림 기준 finite `1..2_147_483_647` ms다. `defer()`에서 monotonic absolute deadline을 고정한다.

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
