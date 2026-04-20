[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [SPOT](./nestjs-spot.ko.md) | [STREAM](./nestjs-stream.ko.md) | [Registry](./nestjs-registry.ko.md)

# Draft -- ZLink Framework Node.js Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js`에서 `ZLink Framework`가 노출할 interface와
> decorator를 한 곳에 모은 기준 문서다.

## 1. 기본 타입

```ts
export interface ZLinkHandlerContext {
  channelName?: string;
  packetName?: string;
  contentType?: string;
  correlationId?: string;
  deadline?: Date;
}

export interface ZLinkSendOptions {
  packetName?: string;
}

export interface ZLinkRequestOptions {
  packetName?: string;
  timeoutMs?: number;
}
```

## 2. Client

```ts
export interface ZLinkClient {
  send<TMessage>(
    channelName: string,
    message: TMessage,
    options?: ZLinkSendOptions
  ): Promise<void>;

  request<TReply>(
    channelName: string,
    request: unknown,
    options?: ZLinkRequestOptions
  ): Promise<TReply>;
}
```

## 3. Decorator

```ts
export function ZLinkPacket(packetName: string): ClassDecorator;
export function ZLinkRequest(packetName?: string): MethodDecorator;
export function ZLinkSend(packetName?: string): MethodDecorator;
export function ZLinkEvent(packetName?: string): MethodDecorator;
```

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packetName`
2. payload 타입 `@ZLinkPacket`
3. payload constructor 또는 schema 이름

## 4. Handler

```ts
export interface ZLinkRequestHandler<TReq, TRep> {
  handle(
    request: TReq,
    context: ZLinkRequestContext
  ): Promise<TRep>;
}

export interface ZLinkSendHandler<TMsg> {
  handle(
    message: TMsg,
    context: ZLinkSendContext
  ): Promise<void>;
}

export interface ZLinkEventHandler<TEvent> {
  handle(
    event: TEvent,
    context: ZLinkEventContext
  ): Promise<void>;
}
```

## 5. 중요한 규칙

- 같은 outbound channel은 자동 연결과 수동 연결 중 하나만 선택한다.
- 일반 channel messaging의 handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
