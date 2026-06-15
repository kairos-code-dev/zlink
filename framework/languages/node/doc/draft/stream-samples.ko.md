<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Node.js STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [STREAM](../spec/nestjs-stream.ko.md)

# Draft -- ZLink Framework Node.js STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `STREAM` 초안을 샘플로 보기 위한 문서다.

## 1. Header session

```ts
@Injectable()
export class RouteSession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {}

  async onConnected(signal?: AbortSignal): Promise<void> {
  }

  async onDisconnected(signal?: AbortSignal): Promise<void> {
  }

  async onError(
    error: ZLinkStreamError,
    signal?: AbortSignal,
  ): Promise<void> {
  }

  async onDispatch(
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    await this.context.client
      .reply(new Pong())
      .submit(signal);
  }
}
```

STREAM public session은 단일 `onDispatch(header, payload)` 표면을 사용한다. raw
session public type은 채택하지 않는다.

## 2. Actor relay

```ts
@Injectable()
export class ActorRelaySession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly actors: ZLinkActorManager,
  ) {}

  async onConnected(signal?: AbortSignal): Promise<void> {
  }

  async onDisconnected(signal?: AbortSignal): Promise<void> {
  }

  async onError(
    error: ZLinkStreamError,
    signal?: AbortSignal,
  ): Promise<void> {
  }

  async onDispatch(
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    const actor = await this.actors.getOrCreate('player-42', 'player', signal);
    const bound = await this.context.actors.bind(actor, signal);
    await bound.relay(header, payload, signal);
  }
}
```

## 3. 회귀 테스트

이 샘플 문서는 아래 회귀 항목과 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| header session node | `onDispatch(header, payload)`가 호출된다. |
| session actor relay bridge | session에서 actor bind와 relay가 public session 표면으로 동작한다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Node.js STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:bottom:end -->
