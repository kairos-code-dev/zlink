<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Node.js Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md) | [다음: Node.js STREAM Open Items](stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework NestJS STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `NestJS`에서 `STREAM`을 어떤 표면으로 올릴지 정리한다.

## 1. 방향

`STREAM`은 packet session과 raw session 두 축으로 설명한다.
recv loop를 application 표면에 직접 올리지 않는 편을 기본으로 본다.

## 2. Session 예시

```ts
export interface ZLinkStream {
  sessionId: string;
  write(payload: Message, flags?: SendFlags): Promise<void>;
  writePacket(header: Message, payload: Message, flags?: SendFlags): Promise<void>;
}

export interface ZLinkPacketStreamSession {
  onPacket(
    stream: ZLinkStream,
    header: Message,
    payload: Message,
  ): Promise<void>;
}

export interface ZLinkRawStreamSession {
  onRaw(
    stream: ZLinkStream,
    payload: Message,
  ): Promise<void>;
}
```
