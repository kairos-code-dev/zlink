[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [STREAM](./nestjs-stream.ko.md)

# Draft -- ZLink Framework Node.js STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `STREAM` 초안을 샘플로 보기 위한 문서다.

```ts
@Injectable()
export class RouteHandlers {
  @ZLinkStreamPacket()
  async onPacket(
    header: RouteHeader,
    body: Message,
    context: ZLinkStreamContext,
  ): Promise<void> {
  }

  @ZLinkStreamRaw()
  async onRaw(
    payload: Message,
    context: ZLinkStreamContext,
  ): Promise<void> {
  }
}
```
