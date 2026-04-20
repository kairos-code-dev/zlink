[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [STREAM](./fastapi-stream.ko.md)

# Draft -- ZLink Framework Python STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python` `STREAM` 초안을 샘플로 보기 위한 문서다.

```python
class RouteHandlers:
    @zlink_stream_packet()
    async def on_packet(
        self,
        header: RouteHeader,
        body: Message,
        context: ZLinkStreamContext,
    ) -> None:
        return None

    @zlink_stream_raw()
    async def on_raw(
        self,
        payload: Message,
        context: ZLinkStreamContext,
    ) -> None:
        return None
```
