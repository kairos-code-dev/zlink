<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Python STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Python 묶음](./README.ko.md) | [STREAM](./fastapi-stream.ko.md)

# Draft -- ZLink Framework Python STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python` `STREAM` 초안을 샘플로 보기 위한 문서다.

```python
class RouteSession(ZLinkPacketStreamSession):
    async def on_packet(
        self,
        stream: ZLinkStream,
        header: Message,
        payload: Message,
    ) -> None:
        return None

class RawSession(ZLinkRawStreamSession):
    async def on_raw(
        self,
        stream: ZLinkStream,
        payload: Message,
    ) -> None:
        return None
```

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Python STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:bottom:end -->
