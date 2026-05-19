<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Node.js STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [STREAM](./nestjs-stream.ko.md)

# Draft -- ZLink Framework Node.js STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `STREAM` 초안을 샘플로 보기 위한 문서다.

```ts
@Injectable()
export class RouteSession implements ZLinkPacketStreamSession {
  async onPacket(
    stream: ZLinkStream,
    header: Message,
    payload: Message,
  ): Promise<void> {
  }
}

@Injectable()
export class RawSession implements ZLinkRawStreamSession {
  async onRaw(
    stream: ZLinkStream,
    payload: Message,
  ): Promise<void> {
  }
}
```
