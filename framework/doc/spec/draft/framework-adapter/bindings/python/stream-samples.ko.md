[스펙 목차](../../../README.ko.md)

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
        body: Message,
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
