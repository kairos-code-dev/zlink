[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework FastAPI STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `FastAPI`에서 `STREAM`을 어떤 표면으로 올릴지 정리한다.

## 1. 방향

`STREAM`은 packet handler와 raw handler 두 축으로 설명한다.
recv loop를 application 표면에 직접 올리지 않는 편을 기본으로 본다.

## 2. Handler

```python
class ZLinkStreamPacketHandler(Protocol):
    async def handle(
        self,
        header: Message,
        body: Message,
        context: ZLinkStreamContext,
    ) -> None: ...

class ZLinkStreamRawHandler(Protocol):
    async def handle(
        self,
        payload: Message,
        context: ZLinkStreamContext,
    ) -> None: ...
```
